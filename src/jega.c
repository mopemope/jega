#include "jega.h"
#include "loop.h"
#include "timer.h"

/* #include "server.h" */
#include "socket.h"
#include "futures.h"
#include "task.h"
#include "executor.h"
#include "channel.h"
/* #include "gthread.h" */
/* #include "event.h" */
/* #include "gthread.h" */
/* #include "queue.h" */
/* #include "semaphore.h" */
/* #include "pool.h" */
#include "local.h"
#include "locks.h"
#include "event.h"

uint8_t loop_autostart = 1;

PyObject *TimeoutException;
PyObject *LoopAbortException;

// -------------------------------------------------------------------------------------
// Future Error
// -------------------------------------------------------------------------------------
PyObject *InvalidStateError;
PyObject *CancelledError;
PyObject *TimeoutError;
PyObject *InvalidTimeoutError;


// ---------------------------------------------------------------------------------------
// key cache
// ---------------------------------------------------------------------------------------

static PyObject* _sock_key;
static PyObject* recv_key;
static PyObject* send_key;

static PyObject*
internal_recv(PyObject *self, PyObject *args)
{
    Py_ssize_t size;
    int fd;
    PyObject *socket = NULL;
    PyObject *_socket = NULL;
    PyObject *cbargs = NULL;
    PyObject *callable, *tmp;
    LoopObject *loop;
    
    DEBUG("self:%p", self);
    socket = PyTuple_GET_ITEM(args, 0);
    DEBUG("socket:%p", socket);

    if (socket == NULL) {
        PyErr_SetString(PyExc_IOError, "");
        return NULL;
    }
    loop = (LoopObject*)PyTuple_GET_ITEM(args, 1);

    if (loop == NULL) {
        PyErr_SetString(PyExc_IOError, "");
        return NULL;
    }

    _socket = PyObject_GetAttr(socket, _sock_key);
    if (unlikely(_socket == NULL)) {
        return NULL;
    }

    fd = PyObject_AsFileDescriptor(_socket);
    if (unlikely(fd == -1)) {
        return NULL;
    }

    callable = PyObject_GetAttr(_socket, recv_key);
    if (unlikely(callable == NULL)) {
        return NULL;
    }

    size = PyTuple_GET_SIZE(args);
    if (likely(size > 2)) {
        cbargs = PyTuple_GetSlice(args, 2, size);
    }
    
    if (io_trampoline(loop, fd, PICOEV_READ, 0, NULL) == -1) {
        goto fin;
    }
    DEBUG("call recv");
    while (1) {
        tmp = PyObject_Call(callable, cbargs, NULL);
        if (tmp == NULL) {
            DEBUG("errno %d", errno);
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                //TODO check timeout value
                PyErr_Clear();
                if (io_trampoline(loop, fd, PICOEV_READ, 0, NULL) == -1) {
                    goto fin;
                }
            } else {
                PyErr_SetFromErrno(PyExc_IOError);
                goto fin;
            }
        } else {
            goto fin;
        }
    }
    
fin: 
    Py_DECREF(_socket);
    Py_DECREF(cbargs);
    Py_DECREF(callable);

    return tmp;
}

static PyObject*
internal_send(PyObject *self, PyObject *args)
{
    Py_ssize_t size;
    int fd;
    PyObject *socket = NULL;
    PyObject *_socket = NULL;
    PyObject *cbargs = NULL;
    PyObject *callable, *tmp;
    LoopObject *loop;

    DEBUG("self:%p", self);
    socket = PyTuple_GET_ITEM(args, 0);
    DEBUG("socket:%p", socket);
    if (socket == NULL) {
        PyErr_SetString(PyExc_IOError, "");
        return NULL;
    }
    loop = (LoopObject*)PyTuple_GET_ITEM(args, 1);

    if (loop == NULL) {
        PyErr_SetString(PyExc_IOError, "");
        return NULL;
    }

    _socket = PyObject_GetAttr(socket, _sock_key);
    if (unlikely(_socket == NULL)) {
        return NULL;
    }

    fd = PyObject_AsFileDescriptor(_socket);
    if (unlikely(fd == -1)) {
        return NULL;
    }

    callable = PyObject_GetAttr(_socket, send_key);
    if (unlikely(callable == NULL)) {
        return NULL;
    }

    size = PyTuple_GET_SIZE(args);
    if (likely(size > 2)) {
        cbargs = PyTuple_GetSlice(args, 2, size);
    }

    DEBUG("call send");
    while (1) {
        tmp = PyObject_Call(callable, cbargs, NULL);
        if (tmp == NULL) {
            DEBUG("errno %d", errno);
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                //TODO check timeout value
                PyErr_Clear();
                if (io_trampoline(loop, fd, PICOEV_WRITE, 0, NULL) == -1) {
                    goto fin;
                }
            } else {
                PyErr_SetFromErrno(PyExc_IOError);
                goto fin;
            }
        } else {
            goto fin;
        }
    }
fin: 
    Py_DECREF(_socket);
    Py_DECREF(cbargs);
    Py_DECREF(callable);

    return tmp;
}

