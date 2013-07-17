#include "timer.h"
#include "loop.h"
#include "greensupport.h"

int
check_handle_obj(PyObject *obj)
{
    if (Py_TYPE(obj) != &HandleObjectType){
        return 0;
    }
    return 1;
}

int
check_timer_obj(PyObject *obj)
{
    if (Py_TYPE(obj) != &TimerObjectType){
        return 0;
    }
    return 1;
}

int
is_active_timer(TimerObject *timer)
{
    return timer && (!timer->called || !timer->cancelled);
}


static HandleObject*
HandleObject_new(PyObject *loop, PyObject *callback, PyObject *args, PyObject * greenlet)
{
    HandleObject *self;

    self = PyObject_GC_New(HandleObject, &HandleObjectType);
    if(self == NULL){
        return NULL;
    }
    GDEBUG("alloc HandleObject:%p", self);

    //DEBUG("args seconds:%ld callback:%p args:%p kwargs:%p", seconds, callback, args, kwargs);

    self->seconds = 0;

    Py_XINCREF(callback);
    Py_XINCREF(args);
    Py_XINCREF(greenlet);
    Py_XINCREF(loop);

    self->loop = loop;
    self->callback = callback;
    if(args != NULL){
        self->args = args;
    }else{
        PyObject *temp = PyTuple_New(0);
        self->args = temp;
    }

    self->called = 0;
    self->cancelled = 0;
    self->greenlet = greenlet;
    PyObject_GC_Track(self);
    DEBUG("self:%p", self);
    return self;
}

static TimerObject*
TimerObject_new(PyObject *loop, long seconds, PyObject *callback, PyObject *args, PyObject *greenlet)
{
    TimerObject *self;
    time_t now;

    //self = PyObject_NEW(TimerObject, &TimerObjectType);
    self = PyObject_GC_New(TimerObject, &TimerObjectType);
    if(self == NULL){
        return NULL;
    }
    GDEBUG("alloc TimerObject:%p", self);

    //DEBUG("args seconds:%ld callback:%p args:%p kwargs:%p", seconds, callback, args, kwargs);

    if(seconds > 0){
        now = time(NULL);
        self->seconds = now + seconds;
        self->interval = seconds;
    }else{
        self->seconds = 0;
        self->interval = seconds;
    }

    Py_XINCREF(loop);
    Py_XINCREF(callback);
    Py_XINCREF(args);
    Py_XINCREF(greenlet);

    self->loop = loop;
    self->callback = callback;
    if(args != NULL){
        self->args = args;
    }else{
        PyObject *temp = PyTuple_New(0);
        self->args = temp;
    }

    self->called = 0;
    self->cancelled = 0;
    self->repeat = 0;
    self->greenlet = greenlet;
    PyObject_GC_Track(self);
    DEBUG("self:%p", self);
    return self;
}

PyObject*
make_handle(PyObject *loop, long seconds, PyObject *callback, PyObject *args, PyObject *greenlet)
{
    if (seconds > 0) {
        return (PyObject*)TimerObject_new(loop, seconds, callback, args, greenlet);
    } else {
        return (PyObject*)HandleObject_new(loop, callback, args, greenlet);
    }
}

void
fire_handle(HandleObject *handle)
{
    PyObject *res = NULL;
    
    BDEBUG("HandleObject:%p callback:%p greenlet:%p", handle, handle->callback, handle->greenlet);

    if (handle->cancelled) {
        return;
    }

    if (!handle->called) {

        if (handle->greenlet) {
            BDEBUG("call handle:%p switch to greenlet:%p", handle, handle->greenlet);
            if (!greenlet_dead(handle->greenlet)) {
                res = greenlet_switch(handle->greenlet, handle->args, NULL);
            }
        } else {
            BDEBUG("call handle:%p", handle);
            res = PyEval_CallObjectWithKeywords(handle->callback, handle->args, NULL);
        }

        Py_XDECREF(res);
    }
}

void
fire_timer(TimerObject *timer)
{
    PyObject *res = NULL;
    time_t now = 0;
    
    BDEBUG("TimerObject:%p cancelled:%d called:%d", timer, timer->cancelled, timer->called);

    if (timer->cancelled) {
        return;
    }

    if (!timer->called) {
        timer->called = 1;

        if (timer->greenlet) {
            BDEBUG("call handle:%p switch to greenlet:%p", timer, timer->greenlet);
            if (!greenlet_dead(timer->greenlet)) {
                res = greenlet_switch(timer->greenlet, timer->args, NULL);
            }
        } else {
            BDEBUG("call handle:%p", timer);
            res = PyEval_CallObjectWithKeywords(timer->callback, timer->args, NULL);
        }
        timer->counter++;

        Py_XDECREF(res);

        if (timer->repeat) {
            timer->called = 0;
            timer->cancelled = 0;
            now = time(NULL);
            timer->seconds = now + timer->interval;
            res = schedule_timer((LoopObject*)timer->loop, (PyObject*)timer);            
        }
    }
}

