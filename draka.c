#include "draka.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define ERROR(message, code) do { \
    fprintf(stderr, "%s: [%s: %s]\n", message, uv_err_name(code), uv_strerror(code)); \
    exit(1); \
} while(0)

#define SOCKET "/tmp/sylvanas.draka.sock"
#define BUFFER_SIZE 128

extern uv_loop_t* loop;
static uv_pipe_t* draka;

// Private functions
static void drakaOnClose(uv_handle_t* handle);
static void drakaOnConnection(uv_stream_t* draka, int status);
static void drakaOnAllocate(uv_handle_t* handle, size_t size, uv_buf_t* buf);
static void drakaOnRead(uv_stream_t* stream, ssize_t size, const uv_buf_t* buf);

void drakaInit() {
    int err;
    draka = malloc(sizeof(uv_pipe_t));
    uv_pipe_init(loop, draka, 0);

    uv_fs_t req;
    uv_fs_unlink(loop, &req, SOCKET, NULL);
    
    err = uv_pipe_bind(draka, SOCKET);
    if(err) ERROR("draka bind", err);

    err = uv_listen((uv_stream_t*) draka, 128, drakaOnConnection);
    if(err) ERROR("draka listen", err);

    printf("draka: listening\n");
}

void drakaDestroy() {
    uv_close((uv_handle_t*) draka, drakaOnClose);
}

//Private functions
void drakaOnClose(uv_handle_t* handle) {
    if(handle->data != NULL) {
        free(handle->data);
    }
    free(handle);
    handle = NULL;
}

void drakaOnConnection(uv_stream_t* draka, int status) {
    if(status == -1) ERROR("draka on connect", status);

    uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    uv_tcp_init(loop, client);
    int err = uv_accept(draka, (uv_stream_t*) client);
    if(err) {
        uv_close((uv_handle_t*) client, drakaOnClose);
        ERROR("draka accept", err);
    }

    drakaSession* session = calloc(1, sizeof(drakaSession));
    session->buffer = calloc(BUFFER_SIZE, sizeof(char));
    client->data = session;

    printf("draka: got connection\n");

    uv_read_start((uv_stream_t*) client, drakaOnAllocate, drakaOnRead);
}

void drakaOnAllocate(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
    buf->base = malloc(size);
    buf->len = size;
}

void drakaOnRead(uv_stream_t* client, ssize_t size, const uv_buf_t* buf) {
    drakaSession* session = (drakaSession*) client->data;

    if(size == UV_EOF) {
        printf("draka: client disconnected\n");
        free(session->prefix);
        free(session->buffer);
        uv_close((uv_handle_t*) client, drakaOnClose);
        goto free;
    }

    if(size < 0) ERROR("draka read", size);

    char* buffer = session->buffer;
    char* chunk = buf->base;
    char* message = strsep(&chunk, "\n");

    // Gotta capture the realm and faction first
    if(session->prefix == NULL) {
        int len = strlen(buffer);

        // Make sure we have enough room, then append token onto buffer
        assert(BUFFER_SIZE - len > strlen(message));
        strncat(buffer, message, BUFFER_SIZE - len);

        // If we didn't get the whole line, bail out and wait for more stuff
        if(chunk == NULL) goto free;

        // We have the whole line.  Set the prefix, clear the buffer, and keep going
        session->prefix = strdup(buffer);
        memset(buffer, 0, BUFFER_SIZE);
        message = strsep(&chunk, "\n");

        printf("Clearing redis for %s\n", session->prefix);
    }

    // Allocate a (resizable) string of keys to pass to redis for deletion
    size_t n = 1024;
    char* keys = calloc(n, sizeof(char));

    // Copy buffer over
    strncat(keys, buffer, strlen(buffer));

    // Nom the tokens
    for(; chunk != NULL; message = strsep(&chunk, "\n")) {
        int len = strlen(session->prefix) + strlen(message) + 2;

        // Resize keys
        if(strlen(keys) + len > n) {
            do { n *= 2; }
            while(strlen(keys) + len > n);
            keys = realloc(keys, n);
        }

        // Format and append the redis key onto the list of keys
        strncat(keys, session->prefix, strlen(session->prefix));
        strncat(keys, ",", 1);
        strncat(keys, message, strlen(message));
        strncat(keys, " ", 1);
    }

    // Set the buffer to contain anything remaining
    assert(strlen(message) < BUFFER_SIZE);
    memcpy(buffer, message, BUFFER_SIZE);

    redisClear(keys);

    free(keys);

free:
    free(buf->base);
}
