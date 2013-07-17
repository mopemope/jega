#include "loop.h"
#include "timer.h"
#include "heapq.h"
#include "util.h"
#include "socket.h"
#include "channel.h"
#include "executor.h"
#include "greensupport.h"
#include "lookup.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>

PyObject *loop_switch_value = NULL;

//loop start flag
static volatile sig_atomic_t catch_signal = 0;

static PyObject *system_errors = NULL;

static PyObject* event_getter = NULL;
//special switch value

static PyObject* internal_loop_schedule_call(LoopObject *self, long seconds, PyObject *cb, PyObject *args, PyObject *greenlet);

static int run_loop(LoopObject *loop);

static void destroy_callback_arg(callback_arg_t *carg);


static void
sigint_cb(int signum)
{
    DEBUG("call SIGINT");
    if (!catch_signal) {
        catch_signal = signum;
    }
}

static void
sigpipe_cb(int signum)
{
}

void
reset_all(LoopObject *loop)
{
    //Py_XDECREF(loop);
    //loop = NULL;
    //TODO error check
    /* destroy_queue(g_timers); */
    /* g_timers = init_queue(); */
    /* destroy_pendings(); */
    /* init_pendings(); */
    /* (void)reset_channel(loop); */
    //set sig again
    PyOS_setsig(SIGPIPE, sigpipe_cb);
    PyOS_setsig(SIGINT, sigint_cb);
    PyOS_setsig(SIGTERM, sigint_cb);
    /* reset_main_loop(); */

}

static int 
check_stop_signal(LoopObject *self) 
{

    if (unlikely(self->state == L_STOP)) {
        RDEBUG("LOOP STOPPED");
        return 1;
    }

    if (unlikely(catch_signal != 0)) {

        // TODO only SIGINT ?
        if (catch_signal == SIGINT) {
            self->state = L_STOP;
            catch_signal = 0;
            RDEBUG("CATCH SIGNINT");
            // catch SIGINT
            return 1;
        }
    }
    return 0;
}

static int
fire_pendings(LoopObject *self)
{
    PyObject *item = NULL;
    HandleObject *handle = NULL;
    TimerObject *timer = NULL;

    queue_t *pendings = self->pendings;
   
    while (likely(self->state != L_STOP && (item = queue_shift(pendings)) != NULL)) {

        if (likely(check_handle_obj(item))) {
            handle = (HandleObject*)item;
            DEBUG("start handle:%p activecnt:%d", handle, self->activecnt);
            if (!handle->cancelled) {
                fire_handle(handle);
            }
            DEBUG("fin handle:%p activecnt:%d", handle, self->activecnt);

            Py_DECREF(item);
            self->activecnt--;
        } else {
            timer = (TimerObject*)item;
            DEBUG("start timer:%p activecnt:%d", timer, self->activecnt);
            if (!timer->cancelled) {
                fire_timer(timer);
            }
            DEBUG("fin timer :%p activecnt:%d", timer, self->activecnt);
            Py_DECREF(item);
            self->activecnt--;
        }

        if (unlikely(PyErr_Occurred() != NULL)) {
            RDEBUG("pending call raise exception");
            /* PyErr_Print(); */
            return -1;
        }
        if (unlikely(check_stop_signal(self))) {
            PyErr_SetNone(PyExc_KeyboardInterrupt);
            return -1;
        }
    }
    return 1;
}

static  int
fire_timers(LoopObject *self)
{
    TimerObject *timer = NULL;
    int ret = 1;
    heapq_t *q = self->scheduled;
    time_t now = time(NULL);
    queue_t *pendings = self->pendings;
    /* while (q->size > 0 && self->running && self->activecnt > 0) { */
    while (likely(q->size > 0 && self->state == L_RUNNING)) {

        timer = q->heap[0];
        if (timer->cancelled) {
            timer = heappop(q);
            Py_DECREF(timer);
            self->activecnt--;
            continue;
        }

        if (timer->seconds <= now) {
            timer = heappop(q);
            //call
            if (unlikely(queue_push(pendings, (void *)timer) == -1)) {
                Py_DECREF(timer);
                RDEBUG("fail push");
                return -1;
            }

            /* DEBUG("start timer:%p activecnt:%d", timer, self->activecnt); */
            /* fire_timer(timer); */
            /* Py_DECREF(timer); */
            /* self->activecnt--; */
            /* DEBUG("fin timer:%p activecnt:%d", timer, self->activecnt); */

            /* if (PyErr_Occurred()) { */
                /* RDEBUG("scheduled call raise exception"); */
                /* [> PyErr_Print(); <] */
                /* return -1; */
            /* } */
        } else {
            break;
        }
    }
    return ret;

}

static void
cb_arg_clear_handler(void* cb_arg)
{
    DEBUG("call :%p", cb_arg);
    
    destroy_callback_arg((callback_arg_t*)cb_arg);
}

static void
init_main_loop(LoopObject *self)
{
    if (self && self->main_loop == NULL) {
        //DEBUG("init_main_loop");
        DEBUG("self:%p" ,self);
        picoev_init(self->maxfd, cb_arg_clear_handler);
        self->main_loop = picoev_create_loop(60);
        self->main_loop->now = time(NULL);
    }
}

static void
destroy_main_loop(LoopObject *self)
{
    if (self && self->main_loop != NULL) {
        DEBUG("self %p" ,self);
        picoev_destroy_loop(self->main_loop);
        picoev_deinit();
        self->main_loop = NULL;
        self->ioactivecnt = 0;
    }
}

static int 
reset_loop(LoopObject *loop)
{
    PyObject *res = NULL;

    if (loop != NULL) {
        destroy_main_loop(loop);
        init_main_loop(loop);
        res = call_method((PyObject*)loop, "reset");
        if (res == NULL) {
            return -1;
        }
        PyOS_setsig(SIGPIPE, sigpipe_cb);
        PyOS_setsig(SIGINT, sigint_cb);
        PyOS_setsig(SIGTERM, sigint_cb);
    }
    return 1;

}

static int
call_fork_loop(LoopObject *loop)
{
    DEBUG("self:%p", loop);
    if (unlikely(loop->postfork)) {
        YDEBUG("call fork(2) reset loop");
        loop->postfork = 0;
        return reset_loop(loop);
    }
    return 1;
}

LoopObject*
get_event_loop(void)
{
    PyObject *f, *m, *md, *sys_modules;
    PyObject *res = NULL;
    
    if (unlikely(event_getter == NULL)) {
        sys_modules = PySys_GetObject("modules");
        m = PyDict_GetItemString(sys_modules, "jega");
        if (m == NULL) {
            m = PyImport_ImportModule("jega");
            if (m == NULL) {
                return NULL;
            }
        }
        md = PyModule_GetDict(m);
        if (!md) {
            return NULL;
        }
        f = PyDict_GetItemString(md, "get_event_loop");
        if (f == NULL) {
            return NULL;
        }
        event_getter = f;
    }

    /* BDEBUG("event_getter:%p", event_getter); */
    res = PyObject_CallFunctionObjArgs(event_getter, NULL);
    DEBUG("get_event_loop:%p", res);
    if (res == NULL) {
        return NULL;
    }

    if (PyObject_IsInstance(res, (PyObject*)&LoopObjectType) == 0) {
        PyErr_SetString(PyExc_TypeError, "must be loop object");
        Py_XDECREF(res);
        return NULL;
    }
    
    return (LoopObject*)res;
}


/* static PyObject* */
/* loop_run(LoopObject *self, PyObject *args) */
/* { */
    /* PyObject *ret = NULL; */
    /* DEBUG("self:%p", self); */
    /* DEBUG("start loop run"); */

    /* while (1) { */

        /* if (loop_done == 1) { */
            /* DEBUG("already running"); */
            /* Py_RETURN_NONE; */
        /* } */

        /* if (internal_loop(self) < 0) { */
            /* return NULL; */
        /* } */
        
        /* if (catch_signal) { */
            /* //override */
            /* PyErr_Clear(); */
            /* PyErr_SetNone(PyExc_KeyboardInterrupt); */
            /* catch_signal = 0; */
            /* return NULL; */
        /* } */
        /* YDEBUG("loop stopped loop_done:%d", loop_done); */
        /* PyErr_SetString(LoopAbortException, "loop block forever"); */
        /* ret = greenlet_throw_err(greenlet_getparent(self->greenlet)); */
        /* Py_XDECREF(ret); */
    /* } */
    /* Py_RETURN_NONE; */
