#ifndef QUEUE_H
#define QUEUE_H

#include "jega.h"


typedef struct {
    void **q;
    uint32_t first;
    uint32_t last;
    uint32_t size;
    uint32_t max;
} queue_t;

queue_t* init_queue(uint32_t maxsize);

int queue_push(queue_t *q, void *o);

int queue_push_noext(queue_t *q, void *o);

void* queue_shift(queue_t *q);

void debug_queue(queue_t *q);

void destroy_queue(queue_t *q);

#endif
