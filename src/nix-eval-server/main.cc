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

#include "eval-gc.hh"
#include "eval.hh"
#include "fetch-settings.hh"
#include "pos-idx.hh"
#include "shared.hh"
#include "source-path.hh"
#include "store-api.hh"
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

nlohmann::json ok(const nlohmann::json & json)
{
    return {{"ok", json}};
}

nlohmann::json error(const std::string & s)
{
    return {{"ok", s}};
}

nlohmann::json handle(const nlohmann::json & request)
{
    if (request["method"] == "get_attributes") {
        return ok(get_attributes(request["expression"]));
    }
    return error("unexpected method");
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
