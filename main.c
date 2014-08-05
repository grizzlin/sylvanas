#include <stdio.h>
#include <mcheck.h>
#include <uv.h>

#include "redis.h"
#include "mongo.h"
#include "durotan.h"
#include "draka.h"

uv_loop_t* loop;
uv_signal_t sig;

void cleanup();

int main(int argc, char** argv) {
    mtrace();

    loop = uv_default_loop();

    redisInit();
    mongoInit();
    drakaInit();
    durotanInit();
    
    uv_signal_init(loop, &sig);
    uv_signal_start(&sig, cleanup, SIGINT);

    uv_run(loop, UV_RUN_DEFAULT);

    if(uv_loop_close(loop) == UV_EBUSY) {
        printf("loop not closed\n");
    }

    muntrace();
}

void cleanup(uv_signal_t* req, int signum) {
    redisDestroy();
    mongoDestroy();
    drakaDestroy();
    durotanDestroy();
    uv_close((uv_handle_t*) &sig, NULL);
}

