#include <cstdio>
#include <iostream>
#include <capnp/common.h>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <capnp/ez-rpc.h>
#include <kj/async.h>
#include <kj/async.h>
#include <kj/memory.h>
#include <nlohmann/json.hpp>

#include "canon-path.hh"
#include "eval-gc.hh"
#include "eval.hh"
#include "fetch-settings.hh"
#include "flake/flake.hh"
#include "flake/lockfile.hh"
#include "flake/settings.hh"
#include "pos-idx.hh"
#include "shared.hh"
#include "source-path.hh"
#include "store-api.hh"
#include "value.hh"
#include <memory>
#include <nix-eval-server.capnp.h>
#include <nlohmann/json_fwd.hpp>
#include <string>

std::unique_ptr<nix::EvalState> state;

nix::fetchers::Settings fetchSettings;

nix::EvalSettings evalSettings{
    nix::settings.readOnlyMode,
    {},
};

std::vector<std::string> get_attributes(const std::string & expression)
{
    nix::Value v;
    try {
        auto expr = state->parseExprFromString(expression, nix::SourcePath(state->rootFS));
        state->eval(expr, v);
        state->forceAttrs(v, nix::noPos, "");
    } catch (nix::Error & e) {
        std::cerr << "CAUGHT ERROR: " << e.what() << "\n";
        return {};
    }
    std::vector<std::string> result;
    for (auto attr : *v.attrs()) {
        result.push_back(state->symbols[attr.name].c_str());
    }
    std::sort(result.begin(), result.end());
    return result;
}

nix::Value * evaluate(nix::Expr * e, nix::Env & env)
{
    auto v = state->allocValue();
    e->eval(*state, env, *v);
    return v;
}

nix::flake::FlakeInputs parseFlakeInputs(nix::EvalState & state, std::string_view path, nix::Expr * flakeExpr)
{
    auto & env = state.baseEnv;

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
            auto subEnv = state.allocEnv(0);
            subEnv.up = &env;
            // auto subEnv = updateEnv(state, flakeExpr, inputsAttrs, &env, {});

            for (auto [inputName, inputAttr] : inputsAttrs->attrs) {
                auto inputValue = evaluate(inputAttr.e, subEnv);
                try {
                    auto flakeInput = nix::flake::parseFlakeInput(
                        state, state.symbols[inputName], inputValue, nix::noPos, {}, {"root"});
                    inputs.emplace(state.symbols[inputName], flakeInput);
                } catch (nix::Error & err) {
                    //     diagnostics.push_back(
                    //         {nix::filterANSIEscapes(err.msg(), true), Location{state, inputAttr.e}.range});
                }
            }
        }
    }
    return inputs;
}
// return inputs;
// }

std::optional<std::string> lockFlake(nix::EvalState & state, nix::Expr * flakeExpr, std::string_view path)
{
    try {
        std::string directoryPath(path.begin(), path.end() - std::string_view{"/flake.nix"}.length());

        // auto oldLockFile = nix::flake::LockFile::read(directoryPath + "/flake.lock");
        nix::flake::LockFile oldLockFile;

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
        // REPORT_ERROR(err);
        return {};
    }
}

nlohmann::json ok(const nlohmann::json & json)
{
    return {{"ok", json}};
}

nlohmann::json error(const std::string & s)
{
    return {{"error", s}};
}

nlohmann::json lock_flake(const std::string & expression)
{
    try {
        auto expr = state->parseExprFromString(expression, nix::SourcePath(state->rootFS));
        auto lock_file = lockFlake(*state, expr, "/flake.nix");
        if (lock_file) {
            return ok(*lock_file);
        }
        return error("failed to lock flake");
    } catch (nix::Error & e) {
        std::cerr << "CAUGHT ERROR: " << e.what() << "\n";
        return error("failed to lock flake");
    }
}
nlohmann::json handle(const nlohmann::json & request)
{
    try {
        if (request["method"] == "get_attributes") {
            return ok(get_attributes(request["expression"]));
        } else if (request["method"] == "lock_flake") {
            return lock_flake(request["expression"]);
        }
        return error("unexpected method");
    } catch (const std::exception & ex) {
        std::cerr << "UNHANDLED EXCEPTION: " << ex.what() << "\n";
        return error("unhandled exception");
    }
}

int main(int argc, char ** argv)
{
    nix::initGC();
    nix::initNix();

    state = std::make_unique<nix::EvalState>(nix::LookupPath::parse({}), nix::openStore(), fetchSettings, evalSettings);

    while (true) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            break;
        }
        auto request = nlohmann::json::parse(line);
        auto response = handle(request);
        std::cout << response << std::endl;
    }
}
