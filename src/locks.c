#include "locks.h"
#include "loop.h"
#include "greensupport.h"
#include "util.h"
#include "timer.h"


#define LOCKS_MOD_NAME "locks"

// --------------------------------------------------------
// Semaphore
// --------------------------------------------------------

static int
SemaphoreObject_init(SemaphoreObject *self, PyObject *args, PyObject *kwargs)
{

    int value = 1; 
    GDEBUG("self:%p", self);
    static char *keywords[] = {"value", NULL};
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i:__init__", keywords, &value)) {
        return -1;
    }
    self->counter = value;

    Py_CLEAR(self->waiters);

    self->waiters = PySet_New(NULL);
    if (self->waiters == NULL) {
        return -1;
    }
    DEBUG("self:%p counter:%d waiters:%p", self, self->counter, self->waiters);
    return 1;
}

static void
SemaphoreObject_dealloc(SemaphoreObject *self)
{
    GDEBUG("self %p", self);
    Py_CLEAR(self->waiters);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject*
fetch_timer(void)
{
    PyObject *t = NULL, *v = NULL, *tr = NULL;
    PyObject *timer;

    PyErr_Fetch(&t, &v, &tr);
    
    if (v) {
        timer = PyObject_GetAttrString(v, "timer");
        Py_XDECREF(timer);
        return timer;
    }

    return NULL;

}
PyObject*
semaphore_acquire(SemaphoreObject *self, PyObject *blocking, long timeout)
{
    PyObject *res = NULL;
    PyObject *current = NULL;
    PyObject *timer = NULL;
    LoopObject *loop = NULL;

    DEBUG("self:%p timeout:%ld counter:%d", self, timeout, self->counter);

    if (self->counter > 0) {
        self->counter--;
        DEBUG("counter decr self:%p", self);
        Py_RETURN_TRUE;

    } else if(PyObject_Not(blocking)) {
        Py_RETURN_FALSE;
    }

    loop = get_event_loop();
    if (loop == NULL) {
        return NULL;
    }

    current = greenlet_getcurrent();
    Py_XDECREF(current);
    if (current == NULL) {
        return NULL;
    }

    timer = loop_set_timeout(loop, timeout, NULL);
    if (timer == NULL) {
        return NULL;
    }

    if (PySet_Add(self->waiters, current) == -1) {
        ((TimerObject*)timer)->cancelled = 1; 
        return NULL;
    }

    while (self->counter <= 0) {
        res = loop_switch(loop);
        Py_XDECREF(res);
        if (res == NULL) {
            if (PyErr_ExceptionMatches(TimeoutException)) {
                RDEBUG("catch TimeoutException self:%p", self);
                if (fetch_timer() == timer) {
                    PyErr_Clear();
                    res = Py_False;
                    goto fin;
                }
            }
            goto fin;
        }
    }

    self->counter--;
    res = Py_True;

fin:
    ((TimerObject*)timer)->cancelled = 1; 
    if (PySet_Discard(self->waiters, current) == -1) {
        res = NULL;
    }
    Py_DECREF(timer);
    Py_XINCREF(res);
    return res;
}

static PyObject*
SemaphoreObject_acquire(SemaphoreObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *timeout = NULL;
    PyObject *blocking = Py_True;
    long seconds = 0;

    static char *keywords[] = {"blocking", "timeout", NULL};
    
    DEBUG("self:%p", self);

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OO:acquire", keywords, &blocking, &timeout)) {
        return NULL;
    }

    if (timeout == NULL) {
        return semaphore_acquire(self, blocking, seconds);
    } else if (timeout == Py_None) {
        return semaphore_acquire(self, blocking, seconds);
    } else if (PyLong_Check(timeout)) {
        seconds = PyLong_AsLong(timeout);
        if (seconds < 0) {
            PyErr_SetString(PyExc_ValueError, "timeout value out of range");
            return NULL;
        }
        return semaphore_acquire(self, blocking, seconds);
    }

    PyErr_SetString(PyExc_TypeError, "an integer is required");
    return NULL;
}