static int
TimerObject_clear(TimerObject *self)
{
    DEBUG("self:%p", self);
    Py_CLEAR(self->args);
    Py_CLEAR(self->loop);
    Py_CLEAR(self->callback);
    Py_CLEAR(self->greenlet);
    return 0;
}

static int
TimerObject_traverse(TimerObject *self, visitproc visit, void *arg)
{
    DEBUG("self:%p", self);
    Py_VISIT(self->args);
    Py_VISIT(self->loop);
    Py_VISIT(self->callback);
    Py_VISIT(self->greenlet);
    return 0;
}

static void
TimerObject_dealloc(TimerObject *self)
{
    GDEBUG("self %p", self);
    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self);
    TimerObject_clear(self);
    PyObject_GC_Del(self);
    Py_TRASHCAN_SAFE_END(self);
}

static PyObject *
TimerObject_cancel(TimerObject *self, PyObject *args)
{
    DEBUG("self:%p", self);
    self->cancelled = 1;

    Py_RETURN_NONE;
}

// --------------------------------------------------------------------


static int
HandleObject_clear(HandleObject *self)
{
    DEBUG("self:%p", self);
    Py_CLEAR(self->args);
    Py_CLEAR(self->loop);
    Py_CLEAR(self->callback);
    Py_CLEAR(self->greenlet);
    return 0;
}

static int
HandleObject_traverse(HandleObject *self, visitproc visit, void *arg)
{
    DEBUG("self:%p", self);
    Py_VISIT(self->args);
    Py_VISIT(self->loop);
    Py_VISIT(self->callback);
    Py_VISIT(self->greenlet);
    return 0;
}

static void
HandleObject_dealloc(HandleObject *self)
{
    GDEBUG("self %p", self);
    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self);
    HandleObject_clear(self);
    PyObject_GC_Del(self);
    Py_TRASHCAN_SAFE_END(self);
}

static PyMethodDef HandleObject_methods[] = {
    {NULL, NULL}
};

static PyMemberDef HandleObject_members[] = {
    {"callback", T_OBJECT, offsetof(HandleObject, callback), READONLY, "Handle callback"},
    {"args", T_OBJECT, offsetof(HandleObject, args), READONLY, "Handle args"},
    {"cancelled", T_BOOL, offsetof(HandleObject, cancelled), READONLY, "Handle cancelled"},
    {NULL}  /* Sentinel */
};

PyTypeObject HandleObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME ".Handle",             /*tp_name*/
    sizeof(HandleObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)HandleObject_dealloc, /*tp_dealloc*/
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
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC|Py_TPFLAGS_BASETYPE,        /*tp_flags*/
    "Handle",           /* tp_doc */
    (traverseproc)HandleObject_traverse,                       /* tp_traverse */
    (inquiry)HandleObject_clear,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    HandleObject_methods,          /* tp_methods */
    HandleObject_members,        /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
    PyObject_GC_Del,                           /* tp_free*/
};


static PyMethodDef TimerObject_methods[] = {
    {"cancel", (PyCFunction)TimerObject_cancel, METH_NOARGS, 0},
    {NULL, NULL}
};

static PyMemberDef TimerObject_members[] = {
    {"callback", T_OBJECT, offsetof(TimerObject, callback), READONLY, "Timer callback"},
    {"args", T_OBJECT, offsetof(TimerObject, args), READONLY, "Timer args"},
    {"cancelled", T_BOOL, offsetof(TimerObject, cancelled), READONLY, "Timer cancelled"},
    {NULL}  /* Sentinel */
};

PyTypeObject TimerObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME ".Timer",             /*tp_name*/
    sizeof(TimerObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)TimerObject_dealloc, /*tp_dealloc*/
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
    "Timer",           /* tp_doc */
    (traverseproc)TimerObject_traverse,                       /* tp_traverse */
    (inquiry)TimerObject_clear,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    TimerObject_methods,          /* tp_methods */
    TimerObject_members,        /* tp_members */
    0,                         /* tp_getset */
    &HandleObjectType,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
    PyObject_GC_Del,                           /* tp_free*/
};

