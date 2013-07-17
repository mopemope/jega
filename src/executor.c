#include "executor.h"
#include "greensupport.h"
#include "local.h"
#include "timer.h"
#include "util.h"

// -------------------------------------------
// Worker Item
// -------------------------------------------

static WorkerItemObject*
make_workeritem(LoopObject *loop, FutureObject *future, PyObject *fn, PyObject *args, PyObject *kwargs)
{
    WorkerItemObject *self;

    self = PyObject_GC_New(WorkerItemObject, &WorkerItemObjectType);
    if(self == NULL){
        return NULL;
    }
    GDEBUG("alloc WorkerItemObject:%p", self);

    Py_XINCREF(loop);
    Py_XINCREF(future);
    Py_XINCREF(fn);
    Py_XINCREF(args);
    Py_XINCREF(kwargs);

    self->args = args;
    self->kwargs = kwargs;
    self->loop = loop;
    self->future = future;
    self->fn = fn;
    
    return self;
}

static int
WorkerItemObject_clear(WorkerItemObject *self)
{
    DEBUG("self %p", self);
    Py_CLEAR(self->loop);
    Py_CLEAR(self->future);
    Py_CLEAR(self->fn);
    Py_CLEAR(self->args);
    Py_CLEAR(self->kwargs);
    return 0;
}

static int
WorkerItemObject_traverse(WorkerItemObject *self, visitproc visit, void *arg)
{
    DEBUG("self:%p", self);
    Py_VISIT(self->loop);
    Py_VISIT(self->future);
    Py_VISIT(self->fn);
    Py_VISIT(self->args);
    Py_VISIT(self->kwargs);
    return 0;
}

static void
WorkerItemObject_dealloc(WorkerItemObject *self)
{
    GDEBUG("self %p", self);
    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self);
    WorkerItemObject_clear(self);
    Py_TYPE(self)->tp_free((PyObject*)self);
    Py_TRASHCAN_SAFE_END(self);
}

static PyObject* 
run_worker(WorkerItemObject *self)
{
    PyObject *res, *exc_info = NULL;
    FutureObject *gfuture = NULL;
    
    DEBUG("item:%p", self);
    gfuture = (FutureObject*)self->future;
    
    if (gfuture->state == F_CANCELLED) {
        Py_RETURN_NONE;
    }

    if (gfuture->result || gfuture->exc_info) {
        Py_RETURN_NONE;
    }
    
    BDEBUG("running item:%p", self);

    res = PyEval_CallObjectWithKeywords(self->fn, self->args, self->kwargs);

    Py_XDECREF(res);

    gfuture->state = F_FINISHED;

    BDEBUG("finishd item:%p", self);

    if (res == NULL) {
        RDEBUG("Error Occured self:%p", self);
        /* PyErr_Print(); */
        exc_info = save_exception();

        if (set_exception(gfuture, exc_info) == -1) {
            return NULL;
        }
    } else {
        if (set_result(gfuture, res) == -1) {
            return NULL;
        }
    }

    DEBUG("END item:%p", self);
    Py_RETURN_NONE;
}

// --------------------------------------------------------------------
// Executor Object Protocol
// --------------------------------------------------------------------

static int
ExecutorObject_init(ExecutorObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *loop = NULL;
    int max_workers = 0; 
    DEBUG("self:%p", self);
    static char *keywords[] = {"event_loop", "max_workers", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|i:__init__", keywords, &loop, &max_workers)) {
        return -1;
    }
    if (PyObject_IsInstance(loop, (PyObject*)&LoopObjectType) == 0) {
        PyErr_SetString(PyExc_TypeError, "must be loop object");
        return -1;
    }

    Py_CLEAR(self->weakreflist);


    self->pendings = init_queue(32);
    if (self->pendings == NULL) {
        return -1;
    }

    /* self->workers= init_queue(max_workers + 1); */
    /* if (self->workers == NULL) { */
        /* destroy_queue(self->pendings); */
        /* return -1; */
    /* } */
    
    Py_CLEAR(self->loop);

    self->loop = (LoopObject*)loop;
    Py_INCREF(self->loop);

    self->max_workers = max_workers;
    self->run_workers = 0;
    self->shutdown = 0;
    return 1;
}

