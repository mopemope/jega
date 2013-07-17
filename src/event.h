#ifndef EVENT_H
#define EVENT_H

#include "jega.h"

typedef struct {
    PyObject_HEAD
    PyObject *cond;
    uint8_t flag;
} EventObject;

extern PyTypeObject EventObjectType;

PyObject* event_set(EventObject *self);

PyObject* event_wait(EventObject *self, long timeout);

PyObject* init_event_module(PyObject *m);

#endif



