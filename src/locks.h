#ifndef LOCKS_H
#define LOCKS_H

#include "jega.h"

typedef struct {
    PyObject_HEAD
    uint32_t counter;
    PyObject *waiters;
} SemaphoreObject;

extern PyTypeObject SemaphoreObjectType;

typedef struct {
    PyObject_HEAD
    uint32_t counter;
    PyObject *waiters;
    uint32_t init_counter;
} BoundedSemaphoreObject;

extern PyTypeObject BoundedSemaphoreObjectType;

typedef struct {
    PyObject_HEAD
    uint32_t counter;
    PyObject *waiters;
} LockObject;

extern PyTypeObject LockObjectType;

typedef struct {
    PyObject_HEAD
    PyObject *block;
    uint32_t count;
    PyObject *owner;
} RLockObject;

extern PyTypeObject RLockObjectType;

typedef struct {
    PyObject_HEAD
    PyObject *lock;
    PyObject *waiters;
    PyObject *dict;
} ConditionObject;

extern PyTypeObject ConditionObjectType;

PyObject* semaphore_acquire(SemaphoreObject *self, PyObject *blocking, long timeout);

PyObject* semaphore_release(SemaphoreObject *self);

PyObject* init_locks_module(PyObject *m);

#endif
