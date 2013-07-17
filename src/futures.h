#ifndef FUTURES_H
#define FUTURES_H

#include "jega.h"
#include "loop.h"
#include "timer.h"

typedef enum {
    F_PENDING = 0,
    F_CANCELLED,
    F_FINISHED 
} future_state;


typedef struct {
    PyObject_HEAD
    PyObject *result;
    PyObject *exc_info;
    PyObject *callbacks;
    PyObject *waiters;
    // PyObject *greenlet;
    HandleObject *handle;
    LoopObject *loop;
    future_state state;
    PyObject *dict;
} FutureObject;


extern PyTypeObject FutureObjectType;

FutureObject* make_future(LoopObject *loop);

int set_result(FutureObject *self, PyObject *result);

int set_exception(FutureObject *self, PyObject *exc_info);

#endif