static int
ExecutorObject_clear(ExecutorObject *self)
{
    PyObject *item = NULL;
    /* HandleObject *handle = NULL; */

    DEBUG("self %p", self);

    DEBUG("start clear worker_item pendings:%p", self->pendings);
    while ((item = (PyObject*)queue_shift(self->pendings)) != NULL) {
        GDEBUG("drop worker_item:%p", item);
        Py_CLEAR(item);
    }

    /* DEBUG("start clear threads Workers:%p", self->workers); */
    /* while ((handle = (HandleObject*)queue_shift(self->workers)) != NULL) { */
        /* GDEBUG("drop handle:%p greenlet:%p", handle, handle->greenlet); */
        /* Py_DECREF(handle->greenlet); */
        /* Py_DECREF(handle); */
    /* } */
    destroy_queue(self->pendings);
    /* destroy_queue(self->workers); */
    self->pendings = NULL;
    /* self->workers = NULL; */
    Py_CLEAR(self->loop);
    DEBUG("END self %p", self);
    return 0;
}

static int
ExecutorObject_traverse(ExecutorObject *self, visitproc visit, void *arg)
{
    DEBUG("self:%p", self);
    Py_VISIT(self->loop);
    return 0;
}

static void
ExecutorObject_dealloc(ExecutorObject *self)
{
    GDEBUG("self %p", self);
    self->shutdown = 1;
    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self);
    if (self->weakreflist != NULL){
        PyObject_ClearWeakRefs((PyObject *)self);
    }
    ExecutorObject_clear(self);
    Py_TYPE(self)->tp_free((PyObject*)self);
    Py_TRASHCAN_SAFE_END(self);
    /* GDEBUG("END self %p", self); */
}

// ------------------------------------------------------------------------
// Methods
// ------------------------------------------------------------------------

static int
check_shutdown(ExecutorObject *self)
{
    return 1;
}

static ExecutorObject*
get_executor_from_ref(PyObject *weakref)
{
    PyObject *res;

    res = PyWeakref_GetObject(weakref);
    if (res == Py_None) {
        // executor is dead
        return NULL;
    }
    return (ExecutorObject*)res;
}

/* static int  */
/* next_sched(PyObject *weakref, PyObject *greenlet)  */
/* { */
    /* ExecutorObject *self; */
    /* PyObject *handle, *res; */

    /* if ((self = (ExecutorObject*)get_executor_from_ref(weakref)) == NULL) { */
        /* return -1; */
    /* } */

    /* handle = loop_schedule_call(self->loop, 0, NULL, NULL, greenlet); */
    /* Py_XDECREF(handle); */

    /* if (handle == NULL) { */
        /* return -1; */
    /* } */
    /* res = loop_switch(self->loop); */
    /* Py_XDECREF(res); */
    /* if (res == NULL) { */
        /* return -1; */
    /* } */

    /* return 1; */
/* } */

