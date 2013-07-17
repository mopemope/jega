#include "event.h"
#include "locks.h"
#include "loop.h"
#include "util.h"

#define EVENT_MOD_NAME "event"

// ----------------------------------------------------------------
// Event
// ----------------------------------------------------------------

static int
EventObject_init(EventObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *lock, *cond;

    DEBUG("self:%p", self);

    DEBUG("create Lock self:%p", self);
    
    Py_CLEAR(self->cond);

    lock = PyObject_CallFunctionObjArgs((PyObject*)&LockObjectType, NULL);
    if (lock == NULL) {
        return -1;
    }

    cond = PyObject_CallFunctionObjArgs((PyObject*)&ConditionObjectType, lock, NULL);
    Py_DECREF(lock);
    if (cond == NULL) {
        return -1;
    }
    
    self->cond = cond;
    self->flag = 0;
    return 1;
}

static void
EventObject_dealloc(EventObject *self)
{
    GDEBUG("self %p", self);
    Py_CLEAR(self->cond);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject*
EventObject_is_set(EventObject *self, PyObject *args)
{
    if (self->flag) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

PyObject*
event_set(EventObject *self)
{
    PyObject *tmp, *res;

    DEBUG("self:%p", self);

    tmp = call_method(self->cond, "acquire");
    Py_XDECREF(tmp);
    if (tmp == NULL) {
        return NULL;
    }

    self->flag = 1;
    res = call_method(self->cond, "notify_all");
    Py_XDECREF(res);

    tmp = call_method(self->cond, "release");
    Py_XDECREF(tmp);

    if (res== NULL) {
        return NULL;
    }
    if (tmp == NULL) {
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject*
EventObject_set(EventObject *self, PyObject *args)
{
    DEBUG("self:%p", self);
    return event_set(self);
}

static PyObject*
EventObject_clear(EventObject *self, PyObject *args)
{
    PyObject *tmp;

    DEBUG("self:%p", self);

    tmp = call_method(self->cond, "acquire");
    Py_XDECREF(tmp);
    if (tmp == NULL) {
        return NULL;
    }
    self->flag = 0;
    tmp = call_method(self->cond, "release");
    Py_XDECREF(tmp);
    if (tmp == NULL) {
        return NULL;
    }

    Py_RETURN_NONE;
}

PyObject*
event_wait(EventObject *self, long timeout)
{
    PyObject *tmp, *res = Py_None;
    DEBUG("self:%p", self);

    tmp = call_method(self->cond, "acquire");
    Py_XDECREF(tmp);
    if (tmp == NULL) {
        return NULL;
    }

    if (!self->flag) {
        res = call_method_args1(self->cond, "wait", PyLong_FromLong(timeout));
    } else {
        Py_INCREF(res);
    }
    tmp = call_method(self->cond, "release");
    Py_XDECREF(tmp);
    if (tmp == NULL) {
        return NULL;
    }
    
    return res;
}

static PyObject*
EventObject_wait(EventObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *timeout = NULL;
    long seconds = 0;

    static char *keywords[] = {"timeout", NULL};
    
    DEBUG("self:%p", self);

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O:wait", keywords, &timeout)) {
        return NULL;
    }

    if (timeout == NULL) {
        return event_wait(self, seconds);
    } else if (timeout == Py_None) {
        return event_wait(self, seconds);
    } else if (PyLong_Check(timeout)) {
        seconds = PyLong_AsLong(timeout);
        if (seconds < 0) {
            PyErr_SetString(PyExc_ValueError, "timeout value out of range");
            return NULL;
        }
        return event_wait(self, seconds);
    }

    PyErr_SetString(PyExc_TypeError, "an integer is required");
    return NULL;
}

static PyMethodDef EventObject_methods[] = {
    {"is_set", (PyCFunction)EventObject_is_set, METH_NOARGS, 0},
    {"set", (PyCFunction)EventObject_set, METH_NOARGS, 0},
    {"clear", (PyCFunction)EventObject_clear, METH_NOARGS, 0},
    {"wait", (PyCFunction)EventObject_wait, METH_VARARGS|METH_KEYWORDS, 0},
    {NULL, NULL}
};

static PyMemberDef EventObject_members[] = {
    {NULL}
};

PyTypeObject EventObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME "." EVENT_MOD_NAME ".Event",             /*tp_name*/
    sizeof(EventObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)EventObject_dealloc, /*tp_dealloc*/
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
    "Event Object",           /* tp_doc */
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    EventObject_methods,          /* tp_methods */
    EventObject_members,        /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)EventObject_init,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
    0,                           /* tp_del */
};

// ----------------------------------------------------------------
// event module
// ----------------------------------------------------------------



static PyMethodDef EventMod_methods[] = {
    {NULL, NULL}           /* sentinel */
};

PyObject* 
init_event_module(PyObject *m)
{
    PyObject *d, *sd, *v;
    PyObject *sys_modules, *module;
    PyMethodDef *ml;
    

#ifdef PY3
    PyObject *mod_name = PyUnicode_FromString(MODULE_NAME "." EVENT_MOD_NAME);
#else
    PyObject *mod_name = PyBytes_FromString(MODULE_NAME "." EVENT_MOD_NAME);
#endif

    if(mod_name == NULL){
        return NULL;
    }

    sys_modules = PySys_GetObject("modules");
    d = PyModule_GetDict(m);
    module = PyDict_GetItem(d, mod_name);
    if(module == NULL) {
        module = PyModule_New(MODULE_NAME "." EVENT_MOD_NAME);
        if(module != NULL) {
            PyDict_SetItem(sys_modules, mod_name, module);
            PyModule_AddObject(m, EVENT_MOD_NAME, module);
        }
    }

    sd = PyModule_GetDict(module);
    for(ml = EventMod_methods; ml->ml_name != NULL; ml++){
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

    EventObjectType.tp_new = PyType_GenericNew;
    if(PyType_Ready(&EventObjectType) < 0){
        return NULL;
    }
    Py_INCREF(&EventObjectType);
    PyModule_AddObject(module, "Event", (PyObject *)&EventObjectType);

    return module;
}

