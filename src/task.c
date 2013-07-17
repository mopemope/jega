#include "task.h"
#include "greensupport.h"

int
is_active_task(TaskObject *task)
{
    return task && !task->started;
}

TaskObject*
spawn_task(PyObject *callback, PyObject *args, PyObject *kwargs, PyObject *parent)
{
    TaskObject *self;
    PyObject *greenlet = NULL;

    greenlet = greenlet_new(callback, parent);

    if (!greenlet){
        return NULL;
    }

    self = PyObject_GC_New(TaskObject, &TaskObjectType);
    if (self == NULL) {
        Py_DECREF(greenlet);
        return NULL;
    }
    GDEBUG("alloc TaskObject:%p", self);
    GDEBUG("alloc TaskObject greenlet:%p", greenlet);
    
    self->started = 0;
    self->result_value = NULL;
    self->greenlet = greenlet;

    Py_XINCREF(args);
    Py_XINCREF(kwargs);

    if(args != NULL){
        self->args = args;
    }else{
        PyObject *temp = PyTuple_New(0);
        self->args = temp;
    }
    self->kwargs = kwargs;
    self->started = 0;
    PyObject_GC_Track(self);
    
    DEBUG("self:%p", self);
    return self;
}

static PyObject*
TaskObject_start(TaskObject *self, PyObject *args)
{
    PyObject *res = NULL;
    if (self->started) {
        Py_RETURN_NONE;
    }
    res = greenlet_switch(self->greenlet, self->args, self->kwargs);
    self->started = 1;
    return res;
}

static int
TaskObject_traverse(TaskObject *self, visitproc visit, void *arg)
{
    DEBUG("self:%p", self);
    Py_VISIT(self->args);
    Py_VISIT(self->kwargs);
    Py_VISIT(self->greenlet);
    return 0;
}

static int
TaskObject_clear(TaskObject *self)
{
    DEBUG("self:%p", self);
    Py_CLEAR(self->args);
    Py_CLEAR(self->kwargs);
    Py_CLEAR(self->greenlet);
    return 0;
}

static void
TaskObject_dealloc(TaskObject *self)
{
    GDEBUG("self:%p", self);
    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self);
    TaskObject_clear(self);
    PyObject_GC_Del(self);
    Py_TRASHCAN_SAFE_END(self);
}

static PyObject *
TaskObject_cancel(TaskObject *self, PyObject *args)
{
    DEBUG("self:%p", self);
    self->started= 1;

    Py_RETURN_NONE;
}

static PyMethodDef TaskObject_methods[] = {
    {"cancel", (PyCFunction)TaskObject_cancel, METH_NOARGS, 0},
    {"start", (PyCFunction)TaskObject_start, METH_NOARGS, 0},
    {NULL, NULL}
};

/* static PyMemberDef TaskObject_members[] = { */
    /* {"start", T_BOOL, offsetof(TaskObject, started), READONLY, "Task started"}, */
    /* {NULL}  [> Sentinel <] */
/* }; */

PyTypeObject TaskObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME ".Task",             /*tp_name*/
    sizeof(TaskObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)TaskObject_dealloc, /*tp_dealloc*/
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
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC,        /*tp_flags*/
    "Task is lightweihgt process Object",           /* tp_doc */
    (traverseproc)TaskObject_traverse,                       /* tp_traverse */
    (inquiry)TaskObject_clear,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    TaskObject_methods,          /* tp_methods */
    0,        /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
    PyObject_GC_Del,                           /* tp_del */
};

