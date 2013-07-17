#ifndef TASk_H
#define TASK_H

#include "jega.h"

typedef struct {
    PyObject_HEAD
    PyObject *args;
    PyObject *kwargs;
    PyObject *greenlet;
    PyObject *result_value;
    char started;
} TaskObject;

extern PyTypeObject TaskObjectType;

TaskObject* spawn_task(PyObject *callback, PyObject *args, PyObject *kwargs, PyObject *parent);

#endif
