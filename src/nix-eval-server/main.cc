#include <cstdio>
#include <grpcpp/support/status.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include "canon-path.hh"
#include "eval-gc.hh"
#include "eval.hh"
#include "fetch-settings.hh"
#include "flake/flake.hh"
#include "flake/lockfile.hh"
#include "flake/settings.hh"
#include "grpcpp/server_context.h"
#include "nixexpr.hh"
#include "pos-idx.hh"
#include "shared.hh"
#include "source-path.hh"
#include "store-api.hh"
#include "terminal.hh"
#include "value.hh"
#include <memory>
#include <nix-eval-server.pb.h>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <sstream>
#include <optional>
#include "nix-eval-server.grpc.pb.h"
#include <grpc++/grpc++.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <string_view>
#include <thread>

#define REPORT_ERROR(e) \
    (std::cerr << "CAUGHT ERROR " << __FILE__ << ":" << __LINE__ << "\n" << nix::filterANSIEscapes(e.what(), true))

const static int envSize = 32768;

std::unique_ptr<nix::EvalState> state;
std::shared_ptr<nix::StaticEnv> baseStaticEnv;
nix::Env * baseEnv;
nix::Displacement displ = 0;

nix::fetchers::Settings fetchSettings;

nix::EvalSettings evalSettings{
    nix::settings.readOnlyMode,
    {},
};

nix::Value * evaluate(nix::Expr * e, nix::Env * env)
{
    auto v = state->allocValue();
    e->eval(*state, *env, *v);
    state->forceValue(*v, nix::noPos);
    return v;
}

nix::Expr * parse(std::string_view s)
{
    return state->parseExprFromString(std::string{s}, nix::SourcePath(state->rootFS), baseStaticEnv);
}

void add_variable(std::string_view name, nix::Value *v) {
    auto symbol = state->symbols.create(name);
    baseStaticEnv->vars.emplace_back(symbol, displ);
    baseStaticEnv->sort();
    baseEnv->values[displ++] = v;
}

std::vector<std::string> get_attributes(const std::string & expression)
{
    nix::Value *v;
    try {
        auto expr = parse(expression);
        v = evaluate(expr, baseEnv);
        state->forceAttrs(*v, nix::noPos, "");
    } catch (nix::Error & e) {
        REPORT_ERROR(e);
        return {};
    }
    std::vector<std::string> result;
    for (auto attr : *v->attrs()) {
        result.push_back(state->symbols[attr.name].c_str());
    }
    std::sort(result.begin(), result.end());
    return result;
}

nix::flake::FlakeInputs parseFlakeInputs(nix::EvalState & state, std::string_view path, nix::Expr * flakeExpr)
{
    auto env = baseEnv;

    auto flakeAttrs = dynamic_cast<nix::ExprAttrs *>(flakeExpr);

    nix::flake::FlakeInputs inputs;

    if (!flakeAttrs) {
        // diagnostics.push_back({"must be an attribute set", Location(state, flakeExpr).range});
        return inputs;
    }

    // const auto sDescription = state.symbols.create("description");
    const auto sInputs = state.symbols.create("inputs");
    // const auto sOutputs = state.symbols.create("outputs");
    // const auto sNixConfig = state.symbols.create("nixConfig");

    bool outputsFound = false;
    for (auto [symbol, attr] : flakeAttrs->attrs) {
        if (symbol == sInputs) {
            auto inputsAttrs = dynamic_cast<nix::ExprAttrs *>(attr.e);
            if (!inputsAttrs) {
                // diagnostics.push_back({"expected an attribute set", Location{state, attr.e}.range});
                continue;
            }
            auto subEnv = &state.allocEnv(0);
            subEnv->up = env;
            // auto subEnv = updateEnv(state, flakeExpr, inputsAttrs, &env, {});

            for (auto [inputName, inputAttr] : inputsAttrs->attrs) {
                auto inputValue = evaluate(inputAttr.e, subEnv);
                try {
                    auto flakeInput = nix::flake::parseFlakeInput(
                        state, state.symbols[inputName], inputValue, nix::noPos, {}, {"root"});
                    inputs.emplace(state.symbols[inputName], flakeInput);
                } catch (nix::Error & err) {
                    REPORT_ERROR(err);
                }
            }
        }
    }
    return inputs;
}

