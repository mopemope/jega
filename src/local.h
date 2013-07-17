#ifndef LOCAL_H
#define LOCAL_H

#include "jega.h"

typedef struct {
    PyObject_HEAD
} LocalObject;

extern PyTypeObject LocalObjectType;

PyObject* get_thread_local(PyObject *current);

PyObject* init_local_module(PyObject *m);

#endif




