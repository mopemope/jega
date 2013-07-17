#ifndef CHANNEL_H
#define CHANNEL_H

#include "jega.h"
#include "loop.h"
#include "queue.h"

typedef struct {
    PyObject *caller;
    PyObject *data;
} channel_msg_t;

typedef struct {
    PyObject_HEAD
    PyObject *waiters;
    LoopObject *loop;
    queue_t *pendings;
    PyObject *handle;
    uint32_t bufsize;
} ChannelObject;

extern PyTypeObject ChannelObjectType;

ChannelObject* create_channel(LoopObject *loop, uint32_t bufsize);

#endif
