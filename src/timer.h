#ifndef TIMER_H
#define TIMER_H

#include "jega.h"

typedef struct {
    PyObject_HEAD
    PyObject *loop;
    PyObject *args;
    PyObject *callback;
    PyObject *greenlet;
    char called;
    char cancelled;
    time_t seconds;
} HandleObject;

extern PyTypeObject HandleObjectType;

typedef struct {
    PyObject_HEAD
    PyObject *loop;
    PyObject *args;
    PyObject *callback;
    PyObject *greenlet;
    char called;
    char cancelled;
    time_t seconds;

    char repeat;
    uint64_t interval;
    uint64_t counter;
} TimerObject;

extern PyTypeObject TimerObjectType;


PyObject* make_handle(PyObject *loop, long seconds, PyObject *callback, PyObject *args, PyObject *greenlet);

void fire_handle(HandleObject *handle);

void fire_timer(TimerObject *timer);

int is_active_timer(TimerObject *timer);

int check_handle_obj(PyObject *obj);

int check_timer_obj(PyObject *obj);

#endif
