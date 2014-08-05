#ifndef __SYLVANAS_MONGO_H
#define __SYLVANAS_MONGO_H

#include <uv.h>

#include "redis.h"

struct mongo_aggregate_context {
    uv_work_t req;
    itemQuery* query;
    char* result;
};

typedef struct mongo_aggregate_context mongoAggregateContext;

void mongoInit();
void mongoDestroy();
void mongoAggregate(uv_work_t* req);

#endif
