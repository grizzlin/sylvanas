#include "redis.h"
#include <stdio.h>
#include <stdlib.h>

extern uv_loop_t* loop;
static redisAsyncContext* ctx;

void onConnect(const redisAsyncContext* ctx, int status) {
    if(status != REDIS_OK) {
        printf("Error connecting to redis: %s\n", ctx->errstr);
    }

    printf("redis: connected\n");
}

void redisInit() {
    ctx = redisAsyncConnect("127.0.0.1", 6379);
    if(ctx->err) {
        printf("Redis connection error: %s", ctx->errstr);
        redisAsyncFree(ctx);
        exit(1);
    }

    redisLibuvAttach(ctx, loop);
    redisAsyncSetConnectCallback(ctx, onConnect);
}

void redisDestroy() {
    redisAsyncDisconnect(ctx);
}

void redisQuery(itemQuery* query, redisCallbackFn* cb) {
    redisAsyncCommand(ctx, cb, query, "GET %s", query->key);
}

void redisUpdate(itemQuery* query, char* data) {
    redisAsyncCommand(ctx, NULL, NULL, "SET %s %s", query->key, data);
}

void redisClear(char* keys) {
    redisAsyncCommand(ctx, NULL, NULL, "DEL %s", keys);
}
