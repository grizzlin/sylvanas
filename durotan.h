#ifndef __SYLVANAS_DUROTAN_H
#define __SYLVANAS_DUROTAN_H

#include <uv.h>
#include "redis.h"

void durotanInit();
void durotanDestroy();
void durotanReply(itemQuery* query, char* reply, int len);

#endif
