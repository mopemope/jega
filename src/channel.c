#include "channel.h"
#include "greensupport.h"
#include "queue.h"
#include "timer.h"

static PyObject *notify_func = NULL;

static PyObject* get_notify_func(void);

ChannelObject*
create_channel(LoopObject *loop, uint32_t bufsize)
{
    ChannelObject *self;
    uint32_t qsize = bufsize;

    self = PyObject_GC_New(ChannelObject, &ChannelObjectType);
    if (self == NULL) {
        return NULL;
    }
    GDEBUG("alloc ChannelObject:%p", self);
    
    if (qsize == 0) {
        qsize = 1;
    }
    DEBUG("channel buf size %d", qsize);

    self->pendings = init_queue(qsize);
    if (self->pendings == NULL) {
        PyObject_GC_Del(self);
        return NULL;
    }

    self->waiters = PySet_New(NULL);
    if (self->waiters == NULL) {
        destroy_queue(self->pendings);
        PyObject_GC_Del(self);
        return NULL;
    }
    
    self->loop = loop;
    self->bufsize = bufsize;
    self->handle = NULL;

    PyObject_GC_Track(self);
    DEBUG("self:%p", self);
    return self;
}

static int
ChannelObject_traverse(ChannelObject *self, visitproc visit, void *arg)
{
    DEBUG("self:%p", self);
    Py_VISIT(self->waiters);
    return 0;
}

static int
ChannelObject_clear(ChannelObject *self)
{
    DEBUG("self:%p", self);
    Py_CLEAR(self->waiters);
    destroy_queue(self->pendings);
    return 0;
}

static void
ChannelObject_dealloc(ChannelObject *self)
{
    GDEBUG("self:%p", self);
    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self);
    ChannelObject_clear(self);
    PyObject_GC_Del(self);
    Py_TRASHCAN_SAFE_END(self);
}

static channel_msg_t*
make_chan_msg(PyObject *data) 
{
    channel_msg_t * msg = NULL;
    PyObject *current = NULL, *data_args = NULL;
    
    current = greenlet_getcurrent();
    if (current == NULL) {
        return NULL;
    }

    if ((data_args = PyTuple_New(1)) == NULL) {
        return NULL;
    }
    PyTuple_SetItem(data_args, 0, data);
    Py_INCREF(data);

    msg = (channel_msg_t*)PyMem_Malloc(sizeof(channel_msg_t));
    if (msg == NULL) {
        Py_DECREF(current);
        Py_DECREF(data_args);
        return NULL;
    }

    memset(msg, 0x0, sizeof(channel_msg_t));
    GDEBUG("alloc channel_msg_t:%p", msg);
    msg->data = data_args;
    msg->caller = current;

    return msg;
}

static void 
destroy_chan_msg(channel_msg_t *msg) 
{
    GDEBUG("dealloc channel_msg_t:%p", msg);
    if (msg == NULL) {
        return;
    }
    Py_DECREF(msg->data);
    Py_DECREF(msg->caller);
    PyMem_Free(msg);
}

static PyObject*
ChannelObject_notify(ChannelObject *self, PyObject *args)
{
    PyObject *o = NULL, *ret = NULL;
    int contains = -1;
    channel_msg_t *msg = NULL;

    self = (ChannelObject*)PyTuple_GET_ITEM(args, 0);
    DEBUG("self:%p", self);

    if (self == NULL) {
        return NULL;
    }
    
    if (self->pendings->size == 0) {
        DEBUG("pendings size zero");
        Py_RETURN_NONE;
    }

    while(1){

        if (self->pendings->size == 0 && PySet_Size(self->waiters) > 0) {
            PyErr_SetString(PyExc_ValueError, "detect racde condition");
            return NULL;
        }

        o = PySet_Pop(self->waiters);
        if(o == NULL){
            DEBUG("waiters empty");
            PyErr_Clear();
            break;
        }

        msg = queue_shift(self->pendings);

        DEBUG("waiter:%p waiter size:%d", o, (int)PySet_Size(self->waiters));

        /* if (msg == NULL) { */
            /* DEBUG("msg is NULL"); */
            /* if (o) { */
                /* if (PySet_Add(self->waiters, o) == -1) { */
                    /* Py_DECREF(o); */
                    /* return NULL; */
                /* } */
                /* Py_DECREF(o); */
            /* } */
            /* break; */
        /* } */

        DEBUG("msg:%p msg->data:%p", msg, msg->data);

        contains = PySet_Contains(self->waiters, o);
        if (contains == -1) {
            Py_DECREF(o);
            destroy_chan_msg(msg);
            return NULL;
        } else {
            ret = greenlet_switch(o, msg->data, NULL);
            Py_XDECREF(ret);
            if(PyErr_Occurred()){
                ret = loop_handle_error(self->loop, o);
                Py_XDECREF(ret);
            }
        }
        ret = greenlet_switch(msg->caller, loop_switch_value, NULL);
        Py_XDECREF(ret);
        if(PyErr_Occurred()){
            ret = loop_handle_error(self->loop, o);
            Py_XDECREF(ret);
        }
        destroy_chan_msg(msg);
        Py_XDECREF(o);
        /* GDEBUG("self:%p refs:%d", self, Py_REFCNT(self)); */
    }
    /* GDEBUG("END self:%p refs:%d", self, Py_REFCNT(self)); */
    Py_RETURN_NONE;
}

