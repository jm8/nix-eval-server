#include <cstdio>
#include <iostream>
#include <capnp/common.h>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <capnp/ez-rpc.h>
#include <kj/async.h>
#include <kj/async.h>
#include <kj/memory.h>
#include <nix-eval-server.capnp.h>

class EvaluatorImpl : public Evaluator::Server
{
public:
    kj::Promise<void> test(TestContext context)
    {
        context.getResults().setY(context.getParams().getX() * 2.0);
        return kj::READY_NOW;
    }
};

int main(int argc, char ** argv)
{
    capnp::EzRpcServer server(kj::heap<EvaluatorImpl>(), "127.0.0.1", 0);

    auto & waitScope = server.getWaitScope();
    std::cout << server.getPort().wait(waitScope) << std::endl;

    // Run forever, accepting connections and handling requests.
    kj::NEVER_DONE.wait(waitScope);
}
