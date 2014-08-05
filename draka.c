#include "draka.h"
#include <stdio.h>
#include <stdlib.h>

#define ERROR(message, code) do { \
    fprintf(stderr, "%s: [%s: %s]\n", message, uv_err_name(code), uv_strerror(code)); \
    exit(1); \
} while(0)

#define SOCKET "/tmp/sylvanas.draka.sock"

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

    // TODO allocate custom struct in client->data

    printf("draka: got connection\n");

    uv_read_start((uv_stream_t*) client, drakaOnAllocate, drakaOnRead);
}

void drakaOnAllocate(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
    buf->base = malloc(size);
    buf->len = size;
}

void drakaOnRead(uv_stream_t* client, ssize_t size, const uv_buf_t* buf) {
    if(size == UV_EOF) {
        printf("draka: client disconnected\n");
        uv_close((uv_handle_t*) client, drakaOnClose);
        goto free;
    }

    if(size < 0) ERROR("draka read", size);

    // TODO process message

free:
    free(buf->base);
}