std::optional<std::string> lockFlake(
    nix::EvalState & state, std::optional<std::string> oldLockFileStr, nix::Expr * flakeExpr, std::string_view path)
{
    try {
        std::string directoryPath(path.begin(), path.end() - std::string_view{"/flake.nix"}.length());

        // auto oldLockFile = nix::flake::LockFile::read(directoryPath + "/flake.lock");
        nix::flake::LockFile oldLockFile;
        if (oldLockFileStr) {
            oldLockFile = nix::flake::LockFile{fetchSettings, *oldLockFileStr, directoryPath + "/flake.lock"};
        }

        // std::vector<Diagnostic> diagnostics;
        auto inputs = parseFlakeInputs(state, path, flakeExpr);

        nix::fetchers::Attrs attrs;
        attrs.insert_or_assign("type", "path");
        attrs.insert_or_assign("path", directoryPath);

        auto originalRef = nix::FlakeRef(nix::fetchers::Input::fromAttrs(fetchSettings, std::move(attrs)), "");

        // auto sourceInfo = std::make_shared<nix::fetchers::Tree>(directoryPath, nix::StorePath::dummy);
        auto path = state.rootPath(nix::CanonPath("/nix-analyzer-dummy"));

        // the value of lockedRef isn't used in
        // since we are not overwriting the file
        // therefore it doesn't matter
        auto flake = nix::flake::Flake{
            .originalRef = originalRef,
            .resolvedRef = originalRef,
            .lockedRef = originalRef,
            .path = path,
            .forceDirty = false,
            .inputs = inputs,
        };

        nix::flake::Settings settings;

        auto lockedFlake = nix::flake::lockFlake(
            settings, state, originalRef, nix::flake::LockFlags{.writeLockFile = false}, flake, oldLockFile);

        auto [lockFileStr, _] = lockedFlake.lockFile.to_string();

        // download everything to the nix store on this thread
        // so that it will be cached when this happens on the main thread
        // getFlakeLambdaArg(state, result);

        return lockFileStr;
    } catch (nix::Error & err) {
        REPORT_ERROR(err);
        return {};
    }
}

void printValue(std::ostream & stream, nix::EvalState & state, nix::Value * v, int indentation = 0, int maxDepth = 1)
{
    try {
        state.forceValue(*v, nix::noPos);
    } catch (nix::Error & e) {
        REPORT_ERROR(e);
        stream << "[error]";
        return;
    }
    std::string spaces(indentation, ' ');
    switch (v->type()) {
    case nix::nAttrs:
        if (state.isDerivation(*v)) {
            stream << "<DERIVATION>";
            break;
        }
        if (maxDepth > 0) {
            stream << "{\n";
            int n = 0;
            for (auto & i : *v->attrs()) {
                stream << spaces << "  ";
                if (n > 20) {
                    stream << "/* ... */ \n";
                    break;
                }
                stream << state.symbols[i.name] << " = ";
                printValue(stream, state, i.value, indentation + 2, maxDepth - 1);
                stream << ";\n";
                n++;
            }

            stream << spaces << "}";
        } else {
            stream << "{ /* ... */ }";
        }
        break;
    case nix::nList:
        if (maxDepth > 0) {
            stream << "[\n";
            int n = 0;
            for (auto & i : v->listItems()) {
                stream << spaces << "  ";
                if (n > 20) {
                    stream << "/* ... */ \n";
                    break;
                }
                printValue(stream, state, i, indentation + 2, maxDepth - 1);
                stream << "\n";
                n++;
            }

            stream << spaces << "]";
        } else {
            stream << "[ /* ... */ ]";
        }
        break;
    default:
        v->print(state, stream);
        break;
    }
}

std::string documentationPrimop(nix::Value * v)
{
    assert(v->isPrimOp());
    std::stringstream ss;
    ss << "### built-in function `" << v->primOp()->name << "`";
    if (!v->primOp()->args.empty()) {
        ss << " *`";
        for (const auto & arg : v->primOp()->args) {
            ss << arg << " ";
        }
        ss << "`*";
    }
    ss << "\n\n";
    if (v->primOp()->doc) {
        std::string_view doc{v->primOp()->doc};
        int spacesToRemoveAtStartOfLine = 6;
        for (char c : doc) {
            if (c == '\n') {
                ss << '\n';
                spacesToRemoveAtStartOfLine = 6;
            } else if (spacesToRemoveAtStartOfLine > 0 && c == ' ') {
                spacesToRemoveAtStartOfLine--;
            } else {
                ss << c;
                spacesToRemoveAtStartOfLine = -1;
            }
        }
    }
    return ss.str();
}

std::string valueType(nix::Value * v)
{
    if (v->isPrimOp()) {
        return "primop";
    }
    switch (v->type()) {
    case nix::nThunk:
        return "thunk";
    case nix::nInt:
        return "integer";
    case nix::nFloat:
        return "float";
    case nix::nBool:
        return "boolean";
    case nix::nString:
        return "string";
    case nix::nPath:
        return "path";
    case nix::nNull:
        return "null";
    case nix::nAttrs:
        return "attrset";
    case nix::nList:
        return "list";
    case nix::nFunction:
        return "function";
    case nix::nExternal:
        return "external";
    }
}

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using nix_eval_server::GetAttributesRequest;
using nix_eval_server::GetAttributesResponse;
using nix_eval_server::HoverRequest;
using nix_eval_server::HoverResponse;
using nix_eval_server::LockFlakeRequest;
using nix_eval_server::LockFlakeResponse;
using nix_eval_server::AddVariableRequest;
using nix_eval_server::AddVariableResponse;
using nix_eval_server::NixEvalServer;