/* } */

/* PyObject* */
/* loop_abort(void) */
/* { */
    /* LoopObject *self = get_event_loop(); */
    /* DEBUG("self:%p", self); */

    /* if (loop_done == 1) { */
        /* stopped = 1; */
        /* loop_done = 0; */
        /* DEBUG("loop_done = 0"); */
        /* if (greenlet_started(self->greenlet)) { */
            /* //if (!PyErr_Occurred()) { */
                /* //PyErr_SetString(PyExc_IOError, "loop aborted"); */
            /* //} */
            /* [> return loop_switch(); <] */
        /* } */
    /* } */
    /* Py_RETURN_NONE; */
/* } */

/* static PyObject* */
/* LoopObject_abort(LoopObject *self, PyObject *args) */
/* { */
    /* DEBUG("self:%p", self); */
    /* return loop_abort(); */
/* } */

/* static PyObject* */
/* LoopObject_reset(LoopObject *self, PyObject *args) */
/* { */
    /* DEBUG("self:%p", self); */
    /* if (stopped == 1) { */
        /* loop_done = 1; */
        /* stopped = 0; */
    /* } */
    /* Py_RETURN_NONE; */
/* } */

/* static int */
/* init_internal_loop_thread(LoopObject *self) */
/* { */
    /* PyObject *run = NULL; */
    /* PyObject *temp = NULL; */

    /* Py_CLEAR(self->greenlet); */

    /* run = PyObject_GetAttrString((PyObject *)self, "run"); */
    /* if (run == NULL) { */
        /* return -1; */
    /* } */
    /* //loop.run  */
    /* temp = greenlet_new(run, NULL); */

    /* if (temp == NULL) { */
        /* Py_DECREF(run); */
        /* return -1; */
    /* } */
    /* Py_DECREF(greenlet_getparent(temp)); */

    /* Py_DECREF(run); */
    /* Py_INCREF(temp); */
    /* self->greenlet = temp; */
    /* GDEBUG("create new loop greenlet %p", temp); */
    /* return 1; */
/* } */

/* static int */
/* init_loop_thread(void) */
/* { */
    /* return init_internal_loop_thread(get_event_loop()); */
/* } */


PyObject*
loop_switch(LoopObject *loop)
{
    PyObject *current = NULL, *parent = NULL;
    
    DEBUG("self:%p", loop);
    current = greenlet_getcurrent();
    parent = greenlet_getparent(loop->greenlet);

    /* DEBUG("current:%p parent:%p", current, parent); */
    Py_DECREF(current);
    if (loop->greenlet == current) {
        /* DEBUG("switch to parent"); */
        return greenlet_switch(parent, loop_switch_value, NULL);
    }

    DEBUG("switch loop thread:%p loop_parent:%p current:%p", loop->greenlet, parent, current);
    /* DEBUG("current:%p cnt:%d", current, Py_REFCNT(current)); */

    if (greenlet_dead(loop->greenlet)) {
        Py_CLEAR(loop->greenlet);
        //PyErr_SetString(PyExc_IOError, "stoped loop thread");
        YDEBUG("warning !! loop is dead");
        loop->suspend_thread = greenlet_getcurrent();
        if (run_loop(loop) < 0) {
            return NULL;
        }
        Py_RETURN_NONE;
    }

    if (parent != current) {
        greenlet_setparent(current, loop->greenlet);
    }
    //force switch loop thread
    /* DEBUG("force switch loop thread"); */
    return greenlet_switch(loop->greenlet, loop_switch_value, NULL);
}


static PyObject*
LoopObject_switch(LoopObject *self, PyObject *args)
{
    return loop_switch(self);
}

/* static PyObject* */
/* LoopObject_wait(LoopObject *self, PyObject *args, PyObject *kwargs) */
/* { */
    /* PyObject *ret; */

    /* ret = loop_switch(self); */
    /* if (PyErr_ExceptionMatches(LoopAbortException)) { */
        /* DEBUG("loop abort"); */
        /* PyErr_Clear(); */
        /* Py_XDECREF(ret); */
        /* Py_RETURN_NONE; */
    /* } */
    /* return ret; */
/* } */

/* PyObject* */
/* spawn(PyObject *callable, PyObject *args) */
/* { */
    /* PyObject *ret = NULL; */
    /* PyObject *greenlet = NULL; */
    /* LoopObject *loop = NULL; */

    /* loop = get_event_loop(); */
    /* if (loop == NULL) { */
        /* return NULL; */
    /* } */
    
    /* greenlet = greenlet_new(callable, loop->greenlet); */
    /* if (greenlet == NULL) { */
        /* return NULL; */
    /* } */
    /* DEBUG("start thread:%p cnt:%d", greenlet, (int)Py_REFCNT(greenlet)); */
    /* ret = greenlet_switch(greenlet, args, NULL); */
    /* if (greenlet_dead(greenlet)) { */
        /* Py_DECREF(greenlet); */
        /* DEBUG("fin thread:%p ret:%p cnt:%d", greenlet, ret, (int)Py_REFCNT(greenlet)); */
    /* } */
    /* return ret; */
/* } */


static void
print_exception(PyObject *context, PyObject *type, PyObject *value, PyObject *traceback)
{
    DEBUG("context:%p type:%p value:%p traceback:%p", context, type, value, traceback);
    if (PyObject_IsSubclass(type, system_errors)) {
        DEBUG("ignore");
        return;
    }
    if (traceback == NULL) {
        traceback = Py_None;
    }

    Py_INCREF(type);
    Py_INCREF(value);
    Py_INCREF(traceback);
    PyErr_Restore(type, value, traceback);
    PyErr_Print();
    if (context != NULL) {
       PySys_WriteStderr("%s occured from ", PyExceptionClass_Name(type));
       PyObject_Print(context, stderr, 0); 
       PySys_WriteStderr(" \n");
    }
}


static PyObject*
internal_loop_handle_error(LoopObject *loop, PyObject *type, PyObject *value, PyObject *traceback, int checktype)
{
    PyObject *current = NULL, *parent = NULL;
    PyObject *o, *throw, *timer;

    DEBUG("self:%p", loop);
    
    if (checktype && !PyObject_IsSubclass(type, system_errors)) {
        DEBUG("ignore exception");
        Py_RETURN_NONE;
    }

    current = greenlet_getcurrent();
    parent = greenlet_getparent(loop->greenlet);
    
    Py_DECREF(current);
    DEBUG("loop->greenlet:%p current:%p parent:%p", loop->greenlet, current, parent);

    if (current == loop->greenlet || current == parent) {
        RDEBUG("throw to main greenlet");
        loop->state = L_STOP;
        return greenlet_throw(parent, type, value, traceback);
    }

    o = Py_BuildValue("OOO", type, value, traceback);
    if (o == NULL) {
        return NULL;
    }
    throw = PyObject_GetAttrString((PyObject*)parent, "throw");
    if (throw == NULL) {
        return NULL;
    }
    timer = internal_loop_schedule_call(loop, 0, throw, o, NULL);
    if (timer == NULL) {
        return NULL;
    }
    Py_DECREF(throw);
    Py_DECREF(timer);
    Py_RETURN_NONE;
}

PyObject*
loop_handle_error(LoopObject *loop, PyObject *context)
{
    PyObject *t = NULL, *v = NULL, *tr = NULL;
    
    DEBUG("self:%p", loop);

    PyErr_Fetch(&t, &v, &tr);
    PyErr_Clear();
    if (tr == NULL) {
        tr = Py_None;
        Py_INCREF(tr);
    }
    print_exception(context, t, v, tr);
    return internal_loop_handle_error((LoopObject*)loop, t, v, tr, 1);
}

static PyObject*
loop_handle_timeout_error(LoopObject *loop, PyObject *context)
{
    PyObject *t, *v, *tr;

    DEBUG("self:%p", loop);
    PyErr_Fetch(&t, &v, &tr);
    PyErr_Clear();
    if (tr == NULL) {
        tr = Py_None;
        Py_INCREF(tr);
    }
    /* print_exception(context, t, v, tr); */
    return internal_loop_handle_error(loop, t, v, tr, 0);
}


