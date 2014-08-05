#include "mongo.h"
#include <stdio.h>
#include <math.h>
#include <mongoc.h>

static mongoc_client_t* client;
static char* uri = "mongodb://127.0.0.1";
static char* db = "us";

void mongoInit() {
	client = mongoc_client_new(uri);
	if(!client) {
		fprintf(stderr, "mongo: connection failed");
		exit(1);
	}
}

void mongoDestroy() {
	mongoc_client_destroy(client);
}

void mongoAggregate(uv_work_t* req) {
    mongoAggregateContext* context = req->data;
    char collectionName[64];
    snprintf(collectionName, sizeof collectionName, "%s_%s", context->query->realm, context->query->faction);
    mongoc_collection_t* collection = mongoc_client_get_collection(client, db, collectionName);

    mongoc_cursor_t* cursor;
    bson_error_t error;
    const bson_t* doc;
    bson_t* query;
    bson_t* fields;
    bson_iter_t iter;

    query = BCON_NEW("item", BCON_INT32(context->query->item));
    fields = BCON_NEW("buyout", BCON_BOOL(1), "quantity", BCON_BOOL(1), "_id", BCON_BOOL(0));

    cursor = mongoc_collection_find(collection, MONGOC_QUERY_NONE, 0, 0, 0, query, fields, NULL);

    double oldM, oldS, newM, newS;
    int n = 0;
    int i;
    while(mongoc_cursor_next(cursor, &doc)) {
        if(bson_iter_init(&iter, doc)) {
            double x;
            int quantity;
            if(bson_iter_find(&iter, "buyout")) {
                x = (double) bson_iter_int32(&iter);
            }

            if(bson_iter_find(&iter, "quantity")) {
                quantity = bson_iter_int32(&iter);
            }

            if(!n) {
                n += quantity;
                oldM = newM = x;
                oldS = 0.0;
            }
            else {
                n += quantity;
                newM = oldM + (x - oldM) / n;
                newS = oldS + (x - oldM) * (x - newM);

                oldM = newM;
                oldS = newS;
            }
        }
    }

    double mean = n > 0 ? newM : 0;
    double stddev = n > 1 ? sqrt(newS / (n - 1)) : 0;

    context->result = malloc(64 * sizeof(char));
    snprintf(context->result, 64, "%d,%.16g,%.16g", n, round(mean), round(stddev));

    if(mongoc_cursor_error(cursor, &error)) {
        fprintf(stderr, "mongo: aggregation error: %s\n", error.message);
    }

    bson_destroy(query);
    bson_destroy(fields);
    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(collection);
}
