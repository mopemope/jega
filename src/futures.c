#include "futures.h"
#include "loop.h"
#include "greensupport.h"
#include "util.h"

static int call_callbacks(FutureObject *self);

FutureObject* 
make_future(LoopObject *loop)
{
    return (FutureObject*)PyObject_CallFunctionObjArgs((PyObject*)&FutureObjectType, (PyObject*)loop, NULL);
}


static int
FutureObject_init(FutureObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *loop = NULL;

    static char *keywords[] = {"loop", NULL};
    
    DEBUG("self:%p", self);
    GDEBUG("alloc self:%p", self);
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O:__init__", keywords, &loop)) {
        return -1;
    }


    if (loop == NULL) {
        loop = (PyObject*)get_event_loop();
        if (loop == NULL) {
            return -1;
        }
    }
    Py_CLEAR(self->loop);
    Py_CLEAR(self->result);
    Py_CLEAR(self->exc_info);
    Py_CLEAR(self->waiters);
    Py_CLEAR(self->handle);
    Py_CLEAR(self->callbacks);
    Py_CLEAR(self->waiters);

    self->loop = (LoopObject*)loop;
    Py_INCREF(self->loop);
    self->state = F_PENDING;
    self->callbacks = PyList_New(0);

    if (self->callbacks == NULL) {
        return -1;
    }

    self->waiters = PySet_New(NULL);
    if (self->waiters == NULL) {
        Py_DECREF(self->loop);
        Py_DECREF(self->callbacks);
        return -1;
    }
    return 1;
}


static void
back_waiter(FutureObject* self)
{
    PyObject *res = NULL;
    PyObject *o = NULL;
    
    DEBUG("self:%p", self);
    
    if (PySet_Size(self->waiters) > 0) {
        if (self->handle) {
            BDEBUG("reschedule: self:%p handle:%p", self, self->handle);
            self->handle->called = 0;
            schedule_timer(self->loop, (PyObject*)self->handle);
        }
    }

    while(1){
        o = PySet_Pop(self->waiters);
        if (o == NULL) {
            DEBUG("waiters empty");
            PyErr_Clear();
            break;
        }
        res = greenlet_switch(o, loop_switch_value, NULL);
        Py_XDECREF(res);
    }
}

int
set_result(FutureObject *self, PyObject *result)
{
    int ret;
    DEBUG("self:%p", self);

    if (self->result) {
        Py_CLEAR(self->result);
    }
    Py_XINCREF(result);
    self->result = result;
    ret = call_callbacks(self);
    back_waiter(self);
    return ret;
}

int
set_exception(FutureObject *self, PyObject *exc_info)
{
    int ret;

    DEBUG("self:%p", self);

    if (self->exc_info) {
        Py_CLEAR(self->exc_info);
    }
    Py_XINCREF(exc_info);
    self->exc_info = exc_info;
    ret = call_callbacks(self);
    back_waiter(self);
    return ret;
}

static int
call_callbacks(FutureObject *self)
{
    int ret = 1;
    PyObject *iter = NULL, *item = NULL, *res = NULL, *args;
    LoopObject *loop = NULL;
    

    DEBUG("self:%p loop:%p", self, self->loop);
    loop = (LoopObject*)self->loop;

    iter = PyObject_GetIter(self->callbacks);
    if (PyErr_Occurred()) {
        return -1;
    }

    if ((args = PyTuple_New(1)) == NULL) {
        return -1;
    }

    PyTuple_SetItem(args, 0, (PyObject*)self);
    Py_INCREF(self);

    while ((item =  PyIter_Next(iter))) {
        res = loop_schedule_call(loop, 0, item, args, NULL);
        Py_XDECREF(res);
        if (res == NULL) {
            Py_DECREF(item);
            ret = -1;
            break;
        }
        Py_DECREF(item);
    }
    Py_XDECREF(args);
    Py_DECREF(iter);
    Py_CLEAR(self->callbacks);
    self->callbacks = PyList_New(0);

    DEBUG("end self:%p", self);
    return ret;
}