PyObject*
semaphore_release(SemaphoreObject *self)
{
    PyObject *m = NULL, *handle;
    LoopObject *loop;

    DEBUG("self:%p", self);
    loop = get_event_loop();
    if (loop == NULL) {
        return NULL;
    }

    self->counter++;
    if (PySet_Size(self->waiters) > 0) {
        m = PyObject_GetAttrString((PyObject *)self, "_notify_waiter");
        if (m == NULL) {
            return NULL;
        }
        handle = loop_schedule_call(loop, 0, m, NULL, NULL);
        Py_XDECREF(m);
        Py_XDECREF(handle);
        if (handle == NULL) {
            return NULL;
        }
    }

    Py_RETURN_NONE;

}

static PyObject*
SemaphoreObject_release(SemaphoreObject *self, PyObject *args)
{
    DEBUG("self:%p", self);

    return semaphore_release(self);
}

static PyObject*
SemaphoreObject_notify_waiter(SemaphoreObject *self, PyObject *args)
{
    PyObject *waiter, *res;

    if (PySet_Size(self->waiters) > 0 && self->counter >0) {
        waiter = PySet_Pop(self->waiters);
        if (waiter == NULL) {
            return NULL;
        }
        res = greenlet_switch(waiter, NULL, NULL);
        Py_XDECREF(res);
        Py_DECREF(waiter);
        if (res == NULL) {
            return NULL;
        }
    }
    Py_RETURN_NONE;
}

static PyObject*
SemaphoreObject_enter(SemaphoreObject *self, PyObject *args)
{
    return semaphore_acquire(self, Py_True, 0);
}

static PyObject*
SemaphoreObject_exit(SemaphoreObject *self, PyObject *args)
{
    return semaphore_release(self);
}

static PyMethodDef SemaphoreObject_methods[] = {
    {"acquire", (PyCFunction)SemaphoreObject_acquire, METH_VARARGS|METH_KEYWORDS, 0},
    {"release", (PyCFunction)SemaphoreObject_release, METH_NOARGS, 0},
    {"_notify_waiter", (PyCFunction)SemaphoreObject_notify_waiter, METH_NOARGS, 0},
    {"__enter__", (PyCFunction)SemaphoreObject_enter, METH_NOARGS, 0},
    {"__exit__", (PyCFunction)SemaphoreObject_exit, METH_VARARGS, 0},
    {NULL, NULL}
};

static PyMemberDef SemaphoreObject_members[] = {
    {NULL}
};

PyTypeObject SemaphoreObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME "." LOCKS_MOD_NAME ".Semaphore",             /*tp_name*/
    sizeof(SemaphoreObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)SemaphoreObject_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                          /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,        /*tp_flags*/
    "Semaphore Object",           /* tp_doc */
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    SemaphoreObject_methods,          /* tp_methods */
    SemaphoreObject_members,        /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)SemaphoreObject_init,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
    0,                           /* tp_del */
};

// --------------------------------------------------------
// BoundedSemaphore
// --------------------------------------------------------

static int
BoundedSemaphoreObject_init(BoundedSemaphoreObject *self, PyObject *args, PyObject *kwargs)
{

    int value = 1; 
    DEBUG("self:%p", self);
    static char *keywords[] = {"value", NULL};
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i:__init__", keywords, &value)) {
        return -1;
    }

    self->counter = value;
    self->init_counter = value;

    Py_CLEAR(self->waiters);

    self->waiters = PySet_New(NULL);
    if (self->waiters == NULL) {
        return -1;
    }
    DEBUG("self:%p counter:%d waiters:%p", self, self->counter, self->waiters);
    return 1;
}

