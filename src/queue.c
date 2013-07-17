#include "queue.h"

int queue_push(queue_t *q, void *o);

queue_t*
init_queue(uint32_t maxsize)
{
    queue_t *q = PyMem_Malloc(sizeof(queue_t));
    if (q == NULL) {
        return NULL;
    }
    q->q = PyMem_Malloc(sizeof(void*) * (maxsize + 1));
    if (q->q == NULL) {
        PyMem_Free(q);
        return NULL;
    }
    q->max = maxsize;
    q->first = 0;
    q->last = maxsize -1;
    q->size = 0;
    return q;
}

static void
reindex_queue(queue_t *q, uint32_t old_first, uint32_t old_last, uint32_t old_max, uint32_t old_size)
{
    void *x = NULL;
    uint32_t i = old_first; 
    void* old_heap[old_size];
    
    memcpy(old_heap, q->q, sizeof(void*) * old_size);

    while (i != old_last) {
        x = old_heap[i];
        i = (i + 1) % old_max;
        /* YDEBUG("shift:%d:%p", i, x); */
        q->last = (q->last + 1) % q->max;
        q->q[ q->last ] = x;    
    }

    x = old_heap[i];
    /* YDEBUG("shift:%d:%p", i, x); */
    q->last = (q->last + 1) % q->max;
    q->q[ q->last ] = x;    
}

static int
realloc_queue(queue_t *q)
{
    void **new_heap = NULL;
    uint32_t maxsize = 0;
    uint32_t old_first, old_last, old_max, old_size;

    if (q->size >= q->max) {
        /* debug_queue(q); */
        //realloc
        old_first = q->first;
        old_last = q->last;
        old_max = q->max;
        old_size = q->size;

        RDEBUG("realloc:%p", q);

        maxsize = q->max * 2;
        new_heap = (void**)PyMem_Realloc(q->q, sizeof(void*) * (maxsize + 1));
        if (new_heap == NULL) {
            PyErr_SetString(PyExc_Exception, "size over queue");
            return -1;
        }
        q->max = maxsize;
        q->first = 0;
        q->last = q->max -1;
        q->q = new_heap;
        YDEBUG("realloc max:%d", q->max);
        reindex_queue(q, old_first, old_last, old_max, old_size);
        /* debug_queue(q); */
    }
    return 1;
}

int
queue_push(queue_t *q, void *o)
{
    /* DEBUG("queue:%p size:%d o:%p", q, q->size, o); */
    if (realloc_queue(q) == -1) {
        return -1;
    }
    q->last = (q->last + 1) % q->max;
    q->q[ q->last ] = o;    
    q->size++;
    return 1;
}

int
queue_push_noext(queue_t *q, void *o)
{
    /* DEBUG("queue:%p size:%d o:%p", q, q->size, o); */
    if (q->size >= q->max) {
        return -1;
    }
    q->last = (q->last + 1) % q->max;
    q->q[ q->last ] = o;    
    q->size++;
    return 1;
}

void *
queue_shift(queue_t *q)
{
    void *o = NULL;

    /* DEBUG("queue:%p size:%d", q, q->size); */

    if (q->size <= 0) {
        DEBUG("queue is empty:%p", q);
        return NULL;
    } else {
        o = q->q[ q->first ];
        q->first = (q->first + 1) % q->max;
        q->size--;
    }

    return o;
}

void
debug_queue(queue_t *q)
{
    uint32_t i = 0;

    i = q->first; 
    
    while (i != q->last) {
        YDEBUG("entry:%d:%p", i, q->q[i]);
        i = (i + 1) % q->size;
    }

    YDEBUG("entry:%d:%p", i, q->q[i]);
}

void 
destroy_queue(queue_t *q)
{
    DEBUG("destroy:%p", q);
    PyMem_Free(q->q);
    PyMem_Free(q);
}