PyObject*
schedule_timer(LoopObject *loop, PyObject *o)
{

    DEBUG("loop:%p", loop);

    queue_t *pendings = loop->pendings;
    heapq_t *timers = loop->scheduled;

    if (check_handle_obj(o)) {
        if (queue_push(pendings, (void*)o) == -1) {
            Py_DECREF(o);
            return NULL;
        }
        Py_INCREF(o);
        BDEBUG("add handle:%p pendings->size:%d", o, pendings->size);
    } else {
        if (heappush(timers, (TimerObject*)o) == -1) {
            Py_DECREF(o);
            return NULL;
        }
        BDEBUG("add timer:%p timers->size:%d", o, timers->size);
    }
    loop->activecnt++;
    return o;
}

static PyObject*
internal_loop_schedule_call(LoopObject *self, long seconds, PyObject *cb, PyObject *args, PyObject *greenlet)
{
    PyObject *handle;
    
    DEBUG("self:%p seconds:%ld", self, seconds);

    handle = make_handle((PyObject*)self, seconds, cb, args, greenlet);
    if (handle == NULL) {
        return NULL;
    }
    return schedule_timer(self, handle);
}

PyObject*
loop_schedule_call(LoopObject *loop, long seconds, PyObject *cb, PyObject *args, PyObject *greenlet)
{
    return internal_loop_schedule_call(loop, seconds, cb, args, greenlet);
}

PyObject*
loop_set_timeout(LoopObject *loop, long seconds, PyObject *exception)
{
    TimerObject *timer;
    PyObject *greenlet;
    PyObject *o, *throw, *exc, *value;

    DEBUG("self:%p seconds:%ld", loop, seconds);
    
    exc = exception;
    if (exc == NULL) {
        exc = TimeoutException;
    }
    
    // create Exception instance
    value = PyObject_CallFunctionObjArgs(exc, NULL);

    o = Py_BuildValue("OOO", exc, value, Py_None);
    if (o == NULL) {
        return NULL;
    }
    greenlet = greenlet_getcurrent();
    if (greenlet == NULL) {
        return NULL;
    }

    throw = PyObject_GetAttrString((PyObject*)greenlet, "throw");
    if (throw == NULL) {
        return NULL;
    }

    timer = (TimerObject*)internal_loop_schedule_call(loop, seconds, throw, o, NULL);
    if (timer == NULL) {
        return NULL;
    }
    if (seconds == 0) {
        timer->cancelled = 1;
    }
    Py_DECREF(o);
    Py_DECREF(throw);
    Py_DECREF(greenlet);

    if (seconds > 0 && 
        exc == TimeoutException &&
        (PyObject_SetAttrString(value, "timer", (PyObject*)timer) == -1)) {
        timer->cancelled = 1;
        timer->called = 1;
        Py_DECREF(timer);
        return NULL;
    }
    /* DUMP(timer); */
    return (PyObject *)timer;
}


/* static PyObject* */
/* internal_schedule_call(LoopObject *self, PyObject *args, PyObject *kwargs, PyObject *greenlet) */
/* { */
    /* long seconds = 0, ret; */
    /* Py_ssize_t size; */
    /* PyObject *sec = NULL, *cb = NULL, *cbargs = NULL, *timer; */
    /* LoopObject *loop = get_event_loop(); */

    /* DEBUG("self %p", self); */
    /* size = PyTuple_GET_SIZE(args); */
    /* DEBUG("args size %d", (int)size); */

    /* if (size < 2) { */
        /* PyErr_SetString(PyExc_TypeError, "schedule_call takes exactly 2 argument"); */
        /* return NULL; */
    /* } */
    /* sec = PyTuple_GET_ITEM(args, 0); */
    /* cb = PyTuple_GET_ITEM(args, 1); */

/* #ifdef PY3 */
    /* if (!PyLong_Check(sec)) { */
/* #else */
    /* if (!PyInt_Check(sec)) { */
/* #endif */
        /* PyErr_SetString(PyExc_TypeError, "must be integer"); */
        /* return NULL; */
    /* } */
    /* if (!PyCallable_Check(cb)) { */
        /* PyErr_SetString(PyExc_TypeError, "must be callable"); */
        /* return NULL; */
    /* } */

    /* ret = PyLong_AsLong(sec); */
    /* if (PyErr_Occurred()) { */
        /* return NULL; */
    /* } */
    /* if (ret < 0) { */
        /* PyErr_SetString(PyExc_TypeError, "seconds value out of range"); */
        /* return NULL; */
    /* } */
    /* seconds = ret; */

    /* if (size > 2) { */
        /* cbargs = PyTuple_GetSlice(args, 2, size); */
    /* } */

    /* timer = internal_loop_schedule_call(loop, seconds, cb, cbargs, greenlet); */
    /* Py_XDECREF(cbargs); */
    /* return timer; */
/* } */



static void
trampoline_switch_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    switch_arg_t *switch_arg = NULL;
    PyObject *res = NULL;

    switch_arg = (switch_arg_t*)cb_arg;

    DEBUG("trampoline_switch_callback loop:%p thread:%p fd:%d events:%d ", loop, switch_arg->greenlet, fd, events);

    if (unlikely((events & PICOEV_TIMEOUT) != 0)) {
        RDEBUG("trampoline_switch_callback timeout !!!!");
        PyErr_SetString(switch_arg->timeout_exc, "timeout");
        res = loop_handle_timeout_error(switch_arg->loop, (PyObject*)switch_arg->greenlet);

    } else if ((events & PICOEV_WRITE) != 0 ||  (events & PICOEV_READ) != 0) {
        //back 
        /* switch_arg->loop->state = L_SUSPEND; */
        res = greenlet_switch(switch_arg->greenlet, NULL, NULL);
    }
    Py_XDECREF(res);
}

static int
run_loop(LoopObject *loop)
{
    PyObject *run, *res;
    
    DEBUG("self:%p", loop);

    run = PyObject_GetAttrString((PyObject *)loop, "run");
    if (run == NULL) {
        return -1;
    }
    res = PyObject_CallObject(run, NULL);
    Py_DECREF(run);
    if (res == NULL) {
        return -1;
    }

    Py_DECREF(res);
    return 1;
    
}

static int
ready_trampoline(LoopObject *loop, int loop_check)
{
    PyObject *current = NULL;
    
    if (unlikely(loop == NULL)) {
        return -1;
    }

    DEBUG("loop:%p loop->greenlet:%p", loop, loop->greenlet);

    if (unlikely(greenlet_dead(loop->greenlet))) {
        Py_CLEAR(loop->greenlet);
        YDEBUG("loop not running ! loop:%p", loop);
        loop->suspend_thread = greenlet_getcurrent();
        if (run_loop(loop) == -1) {
            return -1;
        }
        return 1;
    }

    if (unlikely(loop->state == L_STOP)) {
        YDEBUG("loop not running ! loop:%p", loop);
        loop->suspend_thread = greenlet_getcurrent();
        if (run_loop(loop) == -1) {
            return -1;
        }
        return 1;
    }

    if (loop_check) {
        current = greenlet_getcurrent();
        if (unlikely(current == loop->greenlet)) {
            RDEBUG("Cannot switch to MAINLOOP from MAINLOOP");
            PyErr_SetString(PyExc_IOError, "Cannot switch to MAINLOOP from MAINLOOP");
            Py_DECREF(current);
            return -1;
        }
        Py_DECREF(current);
    }
    return 1;
}

int
start_loop(LoopObject *loop){
    if (loop_autostart) {
        return ready_trampoline(loop, 1);
    }
    return 1;
}

