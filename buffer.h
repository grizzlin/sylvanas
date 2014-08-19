#ifndef __SYLVANAS_BUFFER_H
#define __SYLVANAS_BUFFER_H

#include <stdlib.h>

void bufferAppend(char* buffer, char* chunk, size_t n);
char* bufferGetMessage(char* buffer, size_t bufferSize);
int bufferGetMessageHeader(char** message);
int bufferSplitMessage(char* message, char** dst, size_t n);

#endif
