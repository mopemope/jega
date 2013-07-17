#ifndef EXECUTOR_H 
#define EXECUTOR_H 

#include "jega.h"
#include "loop.h"
#include "futures.h"
#include "queue.h"
#include "timer.h"

typedef struct {
    PyObject_HEAD
    LoopObject *loop;
    queue_t *pendings;
    // queue_t *workers;
    uint32_t run_workers;
    uint32_t max_workers;
    uint8_t shutdown;
    PyObject *weakreflist;
} ExecutorObject;

extern PyTypeObject ExecutorObjectType;

typedef struct {
    PyObject_HEAD
    LoopObject *loop;
    FutureObject *future;
    PyObject *fn;
    PyObject *args;
    PyObject *kwargs;
    // PyObject *greenlet;
} WorkerItemObject;

extern PyTypeObject WorkerItemObjectType;


PyObject* executor_submit(ExecutorObject *self, PyObject *cb, PyObject *cbargs, PyObject *kwargs);

#endif