static PyObject*
internal_io_trampoline(LoopObject *loop, int fd, int event, int timeout, PyObject *exception, PyObject *args)
{
    int ret, active;
    int t = timeout < 0 ? 0 : timeout;
    switch_arg_t *switch_arg;
    PyObject *current = NULL;
    PyObject *res;

    switch_arg = PyMem_Malloc(sizeof(switch_arg_t));
    if (unlikely(switch_arg == NULL)) {
        return NULL;
    }
    GDEBUG("alloc switch_arg:%p", switch_arg);

    current = greenlet_getcurrent();

    DEBUG("thread:%p cnt:%d", current, (int)Py_REFCNT(current));
    
    // switch to loop
    YDEBUG("add event loop:%p thread:%p fd:%d event:%d timeout:%d", loop->main_loop, current, fd, event, t);

    switch_arg->loop = loop;
    switch_arg->greenlet = current;
    switch_arg->timeout_exc = exception != NULL ? exception : TimeoutException;

    active = picoev_is_active(loop->main_loop, fd);

    ret = picoev_add(loop->main_loop, fd, event, t, trampoline_switch_callback, (void *)switch_arg);

    if (likely((ret == 0 && !active))) {
        /* loop->activecnt++; */
        loop->ioactivecnt++;
    }

    loop->state = L_RUNNING;
    res = greenlet_switch(loop->greenlet, args, NULL);

    GDEBUG("dealloc switch_arg:%p", switch_arg);

    PyMem_Free(switch_arg);
    Py_DECREF(current);

    YDEBUG("del event fd:%d thread:%p", fd, current);
    //clear event
    picoev_del(loop->main_loop, fd);
    loop->ioactivecnt--;

    return res;
}

int
io_trampoline(LoopObject* l, int fd, int event, int timeout, PyObject *exception)
{

    LoopObject *loop = l;
    if (loop == NULL) {
        loop = get_event_loop();
    } else {
        Py_INCREF(loop);
    }

    if (loop == NULL) {
        return -1;
    }
    DEBUG("fd:%d event:%d timeout:%d exception:%p", fd, event, timeout, exception);
    if (ready_trampoline(loop, 1) < 0) {
        Py_XDECREF(loop);
        return -1;
    }

    PyObject *ret = internal_io_trampoline(loop, fd, event, timeout, exception, NULL);
    Py_XDECREF(loop);
    Py_XDECREF(ret);
    if (ret == NULL && PyErr_Occurred()) {
        //PyErr_Print();
        RDEBUG("io_trampoline exception !!!!");
        //error
        return -1;
    }
    return 1;
}

static PyObject*
LoopObject_io_trampoline(LoopObject *self, PyObject *args, PyObject *kwargs)
{
    int fd, event, timeout = 0;
    PyObject *read = Py_None, *write = Py_None, *exception = NULL;

    static char *keywords[] = {"fileno", "read", "write", "timeout", "exception",  NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|OOiO:trampoline", keywords, &fd, &read, &write, &timeout, &exception)) {
        return NULL;
    }

    if (unlikely(fd < 0)) {
        PyErr_SetString(PyExc_ValueError, "fileno value out of range ");
        return NULL;
    }

    if (unlikely(timeout < 0)) {
        PyErr_SetString(PyExc_ValueError, "timeout value out of range ");
        return NULL;
    }

    if (PyObject_IsTrue(read) && PyObject_IsTrue(write)) {
        event = PICOEV_READWRITE;
    }else if (PyObject_IsTrue(read)) {
        event = PICOEV_READ;
    }else if (PyObject_IsTrue(write)) {
        event = PICOEV_WRITE;
    }else{
        event = PICOEV_TIMEOUT;
        if (unlikely(timeout <= 0)) {
            PyErr_SetString(PyExc_ValueError, "timeout value out of range ");
            return NULL;
        }
    }

    if (ready_trampoline(self, 1) < 0) {
        return NULL;
    }
    return internal_io_trampoline(self, fd, event, timeout, exception, NULL);
}

/* void */
/* switch_callback(picoev_loop* loop, int fd, int events, void* cb_arg) */
/* { */
    /* PyObject *t = (PyObject *)cb_arg; */
    /* PyObject *res, *args; */

    /* DEBUG("thread:%p fd:%d events:%d",t, fd, events); */
    /* args = Py_BuildValue("(i)", fd); */
    /* res = greenlet_switch(t, args, NULL); */
    /* Py_XDECREF(res); */
/* } */


int
add_switch_event(int fd, int event, int timeout, picoev_handler *cb)
{
    int ret, active;
    LoopObject *loop = get_event_loop();
    PyObject *current = NULL;
    
    if (loop == NULL) {
        return -1;
    }
    if (ready_trampoline(loop, 1) < 0) {
        Py_XDECREF(loop);
        return -1;
    }
    current = greenlet_getcurrent();
    // switch to loop
    YDEBUG("add event loop:%p thread:%p fd:%d event:%d timeout:%d", loop->main_loop, current, fd, event, timeout);
    active = picoev_is_active(loop->main_loop, fd);
    ret = picoev_add(loop->main_loop, fd, event, timeout, cb, (void *)current);
    if (ret == 0 && !active) {
        loop->activecnt++;
        loop->ioactivecnt++;
    }
    Py_XDECREF(loop);
    Py_DECREF(current);
    return 1;
}

int
add_event(LoopObject *loop, int fd, int event, int timeout, picoev_handler *cb, void *data)
{
    int ret, active;
    if (ready_trampoline(loop, 0) < 0) {
        return -1;
    }
    // switch to loop
    YDEBUG("add event loop:%p fd:%d event:%d timeout:%d", loop->main_loop, fd, event, timeout);
    active = picoev_is_active(loop->main_loop, fd);
    ret = picoev_add(loop->main_loop, fd, event, timeout, cb, (void *)data);
    if (ret == 0 && !active) {
        /* loop->activecnt++; */
        loop->ioactivecnt++;
    }
    return 1;
}

int
is_active_event(LoopObject *loop, int fd)
{
    if (loop && loop->main_loop) {
        return picoev_is_active(loop->main_loop, fd);
    }
    return 0;
}

void
del_event(LoopObject *loop, int fd)
{
    if (loop->main_loop && picoev_is_active(loop->main_loop, fd)) {
        YDEBUG("del event fd:%d", fd);
        picoev_del(loop->main_loop, fd);
        /* loop->activecnt--; */
        loop->ioactivecnt--;
    }
}

void
update_event_timeout(LoopObject *loop, int fd, int timeout)
{
    picoev_set_timeout(loop->main_loop, fd, timeout);
}

int
cmp_loop_thread(PyObject *greenlet)
{
    LoopObject *loop;
    loop = (LoopObject*)get_event_loop();
    if (loop == NULL) {
        return -1;
    }
    if (loop->greenlet == NULL) {
        return 0;
    }
    Py_XDECREF(loop);

    return loop->greenlet == greenlet;
}

/* static PyObject* */
/* LoopObject_sleep_callback(PyObject *self, PyObject *args) */
/* { */
    /* PyObject *o, *ret; */
    
    /* o = PyTuple_GET_ITEM(args, 0); */
    /* if (o == NULL) { */
        /* return NULL; */
    /* } */
    /* ret = greenlet_switch((PyObject*)o, NULL, NULL); */
    /* Py_XDECREF(ret); */
    /* Py_RETURN_NONE; */
/* } */

PyObject*
loop_sleep(LoopObject *loop, long time)
{
    PyObject *timer, *ret = NULL;
    PyObject *current;

    current = greenlet_getcurrent();
    if (current == NULL) {
        return NULL;
    }
    timer = internal_loop_schedule_call(loop, time, NULL, NULL, current);
    Py_DECREF(current);
    if (timer == NULL) {
        return NULL;
    }
    ret = loop_switch(loop);
    Py_XDECREF(ret);
    Py_INCREF(timer);
    return timer;
}

// ------------------------------------------------
// Object Protocol
// ------------------------------------------------

/* static PyObject * */
/* LoopObject_new(PyTypeObject *type, PyObject *args, PyObject *kwds) */
/* { */
    /* LoopObject *self; */

    /* self = (LoopObject *)type->tp_alloc(type, 0); */
    /* if (self == NULL) { */
        /* return NULL; */
    /* } */
    
    /* GDEBUG("alloc LoopObject:%p", self); */
    /* return (PyObject *)self; */
/* } */

static int 
LoopObject_init(LoopObject *self, PyObject *args, PyObject *kwargs)
{
    DEBUG("self:%p", self);

    Py_CLEAR(self->weakreflist);

    self->scheduled = init_heapqueue();
    if (self->scheduled == NULL) {
        return -1;
    }
    self->pendings = init_queue(1024);
    if (self->pendings == NULL) {
        return -1;
    }
    self->postfork = 0; 
    self->maxfd = MAX_FD;
    init_main_loop(self);
    
    self->activecnt = 0;
    self->ioactivecnt = 0;
    self->state = L_STOP;
    self->suspend_thread = NULL;
    if (init_lookup(self) < 0) {
        return -1;
    }
    DEBUG("self:%p pendings:%p", self, self->pendings);
    return 0;
}