static void
BoundedSemaphoreObject_dealloc(BoundedSemaphoreObject *self)
{
    GDEBUG("self %p", self);
    Py_CLEAR(self->waiters);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject*
BoundedSemaphoreObject_release(BoundedSemaphoreObject *self, PyObject *args)
{
    if (self->counter >= self->init_counter) {
        PyErr_SetString(PyExc_ValueError, "Semaphore released too many times");
        return NULL;
    }
    return semaphore_release((SemaphoreObject*)self);
}

static PyMethodDef BoundedSemaphoreObject_methods[] = {
    {"release", (PyCFunction)BoundedSemaphoreObject_release, METH_NOARGS, 0},
    {NULL, NULL}
};

static PyMemberDef BoundedSemaphoreObject_members[] = {
    {NULL}  
};

PyTypeObject BoundedSemaphoreObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME "." LOCKS_MOD_NAME ".BoundedSemaphore",             /*tp_name*/
    sizeof(BoundedSemaphoreObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)BoundedSemaphoreObject_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,        /*tp_flags*/
    "BoundedSemaphore Object",           /* tp_doc */
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    BoundedSemaphoreObject_methods,          /* tp_methods */
    BoundedSemaphoreObject_members,        /* tp_members */
    0,                         /* tp_getset */
    &SemaphoreObjectType,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)BoundedSemaphoreObject_init,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
    0,                           /* tp_del */
};

// ----------------------------------------------------------------------------
// Lock
// ----------------------------------------------------------------------------


static int
LockObject_init(LockObject *self, PyObject *args, PyObject *kwargs)
{

    GDEBUG("self:%p", self);

    self->counter = 1;

    Py_CLEAR(self->waiters);

    self->waiters = PySet_New(NULL);
    if (self->waiters == NULL) {
        return -1;
    }
    DEBUG("self:%p counter:%d waiters:%p", self, self->counter, self->waiters);
    return 1;
}

static void
LockObject_dealloc(LockObject *self)
{
    GDEBUG("self %p", self);
    Py_CLEAR(self->waiters);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyMethodDef LockObject_methods[] = {
    {NULL, NULL}
};

static PyMemberDef LockObject_members[] = {
    {NULL}  
};

PyTypeObject LockObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME "." LOCKS_MOD_NAME ".Lock",             /*tp_name*/
    sizeof(LockObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)LockObject_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,        /*tp_flags*/
    "Lock Object",           /* tp_doc */
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    LockObject_methods,          /* tp_methods */
    LockObject_members,        /* tp_members */
    0,                         /* tp_getset */
    &SemaphoreObjectType,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)LockObject_init,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
    0,                           /* tp_del */
};

// ----------------------------------------------------------------
// RLock
// ----------------------------------------------------------------


static int
RLockObject_init(RLockObject *self, PyObject *args, PyObject *kwargs)
{
    
    PyObject *s;
    DEBUG("self:%p", self);

    s = PyObject_CallFunctionObjArgs((PyObject*)&SemaphoreObjectType, NULL);
    if (s == NULL) {
        return -1;
    }
    Py_CLEAR(self->block);
    Py_CLEAR(self->owner);

    self->block = s;
    self->count = 0;
    self->owner = Py_None;
    Py_INCREF(self->owner);

    return 1;
}

