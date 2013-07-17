#ifndef HEAPQ_H
#define HEAPQ_H

#include "jega.h"
#include "timer.h"

typedef struct {
   TimerObject **heap;
   uint32_t size;
   uint32_t max;
} heapq_t;

heapq_t* init_heapqueue(void);

void destroy_heapqueue(heapq_t *q);

TimerObject* heappop(heapq_t *q);

int heappush(heapq_t *q, TimerObject *val);

#endif
