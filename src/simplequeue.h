#ifndef SIMPLE_QUEUE_H
#define SIMPLE_QUEUE_H

#include "jega.h"

typedef struct {
    void *val;
    void *prev;
    void *next;
} node_t;

typedef struct {
    node_t *first;
    node_t *tail;
    int size;
} queue_t;

static __inline__ queue_t*
create_queue(void)
{
    queue_t *q;
    q = (queue_t *)PyMem_Malloc(sizeof(queue_t));
    if (q == NULL) {
        return NULL;
    }
    memset(q, 0x0, sizeof(q));
    return q ;
}

/*
static __inline__ void
queue_push_raw(queue_t *q, void *val)
{
    node_t *n = (node_t *)PyMem_Malloc(sizeof(node_t));
    n->prev = NULL;
    n->next = NULL;
    n->val = val;

    if(q->first == NULL){
        q->first = n;
        q->tail = n;
    }else{
        node_t *t = q->tail;
        n->prev = t;
        t->next = n;
        q->tail = n;
    }
    q->size++;
}
*/

static __inline__ void
queue_push(queue_t *q, PyObject *val)
{
    node_t *n = (node_t *)PyMem_Malloc(sizeof(node_t));
    if (n == NULL) {
        return;
    }
    n->prev = NULL;
    n->next = NULL;
    n->val = val;

    if(q->first == NULL){
        q->first = n;
        q->tail = n;
    }else{
        node_t *t = q->tail;
        n->prev = t;
        t->next = n;
        q->tail = n;
    }
    Py_XINCREF(val);
    q->size++;
}

/*
static __inline__ void*
queue_shift_raw(queue_t *q)
{
    node_t *f;
    void *val = NULL;

    if(q->first == NULL){
        //queue_t size = 0
        return NULL;
    }
    f = q->first;
    if(q->first == q->tail){
        q->first = NULL;
        q->tail = NULL;
    }else{
        q->first = f->next;
    }
    q->size--;
    val = f->val;
    PyMem_Free(f);
    return val;
}
*/

static __inline__ PyObject* 
queue_shift(queue_t *q)
{
    node_t *f;
    PyObject *val = NULL;

    if(q->first == NULL){
        //queue_t size = 0
        return NULL;
    }
    f = q->first;
    if(q->first == q->tail){
        q->first = NULL;
        q->tail = NULL;
    }else{
        q->first = f->next;
    }
    q->size--;
    val = f->val;
    PyMem_Free(f);
    Py_XDECREF(val);
    return val;
}

static __inline__ PyObject* 
queue_peek(queue_t *q)
{

    if(q->first == NULL){
        //queue_t size = 0
        return NULL;
    }
    return q->first->val;
}
/*
static __inline__ void*
queue_pop_raw(queue_t *q)
{
    node_t *t;
    void *val = NULL;

    if(q->tail == NULL){
        //queue_t size = 0
        return NULL;
    }
    t = q->tail;
    if(q->first == q->tail){
        q->first = NULL;
        q->tail = NULL;
    }else{
        q->tail = t->prev;
    }
    q->size--;
    val = t->val;
    PyMem_Free(t);
    return val;
}*/

static __inline__ PyObject* 
queue_pop(queue_t *q)
{
    node_t *t;
    PyObject *val = NULL;

    if(q->tail == NULL){
        //queue_t size = 0
        return NULL;
    }
    t = q->tail;
    if(q->first == q->tail){
        q->first = NULL;
        q->tail = NULL;
    }else{
        q->tail = t->prev;
    }
    q->size--;
    val = t->val;
    PyMem_Free(t);
    Py_XDECREF(val);
    return val;
}

static __inline__ void
destroy_queue(queue_t *q)
{
    PyObject *o;
    while(q->size > 0){
        o = queue_shift(q);
        //free???
    }
    PyMem_Free(q);
}

#endif