class NixEvalServerImpl final : public NixEvalServer::Service
{
    Status GetAttributes(
        ServerContext * context, const GetAttributesRequest * request, GetAttributesResponse * response) override
    {
        const auto expression = request->expression();
        const auto attributes = get_attributes(expression);
        for (const auto & attribute : attributes) {
            response->add_attributes(attribute);
        }
        return Status::OK;
    }

    Status LockFlake(ServerContext * context, const LockFlakeRequest * request, LockFlakeResponse * response) override
    {
        try {
            std::optional<std::string> oldLockFile;
            if (request->has_old_lock_file()) {
                oldLockFile = std::string{request->old_lock_file()};
            }
            const auto expression = request->expression();
            auto expr = parse(expression);
            auto lock_file = lockFlake(*state, oldLockFile, expr, "/flake.nix");
            if (lock_file) {
                response->set_lock_file(*lock_file);
                return Status::OK;
            }
        } catch (std::exception & ex) {
            REPORT_ERROR(ex);
            return {StatusCode::INTERNAL, "Failed"};
        }
    }

    Status Hover(ServerContext * context, const HoverRequest * request, HoverResponse * response) override
    {
        try {
            const auto expression = request->expression();
            auto expr = parse(expression);
            nix::Value * value = evaluate(expr, baseEnv);
            response->set_type(valueType(value));

            if (request->has_attr()) {
                state->forceAttrs(*value, nix::noPos, "");
                bool found = false;
                for (auto attr : *value->attrs()) {
                    if (state->symbols[attr.name] == request->attr()) {
                        auto pos = state->positions[attr.pos];
                        std::string path;
                        if (std::get_if<nix::Pos::String>(&pos.origin)) {
                            path = "<<string>>";
                        } else if (auto * sourcePath = std::get_if<nix::SourcePath>(&pos.origin)) {
                            path = sourcePath->to_string();
                        }
                        response->set_path(path);
                        response->set_row(pos.line);
                        response->set_col(pos.column);
                        value = attr.value;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return {StatusCode::INTERNAL, "attrset does not contain specified attr"};
                }
            }

            if (value->isPrimOp()) {
                response->set_value(documentationPrimop(value));
            } else {
                std::stringstream ss;
                printValue(ss, *state, value);
                response->set_value(ss.str());
            }
        } catch (std::exception & ex) {
            REPORT_ERROR(ex);
            return {StatusCode::INTERNAL, "Failed"};
        }
        return Status::OK;
    }

    Status AddVariable(ServerContext * context, const AddVariableRequest * request, AddVariableResponse * response) override
    {
        try {
            const auto expression = request->expression();
            const auto name = request->name();
            auto expr = parse(expression);
            nix::Value * value = evaluate(expr, baseEnv);
            add_variable(name, value);
        } catch (std::exception & ex) {
            REPORT_ERROR(ex);
            return {StatusCode::INTERNAL, "Failed"};
        }
        return Status::OK;
    }
};

void monitor_parent()
{
    pid_t original_ppid = getppid();

    while (1) {
        pid_t current_ppid = getppid();
        if (current_ppid != original_ppid) {
            std::cout << "Parent process changed, exiting" << std::endl;
            exit(1); // Exit immediately
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
int main(int argc, char ** argv)
{
    std::thread monitor_thread(monitor_parent);
    monitor_thread.detach();

    nix::initGC();
    nix::initNix();

    state = std::make_unique<nix::EvalState>(nix::LookupPath::parse({}), nix::openStore(), fetchSettings, evalSettings);
    baseStaticEnv = std::make_shared<nix::StaticEnv>(nullptr, state->staticBaseEnv.get());
    baseEnv = &state->allocEnv(envSize);
    baseEnv->up = &state->baseEnv;

    add_variable("__nix_analyzer_test", evaluate(parse(R"({test1234 = 1; test5678 = 2;} )"), baseEnv));

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    NixEvalServerImpl service;
    std::string server_address{"localhost:0"};
    ServerBuilder builder;
    int selected_port;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials(), &selected_port);
    builder.RegisterService(&service);
    std::unique_ptr<Server> server{builder.BuildAndStart()};

    std::cout << selected_port << std::endl;
    server->Wait();
}
