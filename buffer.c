#include "buffer.h"
#include <string.h>

void bufferAppend(char* buffer, char* chunk, size_t n) {
    strncat(buffer, chunk, n);
}

char* bufferGetMessage(char* buffer, size_t bufferSize) {
    char* ptr = buffer;

    if(!index(ptr, '\n')) return NULL;

    char* message = strdup(strsep(&ptr, "\n"));

    size_t n = strnlen(ptr, bufferSize - strlen(buffer));
    memcpy(buffer, ptr, n);
    memset(buffer + n, 0, bufferSize - n);

    return message;
}

int bufferGetMessageHeader(char** message) {
    return atoi(strsep(message, ","));
}

int bufferSplitMessage(char* message, char** dst, size_t n) {
    int i;

    for(i = 0; i < n && message != NULL; i++) {
        dst[i] = strsep(&message, ",");
    }

    return i;
}