static PyObject*
_worker(PyObject *n, PyObject *args)
{
    WorkerItemObject *worker_item = NULL;
    FutureObject *future = NULL;
    PyObject *res, *greenlet;
    HandleObject *handle = NULL;
    PyObject *dict = NULL;
    PyObject *weakref = NULL;
    ExecutorObject *self = NULL;
    int max_workers = 0;

    if (!PyArg_ParseTuple(args,  "O:_worker", &weakref)) {
        return NULL;
    }

    DEBUG("weakref:%p", weakref);

    greenlet = greenlet_getcurrent();
    Py_XDECREF(greenlet);

    dict = get_thread_local(greenlet);
    Py_XDECREF(dict);
    if (dict == NULL) {
        return NULL;
    }

    handle = (HandleObject*)PyDict_GetItemString(dict, "handle");
    if (handle == NULL) {
        return NULL;
    }
    PyDict_DelItemString(dict, "handle");
    
    BDEBUG("start worker greenlet:%p handle:%p", greenlet, handle);

    self = get_executor_from_ref(weakref);
    if (!self) {
        goto fin;
    }
    greenlet_setparent(greenlet, self->loop->greenlet);
    max_workers = self->max_workers;

    while (1) {
        self = get_executor_from_ref(weakref);
        if (!self) {
            goto fin;
        }

        if (check_interrupted(self->loop) || self->shutdown) {
            goto fin;
        }

        worker_item = (WorkerItemObject*)queue_shift(self->pendings);

        DEBUG("start run worker greenlet:%p item:%p", greenlet, worker_item);

        if (worker_item == NULL) {
            // switch loop
            res = loop_switch(self->loop);
            Py_XDECREF(res);
            continue;
        }

        future = worker_item->future;
        if (future->state != F_PENDING) {
            Py_DECREF(worker_item);
            continue;
        }
        future->handle = handle;
        Py_INCREF(future->handle);

        res = run_worker(worker_item);
        Py_XDECREF(res);
        Py_DECREF(worker_item);

        if (PyErr_Occurred()) {
            goto err;
        }
        DEBUG("end run worker greenlet:%p item:%p", greenlet, worker_item);

        self = get_executor_from_ref(weakref);
        if (!self) {
            goto fin;
        }
        if (check_interrupted(self->loop) || self->shutdown) {
            goto fin;
        }
        
        if (max_workers == 0) {
            break;
        }
        handle->called = 0;
        schedule_timer(self->loop, (PyObject*)handle);

    }

fin:
    BDEBUG("stop worker greenlet:%p handle:%p", greenlet, handle);
    Py_XDECREF(handle);
    Py_RETURN_NONE;
err:
    RDEBUG("stop worker by error greenlet:%p", greenlet);
    Py_XDECREF(handle);
    return NULL;

}

static PyObject* worker_func = NULL;

static PyMethodDef worker_func_def = {"_worker",   (PyCFunction)_worker, METH_VARARGS, 0};

static PyObject*
get_worker_func(void)
{
    if(worker_func == NULL){
        worker_func = PyCFunction_NewEx(&worker_func_def, (PyObject *)NULL, NULL);
    }
    Py_INCREF(worker_func);
    return worker_func;
}

static int
adjust_thread_count(ExecutorObject *self, FutureObject *future)
{
    PyObject *greenlet = NULL, *tmp = NULL, *args = NULL;
    PyObject *handle, *dict;
    PyObject* weakref = NULL;

    DEBUG("self:%p", self);

    if (self->max_workers == 0 || self->run_workers < self->max_workers) {
        
        tmp = get_worker_func();
        if (tmp == NULL) {
            return -1;
        }

        greenlet = greenlet_new(tmp, self->loop->greenlet);
        Py_DECREF(tmp);
        if (greenlet == NULL) {
            return -1;
        }

        dict = get_thread_local(greenlet);
        Py_XDECREF(dict);
        if (dict == NULL) {
            return -1;
        }
        
        if ((args = PyTuple_New(1)) == NULL) {
            Py_DECREF(greenlet);
            return -1;
        }

        weakref = PyWeakref_NewRef((PyObject*)self, NULL);
        if (!weakref) {
            Py_DECREF(greenlet);
            return -1;
        }
        
        PyTuple_SET_ITEM(args, 0, weakref);

        //scheduled
        handle = loop_schedule_call(self->loop, 0, NULL, args, greenlet);

        if (handle == NULL) {
            Py_DECREF(greenlet);
            Py_DECREF(weakref);
            return -1;
        }

        if (PyDict_SetItemString(dict, "handle", handle) == -1) {
            Py_DECREF(greenlet);
            Py_DECREF(weakref);
            return -1;
        }

        /* if (queue_push(self->workers, handle) == -1) { */
            /* Py_DECREF(greenlet); */
            /* Py_DECREF(weakref); */
            
            /* return -1; */
        /* } */
        self->run_workers++;
    }

    DEBUG("END self:%p", self);
    return 1;
}