static void
RLockObject_dealloc(RLockObject *self)
{
    GDEBUG("self %p", self);
    Py_CLEAR(self->block);
    Py_CLEAR(self->owner);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject*
rlock_acquire(RLockObject *self, PyObject *blocking, long timeout)
{
    PyObject *current, *res;
    int ret;


    DEBUG("self:%p", self);

    current = greenlet_getcurrent();
    Py_XDECREF(current);
    if (current == NULL) {
        return NULL;
    }

    if (self->owner == current) {
        self->count++;
        Py_RETURN_TRUE;
    }

    res = semaphore_acquire((SemaphoreObject*)self->block, blocking, timeout);
    if (res == NULL) {
        return NULL;
    }
    ret = PyObject_IsTrue(res);
    if (ret == -1) {
        return NULL;
    }
    if (ret) {
        self->owner = current;
        Py_INCREF(self->owner);
        self->count = 1;
    }
    return res;
}

static PyObject*
RLockObject_acquire(RLockObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *timeout = NULL;
    PyObject *blocking = Py_True;
    long seconds = 0;

    static char *keywords[] = {"blocking", "timeout", NULL};
    
    DEBUG("self:%p", self);
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OO:acquire", keywords, &blocking, &timeout)) {
        return NULL;
    }

    if (timeout == NULL) {
        return rlock_acquire(self, blocking, seconds);
    } else if (timeout == Py_None) {
        return rlock_acquire(self, blocking, seconds);
    } else if (PyLong_Check(timeout)) {
        seconds = PyLong_AsLong(timeout);
        if (seconds < 0) {
            PyErr_SetString(PyExc_ValueError, "timeout value out of range");
            return NULL;
        }
        return rlock_acquire(self, blocking, seconds);
    }

    PyErr_SetString(PyExc_TypeError, "an integer is required");
    return NULL;
}

static PyObject*
rlock_release(RLockObject *self)
{
    PyObject *current, *res;

    DEBUG("self:%p", self);
    current = greenlet_getcurrent();
    Py_XDECREF(current);
    if (current == NULL) {
        return NULL;
    }

    if (self->owner != current) {
        PyErr_SetString(PyExc_RuntimeError, "cannot release un-acquired lock");
        return NULL;
    }
    self->count--;
    if (self->count <= 0) {
        Py_CLEAR(self->owner);
        self->owner = Py_None;
        Py_INCREF(self->owner);
        res = semaphore_release((SemaphoreObject*)self->block);
        Py_XDECREF(res);
        if (res == NULL) {
            return NULL;
        }
    }

    Py_RETURN_NONE;
}

static PyObject*
RLockObject_release(RLockObject *self, PyObject *args)
{
    return rlock_release(self);
}

static PyObject*
RLockObject_enter(RLockObject *self, PyObject *args)
{
    return rlock_acquire(self, Py_True, 0);
}

static PyObject*
RLockObject_exit(RLockObject *self, PyObject *args)
{
    return rlock_release(self);
}

static PyObject*
rlock_acquire_restore(RLockObject *self, PyObject *args)
{
    PyObject *res, *count, *owner, *state = NULL;
    long cnt;
     
    DEBUG("self:%p", self);
    if (!PyArg_ParseTuple(args,  "O:_acquire_restore", &state)) {
        return NULL;
    }

    res = semaphore_acquire((SemaphoreObject*)self->block, Py_True, 0);
    Py_XDECREF(res);
    if (res == NULL) {
        return NULL;
    }
    
    count = PyTuple_GET_ITEM(state, 0);
    cnt = PyLong_AS_LONG(count);

    owner = PyTuple_GET_ITEM(state, 1);

    self->count = cnt;
    Py_CLEAR(self->owner);
    self->owner = owner;
    Py_INCREF(self->owner);

    Py_RETURN_NONE;
}

static PyObject*
rlock_release_save(RLockObject *self)
{
    PyObject *state, *res;

    DEBUG("self:%p", self);
    if ((state = PyTuple_New(2)) == NULL) {
        return NULL;
    }
    PyTuple_SET_ITEM(state, 0, PyLong_FromLong(self->count));
    PyTuple_SET_ITEM(state, 1, self->owner);

    self->count = 0;
    self->owner = Py_None;
    Py_INCREF(self->owner);

    res = semaphore_release((SemaphoreObject*)self->block);
    Py_XDECREF(res);
    if (res == NULL) {
        Py_DECREF(state);
        return NULL;
    }
    return state;
} 