static PyMethodDef CoreMethods[] = {
    {"_internal_recv", (PyCFunction)internal_recv, METH_VARARGS, 0},
    {"_internal_send", (PyCFunction)internal_send, METH_VARARGS, 0},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

#ifdef PY3
#define INITERROR return NULL

static struct PyModuleDef core_module_def = {
  PyModuleDef_HEAD_INIT,
  MODULE_NAME,
  NULL,
  -1,
  CoreMethods,
};

PyObject *
PyInit__jega(void)
#else
#define INITERROR return

PyMODINIT_FUNC
init_jega(void)
#endif
{
    PyObject *m ;

#ifdef PY3
    m = PyModule_Create(&core_module_def);
#else
    m = Py_InitModule3(MODULE_NAME, CoreMethods, "");
#endif
    if (m == NULL) {
        INITERROR;
    }


    TimeoutException = PyErr_NewException(MODULE_NAME ".TimeoutException", PyExc_IOError, NULL);
    if (TimeoutException == NULL) {
        INITERROR;
    }
    LoopAbortException = PyErr_NewException(MODULE_NAME ".LoopAbortException", PyExc_IOError, NULL);
    if (LoopAbortException == NULL) {
        INITERROR;
    }
    InvalidStateError = PyErr_NewException(MODULE_NAME ".InvalidStateError", PyExc_Exception, NULL);
    if (InvalidStateError == NULL) {
        INITERROR;
    }
    CancelledError = PyErr_NewException(MODULE_NAME ".CancelledError", PyExc_Exception, NULL);
    if (CancelledError == NULL) {
        INITERROR;
    }
    TimeoutError = PyErr_NewException(MODULE_NAME ".TimeoutError", PyExc_Exception, NULL);
    if (TimeoutError == NULL) {
        INITERROR;
    }
    InvalidTimeoutError = PyErr_NewException(MODULE_NAME ".InvalidTimeoutError", PyExc_Exception, NULL);
    if (InvalidTimeoutError == NULL) {
        INITERROR;
    }


    LoopObjectType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&LoopObjectType) < 0) {
        INITERROR;
    }

    if (PyType_Ready(&HandleObjectType) < 0) {
        INITERROR;
    }

    if (PyType_Ready(&TimerObjectType) < 0) {
        INITERROR;
    }

    FutureObjectType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&FutureObjectType) < 0) {
        INITERROR;
    }

    ExecutorObjectType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&ExecutorObjectType) < 0) {
        INITERROR;
    }

    if (PyType_Ready(&WorkerItemObjectType) < 0) {
        INITERROR;
    }
    if (PyType_Ready(&ChannelObjectType) < 0) {
        INITERROR;
    }

    Py_INCREF(&LoopObjectType);
    PyModule_AddObject(m, "AbstractEventLoop", (PyObject *)&LoopObjectType);

    Py_INCREF(&HandleObjectType);
    PyModule_AddObject(m, "Handle", (PyObject *)&HandleObjectType);
   
    Py_INCREF(&TimerObjectType);
    PyModule_AddObject(m, "Timer", (PyObject *)&TimerObjectType);
    
    /* Py_INCREF(&FutureObjectType); */
    /* PyModule_AddObject(m, "Future", (PyObject *)&FutureObjectType); */
    Py_INCREF(&FutureObjectType);
    PyModule_AddObject(m, "TaskFuture", (PyObject *)&FutureObjectType);

    Py_INCREF(&ExecutorObjectType);
    PyModule_AddObject(m, "TaskExecutor", (PyObject *)&ExecutorObjectType);
    
    Py_INCREF(&ChannelObjectType);
    PyModule_AddObject(m, "Channel", (PyObject *)&ChannelObjectType);

    Py_INCREF(TimeoutException);
    PyModule_AddObject(m, "TimeoutException", TimeoutException);
    Py_INCREF(LoopAbortException);
    PyModule_AddObject(m, "LoopAbortException", LoopAbortException);

    Py_INCREF(InvalidStateError);
    PyModule_AddObject(m, "InvalidStateError", InvalidStateError);
    Py_INCREF(CancelledError);
    PyModule_AddObject(m, "CancelledError", CancelledError);
    Py_INCREF(TimeoutError);
    PyModule_AddObject(m, "TimeoutError", TimeoutError);
    Py_INCREF(InvalidTimeoutError);
    PyModule_AddObject(m, "InvalidTimeoutError", InvalidTimeoutError);

    if (init_loop_module() < 0) {
        INITERROR;
    }
    
    if (setup_socket_mod() < 0) {
        INITERROR;
    }
    if (init_local_module(m) == NULL) {
        INITERROR;
    }
    if (init_locks_module(m) == NULL) {
        INITERROR;
    }
    if (init_event_module(m) == NULL) {
        INITERROR;
    }
    
#ifdef PY3
    _sock_key = PyUnicode_InternFromString("_sock");
    recv_key = PyUnicode_InternFromString("recv");
    send_key = PyUnicode_InternFromString("send");
#else
    _sock_key = PyBytes_FromString("_sock");
    recv_key = PyBytes_FromString("recv");
    send_key = PyBytes_FromString("send");
#endif

#ifdef PY3
  return m;
#endif
}
