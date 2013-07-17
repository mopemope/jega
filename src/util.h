#ifndef UTIL_H
#define UTIL_H

#include "jega.h"

int
urldecode(char *buf, int len);

static __inline__ int
search_line(char *s, int len)
{
    int i;
    char *p = s;
    for(i = 0; i < len; i++){
        if(*p == '\n'){
            return i;
        }
        (*p)++;
    }
    return -1;
}

PyObject* save_exception(void);

int raise_exception(PyObject *args);

PyObject* get_exception(PyObject *args);

int catch_exception(PyObject *err);

PyObject* call_method(PyObject *self, const char *name);

PyObject* call_method_args1(PyObject *self, const char *name, PyObject *o);

int remove_from_list(PyListObject *self, PyObject *v);

#endif