static PyObject*
internal_submit(ExecutorObject *self, PyObject *cb, PyObject *cbargs, PyObject *kwargs)
{
    WorkerItemObject *workeritem = NULL;
    FutureObject *future = NULL;

    future = make_future(self->loop);
    if (future == NULL) {
        goto err;
    }

    workeritem = make_workeritem(self->loop ,future, cb, cbargs, kwargs);
    if (workeritem == NULL) {
        goto err;
    }

    if (queue_push(self->pendings, workeritem) == -1) {
        goto err;
    }
    
    if (adjust_thread_count(self, future) == -1) {
        goto err;
    }

    return (PyObject*)future;

err:
    Py_XDECREF(future);
    Py_XDECREF(workeritem);
    return NULL;
}

PyObject*
executor_submit(ExecutorObject *self, PyObject *cb, PyObject *cbargs, PyObject *kwargs)
{
    return internal_submit(self, cb, cbargs, kwargs);
}

static PyObject*
ExecutorObject_submit(ExecutorObject *self, PyObject *args, PyObject *kwargs)
{
    Py_ssize_t size;
    PyObject *cb = NULL, *cbargs = NULL, *res = NULL;

    DEBUG("self:%p args:%p kwargs:%p", self, args, kwargs);
    size = PyTuple_GET_SIZE(args);
    DEBUG("args size %d", (int)size);

    if (size < 1) {
        PyErr_SetString(PyExc_TypeError, "submit takes exactly 1 argument");
        return NULL;
    }

    cb = PyTuple_GET_ITEM(args, 0);

    if (!PyCallable_Check(cb)) {
        PyErr_SetString(PyExc_TypeError, "must be callable");
        return NULL;
    }

    if (size > 1) {
        cbargs = PyTuple_GetSlice(args, 1, size);
    }

    res = internal_submit(self, cb, cbargs, kwargs);
    Py_XDECREF(cbargs);

    return res;
}


static PyMethodDef ExecutorObject_methods[] = {
    {"submit", (PyCFunction)ExecutorObject_submit, METH_VARARGS|METH_KEYWORDS, 0},
    {NULL, NULL}
};

static PyMemberDef ExecutorObject_members[] = {
    {NULL}  /* Sentinel */
};

PyTypeObject ExecutorObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME ".TaskExecutor",             /*tp_name*/
    sizeof(ExecutorObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)ExecutorObject_dealloc,  /*tp_dealloc*/
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
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,        /*tp_flags*/
    "TaskExecutor",           /* tp_doc */
    (traverseproc)ExecutorObject_traverse,                        /* tp_traverse */
    (inquiry)ExecutorObject_clear,                       /* tp_clear */
    0,                       /* tp_richcompare */
    offsetof(ExecutorObject, weakreflist),          /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    ExecutorObject_methods,          /* tp_methods */
    ExecutorObject_members,        /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)ExecutorObject_init,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
    0,                           /* tp_free */
};


static PyMethodDef WorkerItemObject_methods[] = {
    /* {"_run", (PyCFunction)WorkerItemObject_run, METH_NOARGS, 0}, */
    {NULL, NULL}
};

static PyMemberDef WorkerItemObject_members[] = {
    {NULL}  /* Sentinel */
};

PyTypeObject WorkerItemObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME "._WorkerItem",             /*tp_name*/
    sizeof(WorkerItemObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)WorkerItemObject_dealloc,  /*tp_dealloc*/
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
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,        /*tp_flags*/
    "_WorkerItem",           /* tp_doc */
    (traverseproc)WorkerItemObject_traverse,                        /* tp_traverse */
    (inquiry)WorkerItemObject_clear,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    WorkerItemObject_methods,          /* tp_methods */
    WorkerItemObject_members,        /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
    PyObject_GC_Del,                           /* tp_free */
};


