#ifndef __SYLVANAS_DRAKA_H
#define __SYLVANAS_DRAKA_H

#include <uv.h>

struct draka_session {
    char* prefix;
    char* buffer;
};

typedef struct draka_session drakaSession;

void drakaInit();
void drakaDestroy();

#endif