static int 
LoopObject_clear(LoopObject *self)
{
    PyObject *item = NULL;

    DEBUG("self:%p", self);
    destroy_main_loop(self);

    while ((item = (PyObject*)queue_shift(self->pendings)) != NULL) {
        GDEBUG("drop item:%p", item);
        Py_CLEAR(item);
    }
    destroy_queue(self->pendings);
    destroy_heapqueue(self->scheduled);

    Py_CLEAR(self->greenlet);
    Py_CLEAR(self->suspend_thread);
    return 0;
}

static int
LoopObject_traverse(LoopObject *self, visitproc visit, void *arg)
{
    DEBUG("self:%p", self);
    Py_VISIT(self->greenlet);
    Py_VISIT(self->suspend_thread);
    return 0;
}

static void
LoopObject_dealloc(LoopObject *self)
{
    GDEBUG("dealloc LoopObject:%p", self);

    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self);
    if (self->weakreflist != NULL){
        PyObject_ClearWeakRefs((PyObject *)self);
    }
    LoopObject_clear(self);
    Py_TYPE(self)->tp_free((PyObject*)self);
    Py_TRASHCAN_SAFE_END(self);
}

// -----------------------------------------------------
// Event Loop Methods
// -----------------------------------------------------

// -----------------------------------------------------
// internal function
// -----------------------------------------------------

static callback_arg_t*
make_callback_arg(
        LoopObject *loop,
        int events,
        PyObject *read_callback,
        PyObject *read_args,
        PyObject *write_callback,
        PyObject *write_args)
{

    callback_arg_t *carg = PyMem_Malloc(sizeof(callback_arg_t));
    if (carg == NULL) {
        return NULL;
    }
    memset(carg, 0x0, sizeof(callback_arg_t));
    
    GDEBUG("alloc callback_arg_t:%p", carg);

    carg->loop = loop;
    Py_XINCREF(carg->loop);

    if ((events & PICOEV_READ) != 0) {
        if (read_callback == NULL) {
            PyMem_Free(carg);
            return NULL;
        }
        carg->read_callback = read_callback;
        carg->read_args = read_args;
    }

    if ((events & PICOEV_WRITE) != 0) {
        if (write_callback == NULL) {
            PyMem_Free(carg);
            return NULL;
        }
        carg->write_callback = write_callback;
        carg->write_args = write_args;
    }
    Py_XINCREF(carg->read_callback);
    Py_XINCREF(carg->read_args);
    Py_XINCREF(carg->write_callback);
    Py_XINCREF(carg->write_args);
    return carg;
}

static void
destroy_callback_arg(callback_arg_t *carg) 
{
    GDEBUG("dealloc callback_arg_t:%p", carg);
    Py_CLEAR(carg->loop);
    Py_CLEAR(carg->read_callback);
    Py_CLEAR(carg->read_args);
    Py_CLEAR(carg->write_callback);
    Py_CLEAR(carg->write_args);
    PyMem_Free(carg);
}

static int 
merge_callback_arg(picoev_loop *loop, int fd, callback_arg_t *carg) 
{
    callback_arg_t *orig_carg;
    
    (void)picoev_get_callback(loop, fd, (void**)&orig_carg);

    if (orig_carg) {
        if (carg->read_callback) {
            //Not allow overwrite
            if (orig_carg->read_callback == NULL) {
                orig_carg->read_callback = carg->read_callback;
                orig_carg->read_args = carg->read_args;
                Py_XINCREF(orig_carg->read_callback);
                Py_XINCREF(orig_carg->read_args);
                return 1;
            } else {
                //TODO Error?
                return -1;
            }
        }

        if (carg->write_callback) {
            if (orig_carg->write_callback == NULL) {
                orig_carg->write_callback = carg->write_callback;
                orig_carg->write_args = carg->write_args;
                Py_XINCREF(orig_carg->write_callback);
                Py_XINCREF(orig_carg->write_args);
                return 1;
            } else {
                //TODO Error?
                return -1;
            }
        }
    }
    return -1;

}

static void
fd_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    PyObject *res = NULL;
    LoopObject *self = NULL;
    callback_arg_t *carg = (callback_arg_t*)cb_arg;
    
    DEBUG("call fd_callback:%p fd:%d events:%d", carg, fd, events);

    /* picoev_del(loop, fd); */
    if ((events & PICOEV_TIMEOUT) != 0) {
        RDEBUG("TIMEOUT");
    } else if ((events & PICOEV_READ) != 0) {
        DEBUG("call READ callback:%p", carg);
        res = PyEval_CallObject(carg->read_callback, carg->read_args);
        DEBUG("END call READ callback:%p", carg);

    } else if ((events & PICOEV_WRITE) != 0) {
        DEBUG("call WRITE callback:%p", carg);
        res = PyEval_CallObject(carg->write_callback, carg->write_args);
    } else {
        //PICOEV_ERR
        YDEBUG("del event fd:%d", fd);
        picoev_del(loop, fd);
        self = get_event_loop();
        if (self == NULL) {
            return;
        }
        /* self->activecnt--; */
        self->ioactivecnt--;
        PyErr_SetString(PyExc_IOError, "registered fd is invalid");
        destroy_callback_arg(carg);
        Py_DECREF(self);
    }
    Py_XDECREF(res);
}

static int
register_fd_callback(LoopObject *loop, int fd, int event, callback_arg_t *carg, int timeout)
{
    int active, ret;

    if (unlikely(call_fork_loop(loop) == -1)) {
        return -1;
    }

    active = picoev_is_active(loop->main_loop, fd);
    if (active) {
        ret = merge_callback_arg(loop->main_loop, fd, carg);
        if (ret > 0) {
            ret = picoev_set_events(loop->main_loop, fd, PICOEV_READWRITE);
            if (ret != 0) {
                return -1;
            }
        }
    } else {
        YDEBUG("add event loop:%p fd:%d event:%d timeout:%d", loop->main_loop, fd, event, timeout);
        ret = picoev_add(loop->main_loop, fd, event, timeout, fd_callback, (void *)carg);
        if (likely(ret == 0)) {
            /* loop->activecnt++; */
            loop->ioactivecnt++;
        } else {
            //TODO set error
            return -1;
        }
    }
    return 1;
}


PyObject*
internal_run_once(LoopObject *self, int timeout)
{
    heapq_t *q = NULL;
    queue_t *pendings = NULL;
    picoev_loop *loop = NULL;
    
    PDEBUG("START self:%p state:%d activecnt:%d ioactivecnt:%d", self, self->state, self->activecnt, self->ioactivecnt);

    loop = self->main_loop;
    q = self->scheduled;
    pendings = self->pendings;
    
    DEBUG("self:%p q->size:%d", self, q->size);
    
    if (unlikely(call_fork_loop(self) == -1)) {
        return NULL;
    }

    if (q->size > 0 && unlikely(fire_timers(self) == -1)) {
        return NULL;
    }
    
    if (pendings->size > 0 && unlikely(fire_pendings(self) == -1)) {
        return NULL;
    }
    
    if (q->size > 0 || self->ioactivecnt > 1) {
        PDEBUG("wait or I/O event... self:%p", self);
        picoev_loop_once(loop, timeout);
    }
    
    if (unlikely(call_fork_loop(self) == -1)) {
        return NULL;
    }
    PDEBUG("END   self:%p state:%d activecnt:%d ioactivecnt:%d", self, self->state, self->activecnt, self->ioactivecnt);

    Py_RETURN_NONE;
}

int 
check_interrupted(LoopObject *self)
{
    return check_stop_signal(self);
}

