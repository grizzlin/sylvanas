#include "durotan.h"
#include "buffer.h"
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

#define MSG_ITEM 0

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

    uv_fs_t req;
    uv_fs_unlink(loop, &req, SOCKET, NULL);
    
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

    // Allocate space for the message, a comma, the key, a comma, the stats, a newline, and a null character.
    int len = 1 + 1 + strlen(query->key) + 1 + n + 2;
    uv_buf_t* buf = malloc(sizeof(uv_buf_t));
    buf->base = malloc(len * sizeof(char));
    buf->len = len - 1;

    // Format the message
    snprintf(buf->base, len, "%d,%s,%s\n", MSG_ITEM, query->key, reply);

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

    printf("%s", (char*) buf->base);

    char* buffer = client->data;
    char* message;
    char* m;

    // Append chunk onto buffer
    bufferAppend(buffer, buf->base, BUFFER_SIZE - strlen(buffer));

    // Read a line, bailing out if it isn't a complete line
    message = m = bufferGetMessage(buffer, BUFFER_SIZE);
    if(message == NULL) goto free;

    // Figure out what kind of message it is
    int header = bufferGetMessageHeader(&message);

    // Figure out how many pieces there are
    int elementCount;
    switch(header) {
        case MSG_ITEM: elementCount = 3; break;
    }

    // Parse the pieces
    char** elements = malloc(elementCount * sizeof(char*));
    int parsed = bufferSplitMessage(message, elements, elementCount);
    if(parsed < elementCount) {
        fprintf(stderr, "durotan received message %d with only %d pieces\n", header, parsed);
        goto free;
    }

    // Handle message
    switch(header) {
        case MSG_ITEM: {
            char* realm = elements[0];
            char* faction = elements[1];
            int item = atoi(elements[2]);
            int keyLen = strlen(realm) + 1 +  strlen(faction) + 1 + strlen(elements[2]) + 1;

            itemQuery* query = malloc(sizeof(itemQuery));
            query->key = malloc(keyLen * sizeof(char));
            snprintf(query->key, keyLen, "%s,%s,%d", realm, faction, item);
            query->realm = strdup(realm);
            query->faction = strdup(faction);
            query->item = item;
            query->client = client;

            // Look up the key in redis
            redisQuery(query, durotanOnRedis);
            break;
        }
    }

    free(m);

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

        // Package everything into a mongoAggregateContext
        mongoAggregateContext* context = malloc(sizeof(mongoAggregateContext));
        context->req.data = (void*)context;
        context->query = query;

        // Aggregate everything on a separate thread
        uv_queue_work(loop, &context->req, mongoAggregate, durotanOnMongo);
    }
    else if(reply->type == REDIS_REPLY_STRING) {

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
