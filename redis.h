#ifndef __SYLVANAS_REDIS_H
#define __SYLVANAS_REDIS_H

#include <uv.h>

#include <hiredis/hiredis.h>
#include <hiredis/adapters/libuv.h>

struct item_query {
    char* key;
    char* realm;
    char* faction;
    int item;
    uv_stream_t* client;
};

typedef struct item_query itemQuery;

void redisInit();
void redisDestroy();
void redisQuery(itemQuery* query, redisCallbackFn* cb);
void redisUpdate(itemQuery* query, char* data);

#endif