static PyObject*
LoopObject_run(LoopObject *self, PyObject *args)
{
    PyObject *res = NULL, *o = NULL;
    
    int interrupted = 0;

    DEBUG("self:%p", self);
    self->state = L_RUNNING;

    if (self->suspend_thread) {
        //reset_main_loop();
        PDEBUG("switch to suspend_thread self:%p greenlet:%p", self, self->suspend_thread);
        o = greenlet_switch(self->suspend_thread, NULL, NULL);
        Py_XDECREF(o);
        PDEBUG("from suspend_thread self:%p greenlet:%p", self, self->suspend_thread);
        Py_CLEAR(self->suspend_thread);
        if (PyErr_Occurred()) {
            return NULL;
        }
    }
    PDEBUG("start run self:%p state:%d activecnt:%d ioactivecnt:%d", self, self->state, self->activecnt, self->ioactivecnt);

    while (likely(self->state != L_STOP && (self->activecnt > 0 || self->ioactivecnt > 1))) {
        res = internal_run_once(self, 1);
        if (unlikely(res == NULL)) {
            RDEBUG("res == NULL");
            goto error;
        }
        Py_DECREF(res);

        interrupted = check_stop_signal(self);

        // TODO Error??
        if (unlikely(PyErr_Occurred() != NULL)) {
            RDEBUG("Not NULL ?? stop");
            goto error;
        }

    }
    PDEBUG("stop run self:%p state:%d activecnt:%d ioactivecnt:%d", self, self->state, self->activecnt, self->ioactivecnt);

    self->state = L_STOP;
    PDEBUG("stop loop:%p", self);
    if (interrupted) {
        //override
        PyErr_Clear();
        PyErr_SetNone(PyExc_KeyboardInterrupt);
        return NULL;
    }
    Py_RETURN_NONE;
error:
    PDEBUG("Error stop loop");
    self->state = L_STOP;
    if (PyErr_ExceptionMatches(LoopAbortException)) {
        PDEBUG("LoopAbortException stop loop:%p", self);
        PyErr_Clear();
        Py_RETURN_NONE;
    }
    return NULL;
}


static PyObject*
LoopObject_run_once(LoopObject *self, PyObject *args, PyObject *kwargs)
{
    int timeout = 10;
    PyObject *o = NULL;

    static char *keywords[] = {"timeout", NULL};

    DEBUG("self:%p", self);
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i:run_once", keywords, &timeout)) {
        return NULL;
    }
    if (self->suspend_thread) {
        //reset_main_loop();
        DEBUG("switch to suspend_thread");
        o = greenlet_switch(self->suspend_thread, NULL, NULL);
        Py_XDECREF(o);
        Py_CLEAR(self->suspend_thread);
        DEBUG("from suspend_thread");
    }

    return internal_run_once(self, timeout);

}

static PyObject*
LoopObject_close(LoopObject *self, PyObject *args)
{
    Py_RETURN_NONE;
}

static PyObject*
LoopObject_call_later(LoopObject *self, PyObject *args)
{
    long seconds = 0, ret;
    Py_ssize_t size;
    PyObject *sec = NULL, *cb = NULL, *cbargs = NULL, *timer;

    DEBUG("self:%p", self);
    size = PyTuple_GET_SIZE(args);
    DEBUG("args size %d", (int)size);

    if (size < 2) {
        PyErr_SetString(PyExc_TypeError, "call_later takes exactly 2 argument");
        return NULL;
    }
    sec = PyTuple_GET_ITEM(args, 0);
    cb = PyTuple_GET_ITEM(args, 1);

#ifdef PY3
    if (!PyLong_Check(sec)) {
#else
    if (!PyInt_Check(sec)) {
#endif
        PyErr_SetString(PyExc_TypeError, "must be integer");
        return NULL;
    }
    if (!PyCallable_Check(cb)) {
        PyErr_SetString(PyExc_TypeError, "must be callable");
        return NULL;
    }

    ret = PyLong_AsLong(sec);
    if (PyErr_Occurred()) {
        return NULL;
    }
    if (ret < 0) {
        PyErr_SetString(PyExc_TypeError, "seconds value out of range");
        return NULL;
    }
    seconds = ret;

    if (size > 2) {
        cbargs = PyTuple_GetSlice(args, 2, size);
    }

    timer = internal_loop_schedule_call(self, seconds, cb, cbargs, NULL);
    Py_XDECREF(cbargs);
    return timer;
}

static PyObject*
LoopObject_call_repeatedly(LoopObject *self, PyObject *args)
{
    TimerObject *timer = (TimerObject*)LoopObject_call_later(self, args);
    if (timer) {
        timer->repeat = 1;
    }
    return (PyObject*)timer;
}

static PyObject*
LoopObject_call_soon(LoopObject *self, PyObject *args)
{
    Py_ssize_t size;
    PyObject *cb = NULL, *cbargs = NULL, *timer;

    DEBUG("self:%p", self);
    size = PyTuple_GET_SIZE(args);
    DEBUG("args size %d", (int)size);

    if (size < 1) {
        PyErr_SetString(PyExc_TypeError, "call soon takes exactly 1 argument");
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

    timer = internal_loop_schedule_call(self, 0, cb, cbargs, NULL);
    Py_XDECREF(cbargs);
    return timer;
}

static PyObject*
LoopObject_add_callback(LoopObject *self, PyObject *o)
{
    PyObject *res;

    res = schedule_timer(self, o);
    if (res == NULL) {
        return NULL;
    }
    Py_INCREF(res);
    return res;
}

static PyObject*
LoopObject_make_handle(LoopObject *self, PyObject *args)
{
    PyObject *cb = NULL, *cbargs = NULL, *handle;

    if (!PyArg_ParseTuple(args,  "OO:make_handle", &cb, &cbargs)) {
        return NULL;
    }

    handle = make_handle((PyObject*)self, 0, cb, cbargs, NULL);
    if (handle == NULL) {
        return NULL;
    }
    Py_INCREF(handle);
    return handle;
}

// ------------------------------------------------
// Event I/O 
// ------------------------------------------------

static PyObject*
LoopObject_add_reader(LoopObject *self, PyObject *args)
{
    long int_fd = 0;
    Py_ssize_t size;
    PyObject *filelike = NULL, *cb = NULL, *cbargs = NULL;
    callback_arg_t *carg;
    
    DEBUG("self:%p", self);
    size = PyTuple_GET_SIZE(args);
    DEBUG("args size %d", (int)size);

    if (size < 2) {
        PyErr_SetString(PyExc_TypeError, "add reader takes exactly 2 argument");
        return NULL;
    }
    
    filelike = PyTuple_GET_ITEM(args, 0);
    cb = PyTuple_GET_ITEM(args, 1);


    if (!PyCallable_Check(cb)) {
        PyErr_SetString(PyExc_TypeError, "must be callable");
        return NULL;
    }

    int_fd = PyObject_AsFileDescriptor(filelike);
    if (int_fd == -1) {
        return NULL;
    }

    if (size > 2) {
        cbargs = PyTuple_GetSlice(args, 2, size);
    }

    carg = make_callback_arg(self, PICOEV_READ, cb, cbargs, NULL, NULL);
    if (!carg) {
        Py_XDECREF(cbargs);
        return NULL;
    }

    if (register_fd_callback(self, int_fd, PICOEV_READ, carg, 0) == -1) {
        //TODO Error
        return NULL;
    }
    
    Py_RETURN_NONE;
}

static PyObject*
LoopObject_remove_reader(LoopObject *self, PyObject *args)
{
    int fd = 0, ret = 0;
    PyObject *filelike;
    callback_arg_t *orig_carg = NULL;

    if (!PyArg_ParseTuple(args,  "O:remove_reader", &filelike)) {
        return NULL;
    }

    fd = PyObject_AsFileDescriptor(filelike);
    if (fd == -1) {
        return NULL;
    }
    
    if (self->main_loop && picoev_is_active(self->main_loop, fd)) {

        (void)picoev_get_callback(self->main_loop, fd, (void**)&orig_carg);

        if (orig_carg->write_callback) {
            //READWRITE
            Py_CLEAR(orig_carg->read_callback);
            Py_CLEAR(orig_carg->read_args);

            ret = picoev_set_events(self->main_loop, fd, PICOEV_WRITE);
            if (ret == 0) {
            }
             
        } else {
            YDEBUG("del event fd:%d", fd);
            picoev_del(self->main_loop, fd);
            /* self->activecnt--; */
            self->ioactivecnt--;
            destroy_callback_arg(orig_carg); 
        }
    }
    Py_RETURN_NONE;
}

static PyObject*
LoopObject_add_writer(LoopObject *self, PyObject *args)
{
    long int_fd = 0;
    Py_ssize_t size;
    PyObject *filelike = NULL, *cb = NULL, *cbargs = NULL;
    callback_arg_t *carg;
    
    DEBUG("self:%p", self);
    size = PyTuple_GET_SIZE(args);
    DEBUG("args size %d", (int)size);

    if (size < 2) {
        PyErr_SetString(PyExc_TypeError, "add writer takes exactly 2 argument");
        return NULL;
    }
    
    filelike = PyTuple_GET_ITEM(args, 0);
    cb = PyTuple_GET_ITEM(args, 1);


    if (!PyCallable_Check(cb)) {
        PyErr_SetString(PyExc_TypeError, "must be callable");
        return NULL;
    }

    int_fd = PyObject_AsFileDescriptor(filelike);
    if (int_fd == -1) {
        return NULL;
    }

    if (size > 2) {
        cbargs = PyTuple_GetSlice(args, 2, size);
    }

    carg = make_callback_arg(self, PICOEV_WRITE, NULL, NULL, cb, cbargs);
    if (!carg) {
        Py_XDECREF(cbargs);
        return NULL;
    }

    if (register_fd_callback(self, int_fd, PICOEV_WRITE, carg, 0) == -1) {
        //TODO Error
        return NULL;
    }
    
    Py_RETURN_NONE;
}

static PyObject*
LoopObject_remove_writer(LoopObject *self, PyObject *args)
{
    int fd = 0, ret = 0;
    callback_arg_t *orig_carg = NULL;
    PyObject *filelike = NULL;

    if (!PyArg_ParseTuple(args,  "O:remove_writer", &filelike)) {
        return NULL;
    }

    fd = PyObject_AsFileDescriptor(filelike);
    if (fd == -1) {
        return NULL;
    }

    if (self->main_loop && picoev_is_active(self->main_loop, fd)) {
        (void)picoev_get_callback(self->main_loop, fd, (void**)&orig_carg);

        if (orig_carg->read_callback) {
            Py_CLEAR(orig_carg->write_callback);
            Py_CLEAR(orig_carg->write_args);

            ret = picoev_set_events(self->main_loop, fd, PICOEV_READ);
            if (ret == 0) {
            }
             
        } else {
            YDEBUG("del event fd:%d", fd);
            picoev_del(self->main_loop, fd);
            /* self->activecnt--; */
            self->ioactivecnt--;
            destroy_callback_arg(orig_carg); 
        }
    }
    Py_RETURN_NONE;
}


static PyObject*
LoopObject_set_timeout(LoopObject *self, PyObject *args, PyObject *kwargs)
{
    int seconds = 0;
    static char *keywords[] = {"timeout", NULL};
    
    DEBUG("self:%p", self);

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i:set_timeout", keywords, &seconds)) {
        return NULL;
    }
    if (seconds < 0) {
        PyErr_SetString(PyExc_ValueError, "timeout value out of range ");
        return NULL;
    }
    return loop_set_timeout(self, seconds, NULL);
}

static PyObject*
LoopObject_sleep(LoopObject *self, PyObject *args, PyObject *kwargs)
{
    int seconds = 0;
    static char *keywords[] = {"time", NULL};
    
    DEBUG("self:%p", self);

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i:sleep", keywords, &seconds)) {
        return NULL;
    }
    if (seconds < 0) {
        PyErr_SetString(PyExc_ValueError, "time value out of range ");
        return NULL;
    }
    return loop_sleep(self, seconds);
}

