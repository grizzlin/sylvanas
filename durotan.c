#include "durotan.h"
#include <stdio.h>
#include <stdlib.h>
#include "redis.h"
#include "mongo.h"

#define ERROR(message, code) do { \
    fprintf(stderr, "%s: [%s: %s]\n", message, uv_err_name(code), uv_strerror(code)); \
    exit(1); \
} while(0)

#define SOCKET "/tmp/sylvanas.durotan.sock"
#define BUFFER_SIZE 256

extern uv_loop_t* loop;
static uv_pipe_t* durotan;
static uv_tcp_t* client;

// Private callbacks
void durotanOnClose(uv_handle_t* handle);
void durotanOnConnection(uv_stream_t* durotan, int status);
void durotanOnAllocate(uv_handle_t* handle, size_t size, uv_buf_t* buf);
void durotanOnRead(uv_stream_t* client, ssize_t size, const uv_buf_t* buf);
void durotanOnWrite(uv_write_t* req, int status);
void durotanOnRedis(redisAsyncContext* ctx, void* reply, void* data);
void durotanOnMongo(uv_work_t* req, int status);

void durotanInit() {
    int err;
    durotan = malloc(sizeof(uv_pipe_t));
    uv_pipe_init(loop, durotan, 0);
    
    err = uv_pipe_bind(durotan, SOCKET);
    if(err) ERROR("durotan bind", err);

    err = uv_listen((uv_stream_t*) durotan, 128, durotanOnConnection);
    if(err) ERROR("durotan listen", err);

    printf("durotan: listening\n");
}

void durotanDestroy() {
    uv_close((uv_handle_t*) durotan, durotanOnClose);
    if(client != NULL && !uv_is_closing((uv_handle_t*) client)) {
        uv_close((uv_handle_t*) client, durotanOnClose);
    }
}

void durotanReply(itemQuery* query, char* reply, int n) {
    if(uv_is_closing((uv_handle_t*) query->client) || !uv_is_writable(query->client)) {
        return;
    }

    // Allocate space for the key, a comma, the stats, a newline, and a null character.
    int len = strlen(query->key) + 1 + n + 2;
    uv_buf_t* buf = malloc(sizeof(uv_buf_t));
    buf->base = malloc(len * sizeof(char));
    buf->len = len - 1;

    // Format the message
    snprintf(buf->base, len, "%s,%s\n", query->key, reply);

    // Create write request
    uv_write_t* req = (uv_write_t*)malloc(sizeof(uv_write_t));
    req->data = (void*)buf;

    // Send her off
    uv_write(req, query->client, buf, 1, durotanOnWrite);
}

//Private callbacks
void durotanOnClose(uv_handle_t* handle) {
    if(handle->data != NULL) {
        free(handle->data);
    }
    free(handle);
    handle = NULL;
}

void durotanOnConnection(uv_stream_t* durotan, int status) {
    if(status == -1) ERROR("durotan on connect", status);

    client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    uv_tcp_init(loop, client);
    int err = uv_accept(durotan, (uv_stream_t*) client);
    if(err) {
        uv_close((uv_handle_t*) client, durotanOnClose);
        ERROR("durotan accept", err);
    }

    // Buffer for incoming messages.
    client->data = calloc(BUFFER_SIZE, sizeof(char));

    printf("durotan: got connection\n");

    uv_read_start((uv_stream_t*) client, durotanOnAllocate, durotanOnRead);
}

void durotanOnAllocate(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
    buf->base = malloc(size);
    buf->len = size;
}

void durotanOnRead(uv_stream_t* client, ssize_t size, const uv_buf_t* buf) {
    if(size == UV_EOF) {
        printf("durotan: client disconnected\n");
        uv_close((uv_handle_t*) client, durotanOnClose);
        goto free;
    }

    if(size < 0) ERROR("durotan read", size);

    printf("Received query %s\n", (char*) buf->base);

    // Append chunk onto buffer
    strcat(client->data, (char*) buf->base);
    char* p = client->data;
    char* message = strsep(&p, "\n");
    if(p == NULL) goto free;

    // Create an itemQuery, the entire message is the redis key
    itemQuery* query = malloc(sizeof(itemQuery));
    query->key = strdup(message);
    query->realm = NULL;
    query->faction = NULL;
    query->item = -1;
    query->client = client;

    // Look up the key in redis
    redisQuery(query, durotanOnRedis);

    // Perform buffer nomming
    int n = strlen(p);
    memcpy(client->data, p, n);
    memset(client->data + n, 0, BUFFER_SIZE - n);

free:
    free(buf->base);
}

void durotanOnWrite(uv_write_t* req, int status) {
    if(status < 0) ERROR("durotan write", status);
    uv_buf_t* buf = (uv_buf_t*) req->data;
    free(buf->base);
    free(buf);
    free(req);
}

void durotanOnRedis(redisAsyncContext* ctx, void* r, void* q) {
    redisReply* reply = r;
    if(reply == NULL) {
        printf("durotan: null redis reply\n");
        exit(1);
    }

    itemQuery* query = q;

    if(reply->type == REDIS_REPLY_NIL) {
        printf("Effectively going to disk\n");

        // Split up the key now that we need the individual elements.
        char* s = strdup(query->key);
        query->realm = strdup(strsep(&s, ","));
        query->faction = strdup(strsep(&s, ","));
        query->item = atoi(strsep(&s, ","));
        free(s);

        // Package everything into a mongoAggregateContext
        mongoAggregateContext* context = malloc(sizeof(mongoAggregateContext));
        context->req.data = (void*)context;
        context->query = query;

        // Aggregate everything on a separate thread
        uv_queue_work(loop, &context->req, mongoAggregate, durotanOnMongo);
    }
    else if(reply->type == REDIS_REPLY_STRING) {
        printf("Yay it's in redis\n");

        // Reply with the query and the stats from redis
        durotanReply(query, reply->str, reply->len);

        free(query->realm);
        free(query->faction);
        free(query->key);
        free(query);
    }
}

void durotanOnMongo(uv_work_t* req, int status) {
    mongoAggregateContext* context = req->data;

    durotanReply(context->query, context->result, strlen(context->result));

    redisUpdate(context->query, context->result);

    free(context->query->realm);
    free(context->query->faction);
    free(context->query->key);
    free(context->query);
    free(context->result);
    free(context);
}