static PyObject* 
rlock_is_owned(RLockObject *self)
{
    PyObject *current;

    DEBUG("self:%p", self);
    current = greenlet_getcurrent();
    Py_XDECREF(current);
    if (current == NULL) {
        return NULL;
    }

    if (self->owner == current) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyMethodDef RLockObject_methods[] = {
    {"acquire", (PyCFunction)RLockObject_acquire, METH_VARARGS|METH_KEYWORDS, 0},
    {"release", (PyCFunction)RLockObject_release, METH_NOARGS, 0},
    {"__enter__", (PyCFunction)RLockObject_enter, METH_NOARGS, 0},
    {"__exit__", (PyCFunction)RLockObject_exit, METH_VARARGS, 0},
    {"_acquire_restore", (PyCFunction)rlock_acquire_restore, METH_VARARGS, 0},
    {"_release_save", (PyCFunction)rlock_release_save, METH_NOARGS, 0},
    {"_is_owned", (PyCFunction)rlock_is_owned, METH_NOARGS, 0},
    {NULL, NULL}
};

static PyMemberDef RLockObject_members[] = {
    {NULL}  
};

PyTypeObject RLockObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME "." LOCKS_MOD_NAME ".RLock",             /*tp_name*/
    sizeof(RLockObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)RLockObject_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,        /*tp_flags*/
    "RLock Object",           /* tp_doc */
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    RLockObject_methods,          /* tp_methods */
    RLockObject_members,        /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)RLockObject_init,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
    0,                           /* tp_del */
};

// --------------------------------------------------------------------
// Condition
// --------------------------------------------------------------------

static int
ConditionObject_init(ConditionObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *lock = NULL, *m = NULL;

    static char *keywords[] = {"lock", NULL};
    
    DEBUG("self:%p", self);
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O:__init__", keywords, &lock)) {
        return -1;
    }

    Py_CLEAR(self->lock);

    if (lock == NULL) {
        DEBUG("create RLock self:%p", self);
        lock = PyObject_CallFunctionObjArgs((PyObject*)&RLockObjectType, NULL);
        if (lock == NULL) {
            return -1;
        }
        self->lock = lock;
    } else {
        self->lock = lock;
        Py_INCREF(self->lock);
    }
    DEBUG("self:%p lock:%p", self, self->lock);
    Py_CLEAR(self->waiters);

    self->waiters = PyList_New(0);
    GDEBUG("list:%p", self->waiters);

    if (self->waiters == NULL) {
        Py_CLEAR(self->lock);
        return -1;
    }
    
    m = PyObject_GetAttrString(lock, "acquire");
    if (m == NULL) {
        return -1;
    }
    if (PyObject_SetAttrString((PyObject*)self, "acquire", m) == -1) {
        Py_DECREF(m);
        return -1;
    }
    Py_XDECREF(m);


    m = PyObject_GetAttrString(lock, "release");
    if (m == NULL) {
        return -1;
    }
    if (PyObject_SetAttrString((PyObject*)self, "release", m) == -1) {
        Py_DECREF(m);
        return -1;
    }
    Py_XDECREF(m);

    m = PyObject_GetAttrString(lock, "_release_save");
    if (m != NULL) {
        if (PyObject_SetAttrString((PyObject*)self, "_release_save", m) == -1) {
            Py_DECREF(m);
            return -1;
        }
        DEBUG("import _release_save self:%p lock:%p", self, lock);
    }
    PyErr_Clear();
    Py_XDECREF(m);

    m = PyObject_GetAttrString(lock, "_acquire_restore");
    if (m != NULL) {
        if (PyObject_SetAttrString((PyObject*)self, "_acquire_restore", m) == -1) {
            Py_DECREF(m);
            return -1;
        }
        DEBUG("import _acquire_restore self:%p lock:%p", self, lock);
    }
    PyErr_Clear();
    Py_XDECREF(m);

    m = PyObject_GetAttrString(lock, "_is_owned");
    if (m != NULL) {
        if (PyObject_SetAttrString((PyObject*)self, "_is_owned", m) == -1) {
            Py_DECREF(m);
            return -1;
        }
        DEBUG("import _is_owned self:%p lock:%p", self, lock);
    }
    PyErr_Clear();
    Py_XDECREF(m);
    
    return 1;
}