static PyObject*
LoopObject_run_on_main_task(LoopObject *self, PyObject *args)
{
    PyObject *cb = NULL;
    PyObject *greenlet = NULL, *res = NULL;

    DEBUG("self:%p", self);

    if (!PyArg_ParseTuple(args,  "O:run_on_main_task", &cb)) {
        return NULL;
    }

    if (!PyCallable_Check(cb)) {
        PyErr_SetString(PyExc_TypeError, "must be callable");
        return NULL;
    }

    /* if (self->state == L_RUNNING) { */
        /* PyErr_SetString(PyExc_ValueError, "already running"); */
        /* return NULL; */
    /* } */
    

    if (greenlet_dead(self->greenlet)) {
        Py_CLEAR(self->greenlet);
        greenlet = greenlet_new(cb, NULL);
        DEBUG("new greenlet:%p", greenlet);
        if (greenlet == NULL) {
            return NULL;
        }
        self->greenlet = greenlet;
    } else {
        DEBUG("running loop:%p greenlet:%p", self, self->greenlet);
    }

    /* GDEBUG("self:%p refs:%d", self, Py_REFCNT(self->greenlet)); */

    self->state = L_RUNNING;
    res = greenlet_switch(self->greenlet, NULL, NULL);
    if (greenlet_dead(self->greenlet)) {
        PDEBUG("end run loop:%p running:%d", self, self->state);
        self->state = L_STOP;
    }

    /* GDEBUG("self:%p refs:%d", self, Py_REFCNT(self->greenlet)); */
    return res;
}

static PyObject*
LoopObject_make_channel(LoopObject *self, PyObject *args)
{
    int size = 0;

    if (!PyArg_ParseTuple(args,  "|i:make_channel", &size)) {
        return NULL;
    }
    if (size < 0) {
        PyErr_SetString(PyExc_TypeError, "buffer size value out of range");
        return NULL;
    }
    return (PyObject*)create_channel(self, size);
}

static PyObject *socket_factory = NULL;

static PyObject*
socket_fromfd(int fd, int family, int type)
{
    PyObject *f, *m, *md, *sys_modules;
    DEBUG("fd:%d", fd);

    if (unlikely(socket_factory == NULL)) {
        sys_modules = PySys_GetObject("modules");
        m = PyDict_GetItemString(sys_modules, "jega.ext.jsocket");
        if(m == NULL){
            m = PyImport_ImportModule("jega.ext.jsocket");
            if(m == NULL){
                return NULL;
            }
        }
        md = PyModule_GetDict(m);
        if(!md){
            return NULL;
        }
        f = PyDict_GetItemString(md, "fromfd");
        if(f == NULL){
            return NULL;
        }
        socket_factory = f;
    }
    DEBUG("call _socketfactory");
    return PyObject_CallFunction(socket_factory, "iii", fd, family, type);
}

static PyObject *executor = NULL;

static ExecutorObject*
get_executor(void)
{
    PyObject *o, *m, *md, *sys_modules;

    if (unlikely(executor == NULL)) {
        sys_modules = PySys_GetObject("modules");
        m = PyDict_GetItemString(sys_modules, "jega.loop");
        if(m == NULL){
            m = PyImport_ImportModule("jega.loop");
            if(m == NULL){
                return NULL;
            }
        }
        md = PyModule_GetDict(m);
        if(!md){
            return NULL;
        }
        o = PyDict_GetItemString(md, "_executor");
        if(o == NULL){
            return NULL;
        }
        executor = o;
    }
    Py_INCREF(executor);
    return (ExecutorObject*)executor;
}

static void
accept_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
    PyObject *res = NULL;
    PyObject *socket = NULL;
    PyObject *arg = NULL, *addr = NULL;
    ExecutorObject *executor;

    int client_fd = 0;
    sock_addr_t client_addr;
    socklen_t client_len = 0;

    callback_arg_t *carg = (callback_arg_t*)cb_arg;
    
    DEBUG("call fd_callback:%p fd:%d events:%d", carg, fd, events);
    if (!getsockaddrlen(carg->family, &client_len)){
        return ;
    }
    memset(&client_addr, 0x0, client_len);

    if (unlikely((events & PICOEV_TIMEOUT) != 0)) {
        RDEBUG("TIMEOUT");
    } else if ((events & PICOEV_READ) != 0) {

        for (;;) {
            client_fd = accept(fd, SAS2SA(&client_addr), &client_len);
            if (unlikely(client_fd != -1)) {
                DEBUG("accept fd %d", client_fd);

                socket = socket_fromfd(client_fd, carg->family, carg->type);
                
                if (unlikely(socket == NULL)) {
                    picoev_del(loop, fd);
                    close(client_fd);
                    return;
                }
                DEBUG("socket:%p", socket);

                addr = getaddrtuple(carg->loop, carg->family, SAS2SA(&client_addr), client_len);
                if (unlikely(addr == NULL)) {
                    Py_DECREF(socket);
                    picoev_del(loop, fd);
                    close(client_fd);
                    return;
                }
                DEBUG("addr:%p", addr);

                if ( unlikely((arg = PyTuple_New(2)) == NULL)) {
                    Py_XDECREF(socket);
                    Py_XDECREF(addr);
                    picoev_del(loop, fd);
                    close(client_fd);
                    return;
                }
                PyTuple_SET_ITEM(arg, 0, socket);
                PyTuple_SET_ITEM(arg, 1, addr);
                executor = get_executor();
                res = executor_submit(executor, carg->read_callback, arg, NULL);
                Py_XDECREF(executor);
                Py_XDECREF(res);
                Py_XDECREF(arg);

            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    PyErr_SetFromErrno(PyExc_IOError);
                }
                break;
            }
        }


    }
}