static PyObject*
FutureObject_cancel(FutureObject *self, PyObject *args)
{
    self->state = F_CANCELLED;

    if (call_callbacks(self) == -1) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject*
FutureObject_cancelled(FutureObject *self, PyObject *args)
{
    PyObject *res;

    if (self->state == F_CANCELLED) {
        res = Py_True;
    } else {
        res = Py_False;
    }
    Py_INCREF(res);
    return res;
}

static PyObject*
FutureObject_running(FutureObject *self, PyObject *args)
{
    PyObject *res;

    res = Py_False;
    Py_INCREF(res);
    return res;
}

static PyObject*
FutureObject_done(FutureObject *self, PyObject *args)
{
    PyObject *res;
    
    if (self->state == F_CANCELLED  || self->result || self->exc_info) {
        res = Py_True;
    } else {
        res = Py_False;
    }
    Py_INCREF(res);
    return res;
}

static PyObject*
FutureObject_set_result(FutureObject *self, PyObject *args)
{
    DEBUG("self:%p", self);
    if (set_result(self, args) == -1) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject*
FutureObject_set_exception(FutureObject *self, PyObject *args)
{
    DEBUG("self:%p", self);
    if (set_exception(self, args) == -1) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static int 
wait_future(FutureObject *self) 
{
    PyObject *res = NULL;
    PyObject *current = NULL;
    
    DEBUG("self:%p", self);

    if (loop_autostart) {
        current = greenlet_getcurrent();
        if (current == NULL) {
            return -1;
        }

        if (PySet_Add(self->waiters, current) == -1) {
            RDEBUG("Error???");
            return -1;
        }

        Py_DECREF(current);

        DEBUG("add waiter self:%p greenlet:%p", self, current);

        if(start_loop(self->loop) == -1) {
            return -1;
        }

        DEBUG("wait self:%p greenlet:%p", self, current);

        if (self->state == F_PENDING) {
            res = loop_switch(self->loop);
            Py_XDECREF(res);
            if (PyErr_Occurred()) {
                return -1;
            }
        } else {
            RDEBUG("NOT PENDING self:%p", self);
        }
    } else {
        PyErr_SetString(InvalidStateError, "");
        return -1;
    }
    return 1;
}


static PyObject*
FutureObject_result(FutureObject *self, PyObject *args)
{
    PyObject *res = NULL;

    DEBUG("self:%p", self);

    if (self->state == F_CANCELLED) {
        PyErr_SetString(CancelledError, "");
        return NULL;
    }
    if (self->state != F_FINISHED) {
        if (wait_future(self) == -1) {
            return NULL;
        }
    }

    if (self->exc_info) {
        RDEBUG("exception occured exc_info:%p", self->exc_info);
        raise_exception(self->exc_info);
        return NULL;
    }

    if (self->result) {
        res = self->result;
        Py_INCREF(res);
        return res;
    }
    RDEBUG("empty result self:%p", self); 
    PyErr_SetString(PyExc_ValueError, "must be set result or exception"); 
    return NULL;
}

static PyObject*
FutureObject_exception(FutureObject *self, PyObject *args)
{
    if (self->state == F_CANCELLED) {
        PyErr_SetString(CancelledError, "");
        return NULL;
    }
    if (self->state != F_FINISHED) {
        if (wait_future(self) == -1) {
            return NULL;
        }
    }

    if (self->exc_info) {
        return get_exception(self->exc_info);
    }
    Py_RETURN_NONE;
}

static PyObject*
FutureObject_add_done_callback(FutureObject *self, PyObject *callable)
{

    PyObject *res = NULL, *args = NULL;
    LoopObject *loop = NULL;

    DEBUG("self:%p", self);

    if (!callable || !PyCallable_Check(callable)) {
        PyErr_SetString(PyExc_TypeError, "must be callable");
        return NULL;
    }

    if (self->state == F_PENDING) {
        BDEBUG("append callback self:%p callable:%p", self, callable);
        if (PyList_Append(self->callbacks, callable) == -1){
            return NULL;
        }
    } else {
        if ((args = PyTuple_New(1)) == NULL) {
            return NULL;
        }
        PyTuple_SetItem(args, 0, (PyObject*)self);
        Py_INCREF(self);
        loop = (LoopObject*)self->loop;
        BDEBUG("add call_soon callback self:%p callback:%p", self, callable);
        res = loop_schedule_call(loop, 0, callable, NULL, NULL);
        Py_DECREF(args);
        if (res == NULL) {
            return NULL;
        }
    }

    Py_RETURN_NONE;
}

static PyObject*
FutureObject_remove_done_callback(FutureObject *self, PyObject *callable)
{

    Py_ssize_t i;
    int ret;

    DEBUG("self:%p", self);

    if (!callable || !PyCallable_Check(callable)) {
        PyErr_SetString(PyExc_TypeError, "must be callable");
        return NULL;
    }

    while (1) {
        i = PySequence_Index(self->callbacks, callable);
        if (i == -1) {
            // not in list
            PyErr_Clear();
            Py_RETURN_NONE;
        }
        ret = PySequence_DelItem(self->callbacks, i);
        if (ret == -1) {
            return NULL;
        }
    }
    Py_RETURN_NONE;
}

// --------------------------------------------------------------------
// Object Protocol
// --------------------------------------------------------------------

static int
FutureObject_clear(FutureObject *self)
{
    DEBUG("self %p", self);
    Py_CLEAR(self->result);
    Py_CLEAR(self->exc_info);
    Py_CLEAR(self->callbacks);
    Py_CLEAR(self->waiters);
    Py_CLEAR(self->handle);
    Py_CLEAR(self->loop);
    return 0;
}

static int
FutureObject_traverse(FutureObject *self, visitproc visit, void *arg)
{
    DEBUG("self:%p", self);
    Py_VISIT(self->result);
    Py_VISIT(self->exc_info);
    Py_VISIT(self->callbacks);
    Py_VISIT(self->waiters);
    Py_VISIT(self->handle);
    Py_VISIT(self->loop);
    return 0;
}

static void
FutureObject_dealloc(FutureObject *self)
{
    GDEBUG("self %p", self);
    FutureObject_clear(self);
    PyObject_GC_UnTrack(self);
    Py_CLEAR(self->dict);
    Py_TYPE(self)->tp_free((PyObject*)self);
    
}


/* static PyMethodDef FutureObject_methods[] = { */
    /* {NULL, NULL} */
/* }; */

/* static PyMemberDef FutureObject_members[] = { */
    /* {NULL}  [> Sentinel <] */
/* }; */

/* PyTypeObject FutureObjectType = { */
/* #ifdef PY3 */
    /* PyVarObject_HEAD_INIT(NULL, 0) */
/* #else */
    /* PyObject_HEAD_INIT(NULL) */
    /* 0,                    [> ob_size <] */
/* #endif */
    /* MODULE_NAME ".Future",             [>tp_name<] */
    /* sizeof(FutureObject), [>tp_basicsize<] */
    /* 0,                         [>tp_itemsize<] */
    /* 0,                         [>tp_dealloc<] */
    /* 0,                         [>tp_print<] */
    /* 0,                         [>tp_getattr<] */
    /* 0,                         [>tp_setattr<] */
    /* 0,                         [>tp_compare<] */
    /* 0,                         [>tp_repr<] */
    /* 0,                         [>tp_as_number<] */
    /* 0,                         [>tp_as_sequence<] */
    /* 0,                          [>tp_as_mapping<] */
    /* 0,                         [>tp_hash <] */
    /* 0,                         [>tp_call<] */
    /* 0,                         [>tp_str<] */
    /* 0,                         [>tp_getattro<] */
    /* 0,                         [>tp_setattro<] */
    /* 0,                         [>tp_as_buffer<] */
    /* Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,        [>tp_flags<] */
    /* "Future",           [> tp_doc <] */
    /* 0,                        [> tp_traverse <] */
    /* 0,                       [> tp_clear <] */
    /* 0,                       [> tp_richcompare <] */
    /* 0,                       [> tp_weaklistoffset <] */
    /* 0,                       [> tp_iter <] */
    /* 0,                       [> tp_iternext <] */
    /* FutureObject_methods,          [> tp_methods <] */
    /* FutureObject_members,        [> tp_members <] */
    /* 0,                         [> tp_getset <] */
    /* 0,                         [> tp_base <] */
    /* 0,                         [> tp_dict <] */
    /* 0,                         [> tp_descr_get <] */
    /* 0,                         [> tp_descr_set <] */
    /* 0,                         [> tp_dictoffset <] */
    /* 0,                      [> tp_init <] */
    /* PyType_GenericAlloc,                         [> tp_alloc <] */
    /* 0,                           [> tp_new <] */
    /* 0,                           [> tp_del <] */
/* }; */


static PyMethodDef FutureObject_methods[] = {
    {"cancel", (PyCFunction)FutureObject_cancel, METH_NOARGS, 0},
    {"cancelled", (PyCFunction)FutureObject_cancelled, METH_NOARGS, 0},
    {"running", (PyCFunction)FutureObject_running, METH_NOARGS, 0},
    {"done", (PyCFunction)FutureObject_done, METH_NOARGS, 0},
    {"set_result", (PyCFunction)FutureObject_set_result, METH_O, 0},
    {"set_exception", (PyCFunction)FutureObject_set_exception, METH_O, 0},
    {"result", (PyCFunction)FutureObject_result, METH_NOARGS, 0},
    {"exception", (PyCFunction)FutureObject_exception, METH_NOARGS, 0},
    {"add_done_callback", (PyCFunction)FutureObject_add_done_callback, METH_O, 0},
    {"remove_done_callback", (PyCFunction)FutureObject_remove_done_callback, METH_O, 0},
    {NULL, NULL}
};

static PyMemberDef FutureObject_members[] = {
    {NULL}  /* Sentinel */
};

PyTypeObject FutureObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME ".TaskFuture",             /*tp_name*/
    sizeof(FutureObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)FutureObject_dealloc,  /*tp_dealloc*/
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
    PyObject_GenericGetAttr,                         /*tp_getattro*/
    PyObject_GenericSetAttr,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,        /*tp_flags*/
    "TaskFuture",           /* tp_doc */
    (traverseproc)FutureObject_traverse,                        /* tp_traverse */
    (inquiry)FutureObject_clear,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    FutureObject_methods,          /* tp_methods */
    FutureObject_members,        /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    offsetof(FutureObject, dict),                         /* tp_dictoffset */
    (initproc)FutureObject_init,                      /* tp_init */
    0,                         /* tp_alloc */
    PyType_GenericNew,                           /* tp_new */
    0,                           /* tp_free */
};