static int
ConditionObject_clear(ConditionObject *self)
{
    DEBUG("self %p", self);
    Py_CLEAR(self->lock);
    Py_CLEAR(self->waiters);
    return 0;
}

static int
ConditionObject_traverse(ConditionObject *self, visitproc visit, void *arg)
{
    DEBUG("self:%p", self);
    Py_VISIT(self->waiters);
    Py_VISIT(self->dict);
    return 0;
}

static void
ConditionObject_dealloc(ConditionObject *self)
{
    GDEBUG("self %p", self);
    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self);
    ConditionObject_clear(self);
    Py_CLEAR(self->dict);
    Py_TYPE(self)->tp_free((PyObject*)self);
    Py_TRASHCAN_SAFE_END(self);
}

static PyObject*
ConditionObject_wait(ConditionObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *timeout = NULL;
    PyObject *res, *result, *saved_state = NULL;
    SemaphoreObject *waiter;
    long seconds = 0;
    
    static char *keywords[] = {"timeout", NULL};

    DEBUG("self:%p", self);

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O:wait", keywords, &timeout)) {
        return NULL;
    }

    if (timeout == NULL) {
        seconds = 0;
    } else if (timeout == Py_None) {
        seconds = 0;
    } else if (PyLong_Check(timeout)) {
        seconds = PyLong_AsLong(timeout);
        if (seconds < 0) {
            PyErr_SetString(PyExc_ValueError, "timeout value out of range");
            return NULL;
        }
    } else {
        PyErr_SetString(PyExc_TypeError, "an integer is required");
        return NULL;
    }


    res = call_method((PyObject*)self, "_is_owned");
    if (res == NULL) {
        return NULL;
    }
    
    if (PyObject_Not(res)) {
        Py_DECREF(res);
        PyErr_SetString(PyExc_RuntimeError, "cannot release un-acquired lock");
        return NULL;
    }
    Py_DECREF(res);
    
    waiter = (SemaphoreObject*)PyObject_CallFunctionObjArgs((PyObject*)&SemaphoreObjectType, NULL);
    if (waiter == NULL) {
        return NULL;
    }

    res = semaphore_acquire(waiter, Py_True, 0);
    Py_XDECREF(res);
    if (res == NULL) {
        return NULL;
    }

    if (PyList_Append(self->waiters, (PyObject*)waiter) == -1) {
        return NULL;
    }

    saved_state = call_method((PyObject*)self, "_release_save");
    if (saved_state == NULL) {
        Py_DECREF(waiter);
        return NULL;
    }

    res = semaphore_acquire(waiter, Py_True, seconds);
    
    result = call_method_args1((PyObject*)self, "_acquire_restore", saved_state);

    Py_DECREF(saved_state);
    if (result == NULL) {
        return NULL;
    }

    Py_DECREF(waiter);
    return res;
}