static PyObject*
LoopObject_set_socket_acceptor(LoopObject *self, PyObject *args, PyObject *kwargs)
{

    PyObject *filelike = NULL;
    PyObject *handler = NULL;
    int active, ret;
    int family, type, fd;
    callback_arg_t *carg = NULL;

    static char *keywords[] = {"filelike", "family", "type", "handler", NULL};
    
    DEBUG("self:%p", self);

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OiiO:set_socket_acceptor", keywords, &filelike, &family, &type, &handler)) {
        return NULL;
    }

    if (!PyCallable_Check(handler)) {
        PyErr_SetString(PyExc_TypeError, "must be callable");
        return NULL;
    }

    fd = PyObject_AsFileDescriptor(filelike);
    if (fd == -1) {
        return NULL;
    }

    carg = make_callback_arg(self, PICOEV_READ, handler, NULL, NULL, NULL);
    if (!carg) {
        return NULL;
    }
    carg->family = family;
    carg->type = type;

    active = picoev_is_active(self->main_loop, fd);
    if (active) {
        PyErr_SetString(PyExc_ValueError, "already set this fd.");
        return NULL;
    } else {
        YDEBUG("add event loop:%p fd:%d event:%d timeout:%d", self->main_loop, fd, PICOEV_READ, 0);
        ret = picoev_add(self->main_loop, fd, PICOEV_READ, 0, accept_callback, (void *)carg);
        if (ret == 0) {
            self->ioactivecnt++;
        } else {
            PyErr_SetString(PyExc_SystemError, "cannot register this fd.");
            return NULL;
        }
    }
    

    Py_RETURN_NONE;
}

static PyMethodDef LoopObject_methods[] = {
    {"switch", (PyCFunction)LoopObject_switch, METH_NOARGS, 0},
    {"_run", (PyCFunction)LoopObject_run, METH_NOARGS, 0},
    {"_run_once", (PyCFunction)LoopObject_run_once, METH_VARARGS|METH_KEYWORDS, 0},
    {"close", (PyCFunction)LoopObject_close, METH_NOARGS, 0},
    {"call_later", (PyCFunction)LoopObject_call_later, METH_VARARGS, 0},
    {"call_repeatedly", (PyCFunction)LoopObject_call_repeatedly, METH_VARARGS, 0},
    {"call_soon", (PyCFunction)LoopObject_call_soon, METH_VARARGS, 0},
    
    // internal
    {"_add_callback", (PyCFunction)LoopObject_add_callback, METH_O, 0},
    {"_make_handle", (PyCFunction)LoopObject_make_handle, METH_VARARGS, 0},
    
    // event driven i/o
    {"add_reader", (PyCFunction)LoopObject_add_reader, METH_VARARGS, 0},
    {"remove_reader", (PyCFunction)LoopObject_remove_reader, METH_VARARGS, 0},
    {"add_writer", (PyCFunction)LoopObject_add_writer, METH_VARARGS, 0},
    {"remove_writer", (PyCFunction)LoopObject_remove_writer, METH_VARARGS, 0},
    {"io_trampoline", (PyCFunction)LoopObject_io_trampoline, METH_VARARGS|METH_KEYWORDS, 0},
    /* {"_set_socket_acceptor", (PyCFunction)LoopObject_set_socket_acceptor, METH_VARARGS|METH_KEYWORDS, 0}, */

    // timer
    {"set_timeout", (PyCFunction)LoopObject_set_timeout, METH_VARARGS|METH_KEYWORDS, 0},
    {"sleep", (PyCFunction)LoopObject_sleep, METH_VARARGS|METH_KEYWORDS, 0},

    // greenlet 
    {"run_on_main_task", (PyCFunction)LoopObject_run_on_main_task, METH_VARARGS, 0},
    
    // async dns
    {"_getaddrinfo", (PyCFunction)SocketMod_getaddrinfo, METH_VARARGS|METH_KEYWORDS, 0},
    {"_getnameinfo", (PyCFunction)SocketMod_getnameinfo, METH_VARARGS, 0},
    {"_gethostbyaddr", (PyCFunction)SocketMod_gethostbyaddr, METH_VARARGS, 0},
    {"_gethostbyname", (PyCFunction)SocketMod_gethostbyname, METH_VARARGS, 0},
    {"_gethostbyname_ex", (PyCFunction)SocketMod_gethostbyname_ex, METH_VARARGS, 0},
    
    // channel
    {"make_channel", (PyCFunction)LoopObject_make_channel, METH_VARARGS, 0},

    {NULL, NULL}
};

static PyMemberDef LoopObject_members[] = {
    {"greenlet", T_OBJECT_EX, offsetof(LoopObject, greenlet), READONLY, "loop greenlet"},
    {"running", T_BOOL, offsetof(LoopObject, state), READONLY, "loop running"},
    {NULL}  /* Sentinel */
};

PyTypeObject LoopObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                              /* ob_size */
#endif
    MODULE_NAME ".AbstractEventLoop",             /*tp_name*/
    sizeof(LoopObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)LoopObject_dealloc, /*tp_dealloc*/
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
    "loop",           /* tp_doc */
    (traverseproc)LoopObject_traverse,               /* tp_traverse */
    (inquiry)LoopObject_clear,               /* tp_clear */
    0,               /* tp_richcompare */
    offsetof(LoopObject, weakreflist),               /* tp_weaklistoffset */
    0,               /* tp_iter */
    0,                          /* tp_iternext */
    LoopObject_methods,          /* tp_methods */
    LoopObject_members,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)LoopObject_init,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
};



static void
ValueObject_dealloc(LoopObject *self)
{
    GDEBUG("self:%p", self);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

PyTypeObject ValueObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                              /* ob_size */
#endif
    MODULE_NAME ".SwitchValue",             /*tp_name*/
    sizeof(ValueObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)ValueObject_dealloc, /*tp_dealloc*/
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
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "",           /* tp_doc */
    0,               /* tp_traverse */
    0,               /* tp_clear */
    0,               /* tp_richcompare */
    0,               /* tp_weaklistoffset */
    0,               /* tp_iter */
    0,                          /* tp_iternext */
    0,          /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
};


static void
fork_child(void)
{
    LoopObject *loop;

    RDEBUG("************ fork ***************");
    //reset_all();
    loop = get_event_loop();
    if (loop == NULL) {
        return;
    }

    loop->postfork = 1;
    Py_DECREF(loop);
}


int
init_loop_module(void)
{
    /* int ret; */
    PyObject *v = NULL;

    system_errors = Py_BuildValue("OOOO", PyExc_KeyboardInterrupt, PyExc_SystemExit, PyExc_SystemError, PyExc_AssertionError);
    if (system_errors == NULL) {
        return -1;
    }

    PyOS_setsig(SIGPIPE, sigpipe_cb);
    PyOS_setsig(SIGINT, sigint_cb);
    PyOS_setsig(SIGTERM, sigint_cb);
    DEBUG("set signals");

    ValueObjectType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&ValueObjectType) < 0) {
        return -1;
    }
    Py_INCREF(&ValueObjectType);

    v = PyObject_CallFunctionObjArgs((PyObject*)&ValueObjectType, NULL);
    if (v == NULL) {
        return -1;
    }
    loop_switch_value = Py_BuildValue("(O)", v);
    if (!loop_switch_value) {
        Py_DECREF(v);
        return -1;
    }
    DEBUG("switch_value:%p", v);
    DEBUG("loop_switch_value:%p", loop_switch_value);
    /* Py_INCREF(v); */
    /* Py_INCREF(loop_switch_value); */

    pthread_atfork(NULL, NULL, fork_child);
    return 1;
}