static PyMethodDef notify_func_def = {"_notify",   (PyCFunction)ChannelObject_notify, METH_VARARGS, 0};

static PyObject*
get_notify_func(void)
{
    if(notify_func == NULL){
        notify_func = PyCFunction_NewEx(&notify_func_def, (PyObject *)NULL, NULL);
    }
    Py_INCREF(notify_func);
    return notify_func;
}

static int
notify_to_loop(ChannelObject *self, PyObject *callable, PyObject *args)
{
    HandleObject *handle = NULL;

    DEBUG("self:%p", self);

    /* DEBUG("self->notify_timer %p", self->notify_timer); */
    if(self->handle == NULL){
        handle = (HandleObject*)loop_schedule_call(self->loop, 0, callable, args, NULL);
        if(handle == NULL){
            return 0;
        }
        self->handle = (PyObject*)handle;
    }else{
        handle = (HandleObject*)self->handle;
        DEBUG("notify called ? %d", handle->called);
        if(handle->called){
            // reschedule 
            Py_CLEAR(self->handle);
            handle = (HandleObject*)loop_schedule_call(self->loop, 0, callable, args, NULL);
            if(handle == NULL){
                return 0;
            }
            self->handle = (PyObject*)handle;
        }else{
            DEBUG("Not yet calll notify");
        }
    }
    return 1;
}


static PyObject* 
ChannelObject_send(ChannelObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *data = NULL, *notify_args = NULL, *res = NULL;
    int timeout = 0;
    channel_msg_t *msg = NULL;

    static char *keywords[] = {"data", "timeout", NULL};
    
    DEBUG("self:%p", self);

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|i:send", keywords, &data, &timeout)) {
        return NULL;
    }

    msg = make_chan_msg(data);
    if (msg == NULL) {
        return NULL;
    }

    if (queue_push_noext(self->pendings, msg) == -1) {
        PyErr_SetString(PyExc_ValueError, "channel buffer overflow");
        // wait or die
        RDEBUG("wait or die");
        goto err;
    }
    
    if (self->pendings->size > 0) {
        //notify
        if ((notify_args = PyTuple_New(1)) == NULL) {
            goto err;
        }
        PyTuple_SetItem(notify_args, 0, (PyObject*)self);
        Py_INCREF(self);

        if(!notify_to_loop(self, get_notify_func(), notify_args)){
            goto err;
        }
    }
    
    //check bufferd channel
    if (self->pendings->size >= self->bufsize) {
        DEBUG("buffer size :%d switch loop", self->bufsize);
        res = loop_switch(self->loop);
        Py_XDECREF(res);
    }

    Py_XDECREF(notify_args);
    Py_RETURN_NONE;
err:
    Py_XDECREF(notify_args);
    destroy_chan_msg(msg);
    return NULL;
}

static PyObject* 
ChannelObject_recv(ChannelObject *self, PyObject *args, PyObject *kwargs)
{
    
    PyObject *current = NULL;
    int timeout = 0;

    static char *keywords[] = {"timeout", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|Oi:recv", keywords, &timeout)) {
        return NULL;
    }
    
    current = greenlet_getcurrent();
    if (current == NULL) {
        return NULL;
    }

    if (PySet_Add(self->waiters, current) == -1) {
        return NULL;
    }
    Py_DECREF(current);
    
    if(start_loop(self->loop) == -1) {
        return NULL;
    }
    return loop_switch(self->loop);
}

static PyMethodDef ChannelObject_methods[] = {
    {"recv", (PyCFunction)ChannelObject_recv, METH_VARARGS|METH_KEYWORDS, 0},
    {"send", (PyCFunction)ChannelObject_send, METH_VARARGS|METH_KEYWORDS, 0},
    {NULL, NULL}
};

static PyMemberDef ChannelObject_members[] = {
    {NULL}  
};

PyTypeObject ChannelObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME ".Channel",             /*tp_name*/
    sizeof(ChannelObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)ChannelObject_dealloc, /*tp_dealloc*/
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
    "Channel ",           /* tp_doc */
    (traverseproc)ChannelObject_traverse,                       /* tp_traverse */
    (inquiry)ChannelObject_clear,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    ChannelObject_methods,          /* tp_methods */
    ChannelObject_members,        /* tp_members */
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