static PyObject*
condition_notify(ConditionObject *self, int num)
{
    PyObject *res, *waiters;
    PyObject *iter, *item;
    
    DEBUG("self:%p", self);
    res = call_method((PyObject*)self, "_is_owned");
    if (res == NULL) {
        return NULL;
    }
    
    if (PyObject_Not(res)) {
        Py_DECREF(res);
        PyErr_SetString(PyExc_RuntimeError, "cannot release un-acquired lock");
        return NULL;
    }
    Py_DECREF(res);
    waiters = PyList_GetSlice(self->waiters, 0, num);
    if (waiters == NULL) {
        return NULL;
    }
    if (PyObject_Not(waiters)) {
        Py_RETURN_NONE;
    }

    iter = PyObject_GetIter(waiters);
    if (PyErr_Occurred()) {
        return NULL;
    }

    while ((item =  PyIter_Next(iter))) {
        res = semaphore_release((SemaphoreObject*)item);
        Py_XDECREF(res);
        if (res == NULL) {
            Py_DECREF(item);
            goto err;
        }
        if (remove_from_list((PyListObject*)self->waiters, item) == -1) {
            Py_DECREF(item);
            goto err;
        }
        Py_DECREF(item);
        /* DEBUG("self->waiters len:%d", PyList_Size(self->waiters)); */
    }
    Py_DECREF(waiters);
    Py_DECREF(iter);
    Py_RETURN_NONE;
err:
    Py_DECREF(waiters);
    Py_DECREF(iter);
    return NULL;

}

static PyObject*
ConditionObject_notify(ConditionObject *self, PyObject *args, PyObject *kwargs)
{
    int num = 1;
    static char *keywords[] = {"n", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i:notify", keywords, &num)) {
        return NULL;
    }
    DEBUG("self:%p", self);
    //TODO check nagative value
    return condition_notify(self, num);
}

static PyObject*
ConditionObject_notify_all(ConditionObject *self, PyObject *args, PyObject *kwargs)
{
    DEBUG("self:%p", self);
    return condition_notify(self, Py_SIZE(self->waiters));
}

static PyObject*
ConditionObject_enter(ConditionObject *self, PyObject *args, PyObject *kwargs)
{
    DEBUG("self:%p", self);
    return call_method(self->lock, "__enter__");
}

static PyObject*
ConditionObject_exit(ConditionObject *self, PyObject *args, PyObject *kwargs)
{
    DEBUG("self:%p", self);
    return call_method(self->lock, "__exit__");
}

static PyObject*
ConditionObject_acquire_restore(ConditionObject *self, PyObject *args)
{
    DEBUG("self:%p", self);
    return call_method(self->lock, "acquire");
}

static PyObject*
ConditionObject_release_save(ConditionObject *self, PyObject *args)
{
    DEBUG("self:%p", self);
    return call_method(self->lock, "release");
}

static PyObject*
ConditionObject_is_owned(ConditionObject *self, PyObject *args)
{
    PyObject *res, *res2;
    DEBUG("self:%p", self);

    res = call_method_args1(self->lock, "acquire", Py_False);

    if (res == NULL) {
        return NULL;
    }
    if (PyObject_IsTrue(res)) {
        Py_DECREF(res);
        res2 = call_method(self->lock, "release");
        if (res2 == NULL) {
            return NULL;
        }
        Py_DECREF(res2);
        Py_RETURN_FALSE;
    } else {
        Py_DECREF(res);
        Py_RETURN_TRUE;
    }
}

static PyMethodDef ConditionObject_methods[] = {
    {"wait", (PyCFunction)ConditionObject_wait, METH_VARARGS|METH_KEYWORDS, 0},
    {"notify", (PyCFunction)ConditionObject_notify, METH_VARARGS|METH_KEYWORDS, 0},
    {"notify_all", (PyCFunction)ConditionObject_notify_all, METH_VARARGS|METH_KEYWORDS, 0},
    {"_acquire_restore", (PyCFunction)ConditionObject_acquire_restore, METH_VARARGS, 0},
    {"_release_save", (PyCFunction)ConditionObject_release_save, METH_NOARGS, 0},
    {"_is_owned", (PyCFunction)ConditionObject_is_owned, METH_NOARGS, 0},
    {"__enter__", (PyCFunction)ConditionObject_enter, METH_NOARGS, 0},
    {"__exit__", (PyCFunction)ConditionObject_exit, METH_VARARGS, 0},
    {NULL, NULL}
};

static PyMemberDef ConditionObject_members[] = {
    {NULL}  
};

