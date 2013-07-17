#ifndef LOOP_H
#define LOOP_H

#include "jega.h"
#include "picoev.h"
#include "ares.h"

#include "timer.h"
#include "queue.h"
#include "heapq.h"

#define MAX_FD 4096

typedef enum {
    L_STOP = 0,
    // L_SUSPEND,
    L_RUNNING
} loop_state;

typedef struct {
    PyObject_HEAD
    picoev_loop *main_loop; //event loop
    PyObject *greenlet; // loop thread
    uint32_t maxfd;
    heapq_t *scheduled; // heapq
    queue_t *pendings;  // pendings
    uint32_t activecnt; // scheduled count
    uint32_t ioactivecnt; // io scheduled counr
    loop_state state;
    ares_channel channel;
    char postfork;
    PyObject *suspend_thread; // suspend thread
    PyObject *weakreflist;
} LoopObject;

typedef struct {
    PyObject *read_callback;
    PyObject *read_args;
    PyObject *write_callback;
    PyObject *write_args;
    int family;
    int type;
    LoopObject *loop;
} callback_arg_t;

typedef struct {
    PyObject *greenlet;
    PyObject *timeout_exc;
    LoopObject *loop;
} switch_arg_t;

typedef void watchdog_handler(void);

typedef struct {
    PyObject_HEAD

} ValueObject;

extern PyTypeObject LoopObjectType;
extern PyTypeObject ValueObjectType;
extern PyObject *loop_switch_value;

int init_loop_module(void);

LoopObject* get_event_loop(void);

PyObject* schedule_timer(LoopObject *loop, PyObject *timer);

PyObject* loop_switch(LoopObject *loop);

PyObject* loop_schedule_call(LoopObject *loop, long seconds, PyObject *cb, PyObject *args, PyObject *greenlet);

PyObject* loop_set_timeout(LoopObject *loop, long seconds, PyObject *exception);

PyObject* loop_handle_error(LoopObject *loop, PyObject *context);

PyObject* loop_sleep(LoopObject *loop, long time);

int check_interrupted(LoopObject *self);

int io_trampoline(LoopObject *loop, int fd, int event, int timeout, PyObject *exception);

// -------------------------------------------------------------------
// IO Event
// -------------------------------------------------------------------

int add_event(LoopObject *loop, int fd, int event, int timeout, picoev_handler *cb, void *data);

int is_active_event(LoopObject *loop, int fd);

void del_event(LoopObject *loop, int fd);

void update_event_timeout(LoopObject *loop, int fd, int timeout);

// -----------------------------------------
// Loop starter
// -----------------------------------------

int start_loop(LoopObject *loop);


#endif

