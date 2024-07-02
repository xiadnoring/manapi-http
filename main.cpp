#include <iostream>
#include <csignal>
#include "ManapiHttp.h"
#include "ManapiTaskHttp.h"
#include "ManapiFilesystem.h"

#define TIME

#ifdef TIME
#include <ctime>
#endif

using namespace manapi::toolbox;
using namespace manapi::net;
using namespace std;

static http server;

void handler_interrupt (int sig_int) {
    server.stop();
}


int main(int argc, char *argv[]) {
#ifdef TIME
    clock_t start = clock();
#endif

    signal (SIGABRT, handler_interrupt);
    signal (SIGKILL, handler_interrupt);
    signal (SIGTERM, handler_interrupt);

    server.set_config("config.json");

    server.GET ("/", [] (REQ(req), RESP(resp)) {
        resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/index.html");
    });

    server.GET ("/+error", [] (REQ(req), RESP(resp)) {
        MAP_STR_STR replacers = {
                {"status_code", std::to_string(resp.get_status_code())},
                {"status_message", resp.get_status_message()}
        };

        resp.set_replacers(replacers);
        resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/error.html");
    });

    server.GET("/test/[name]", [] (REQ(req), RESP(resp)) {
        resp.set_replacers({
                                   {"MSG", *req.get_param("name")}
        });
        resp.set_compress_enabled(true);
        resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test.html");
    });

    auto f1 = server.run();

    f1.get();

    printf ("OK\n");
#ifdef TIME
    clock_t stop = clock();
    printf("Time spent: %f s\n", (double)(stop - start)/CLOCKS_PER_SEC);
#endif

    return 0;
}