PyTypeObject ConditionObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME "." LOCKS_MOD_NAME ".Condition",             /*tp_name*/
    sizeof(ConditionObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)ConditionObject_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    PyObject_GenericGetAttr,                         /*tp_getattro*/
    PyObject_GenericSetAttr,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,        /*tp_flags*/
    "Condition Object",           /* tp_doc */
    (traverseproc)ConditionObject_traverse,                       /* tp_traverse */
    (inquiry)ConditionObject_clear,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    ConditionObject_methods,          /* tp_methods */
    ConditionObject_members,        /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    offsetof(ConditionObject, dict),                         /* tp_dictoffset */
    (initproc)ConditionObject_init,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
    0,                           /* tp_del */
};

// ----------------------------------------------------------------
// locks module
// ----------------------------------------------------------------


static PyMethodDef LocksMod_methods[] = {
    {NULL, NULL}           /* sentinel */
};

PyObject* 
init_locks_module(PyObject *m)
{
    PyObject *d, *sd, *v;
    PyObject *sys_modules, *module;
    PyMethodDef *ml;
    

#ifdef PY3
    PyObject *mod_name = PyUnicode_FromString(MODULE_NAME "." LOCKS_MOD_NAME);
#else
    PyObject *mod_name = PyBytes_FromString(MODULE_NAME "." LOCKS_MOD_NAME);
#endif

    if(mod_name == NULL){
        return NULL;
    }

    sys_modules = PySys_GetObject("modules");
    d = PyModule_GetDict(m);
    module = PyDict_GetItem(d, mod_name);
    if(module == NULL) {
        module = PyModule_New(MODULE_NAME "." LOCKS_MOD_NAME);
        if(module != NULL) {
            PyDict_SetItem(sys_modules, mod_name, module);
            PyModule_AddObject(m, LOCKS_MOD_NAME, module);
        }
    }

    sd = PyModule_GetDict(module);
    for(ml = LocksMod_methods; ml->ml_name != NULL; ml++){
        v = PyCFunction_NewEx(ml, (PyObject *)NULL, mod_name);
        if(v == NULL) {
            goto fin;
        }
        if(PyDict_SetItemString(sd, ml->ml_name, v) != 0){
            Py_DECREF(v);
            return NULL;
        }
        Py_DECREF(v);
    }

fin:
    Py_DECREF(mod_name);

    SemaphoreObjectType.tp_new = PyType_GenericNew;
    if(PyType_Ready(&SemaphoreObjectType) < 0){
        return NULL;
    }
    BoundedSemaphoreObjectType.tp_new = PyType_GenericNew;
    if(PyType_Ready(&BoundedSemaphoreObjectType) < 0){
        return NULL;
    }
    LockObjectType.tp_new = PyType_GenericNew;
    if(PyType_Ready(&LockObjectType) < 0){
        return NULL;
    }
    RLockObjectType.tp_new = PyType_GenericNew;
    if(PyType_Ready(&RLockObjectType) < 0){
        return NULL;
    }

    ConditionObjectType.tp_new = PyType_GenericNew;
    if(PyType_Ready(&ConditionObjectType) < 0){
        return NULL;
    }
    Py_INCREF(&SemaphoreObjectType);
    PyModule_AddObject(module, "Semaphore", (PyObject *)&SemaphoreObjectType);

    Py_INCREF(&BoundedSemaphoreObjectType);
    PyModule_AddObject(module, "BoundedSemaphore", (PyObject *)&BoundedSemaphoreObjectType);
    
    Py_INCREF(&LockObjectType);
    PyModule_AddObject(module, "Lock", (PyObject *)&LockObjectType);

    Py_INCREF(&RLockObjectType);
    PyModule_AddObject(module, "RLock", (PyObject *)&RLockObjectType);

    Py_INCREF(&ConditionObjectType);
    PyModule_AddObject(module, "Condition", (PyObject *)&ConditionObjectType);
    return module;
}

