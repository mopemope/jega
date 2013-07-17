#include "util.h"


/* static char hex[] = "0123456789ABCDEF"; */

static int
hex2int(int i)
{
    i = toupper(i);
    i = i - '0';
    if(i > 9){ 
        i = i - 'A' + '9' + 1;
    }
    return i;
}

int
urldecode(char *buf, int len)
{
    int c, c1;
    char *s0, *t;
    t = s0 = buf;
    while(len > 0){
        c = *buf++;
        if(c == '%' && len > 2){
            c = *buf++;
            c1 = c;
            c = *buf++;
            c = hex2int(c1) * 16 + hex2int(c);
            len -= 2;
        }
        *t++ = c;
        len--;
    }
    *t = 0;
    return t - s0;
}

PyObject*
save_exception(void)
{
    PyObject *t = NULL, *v = NULL, *tr = NULL, *o = NULL;

    PyErr_Fetch(&t, &v, &tr);
    PyErr_Clear();
    if (v == NULL) {
        v = Py_None;
        Py_INCREF(v);
    }
    if (tr == NULL) {
        tr = Py_None;
        Py_INCREF(tr);
    }
    o = Py_BuildValue("OOO", t, v, tr);
    DEBUG("exception :%p", o);
    return o;
}

int
raise_exception(PyObject *args)
{
    PyObject *typ = NULL;
    PyObject *val = NULL;
    PyObject *tb = NULL;

    if (!PyArg_ParseTuple(args, "|OOO:throw", &typ, &val, &tb)) {
        return -1;
    }

    if (tb == Py_None) {
        tb = NULL;
    } else if (tb != NULL && !PyTraceBack_Check(tb)) {
        PyErr_SetString(PyExc_TypeError, "third argument must be a traceback object");
        return -1;
    }

    Py_INCREF(typ);
    Py_XINCREF(val);
    Py_XINCREF(tb);

    if (PyExceptionClass_Check(typ)) {
        PyErr_NormalizeException(&typ, &val, &tb);
    } else if (PyExceptionInstance_Check(typ)) {
        /* Raising an instance. The value should be a dummy. */
        if (val && val != Py_None) {
            PyErr_SetString( PyExc_TypeError, "instance exception may not have a separate value");
            goto failed_throw;
        } else {
            /* Normalize to raise <class>, <instance> */
            Py_XDECREF(val);
            val = typ;
            typ = PyExceptionInstance_Class(typ);
            Py_INCREF(typ);
        }
    } else {
        /* Not something you can raise. throw() fails. */
        PyErr_Format( PyExc_TypeError, "exceptions must be classes, or instances, not %s", Py_TYPE(typ)->tp_name);
        goto failed_throw;
    }

    PyErr_Restore(typ, val, tb);
    return 1;
 failed_throw:
    /* Didn't use our arguments, so restore their original refcounts */
    Py_DECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
    return -1;
}

PyObject*
get_exception(PyObject *args)
{
    PyObject *typ = NULL;
    PyObject *val = NULL;
    PyObject *tb = NULL;
    PyObject *o = NULL;

    if (!PyArg_ParseTuple(args, "|OOO:get_exception", &typ, &val, &tb)) {
        return NULL;
    }

    if (typ == NULL || typ == Py_None) {
        PyErr_SetString(PyExc_TypeError, "type is requried");
        return NULL;
    }
    
    if (val == NULL) {
        val = Py_None;
    }
    o = PyObject_CallFunction(typ, "O", val);
    return o;
}

int
catch_exception(PyObject *err)
{
    if (PyErr_Occurred()) {
        if (PyErr_ExceptionMatches(err)) {
            DEBUG("catch exception exc:%p", err);
            PyErr_Clear();
            return 1;
        }
    }
    return 0;
}

PyObject*
call_method(PyObject *self, const char *name)
{
    PyObject *m, *res;;

    m = PyObject_GetAttrString(self, name);
    if (m == NULL) {
        RDEBUG("method missing self:%p '%s'", self, name);
        return NULL;
    }
    DEBUG("call method self:%p '%s'", self, name);
    res = PyEval_CallObjectWithKeywords(m, NULL, NULL);
    Py_DECREF(m);
    return res;
}

PyObject*
call_method_args1(PyObject *self, const char *name, PyObject *o)
{
    PyObject *m, *res, *args = NULL;

    m = PyObject_GetAttrString((PyObject*)self, name);
    if (m == NULL) {
        RDEBUG("method missing self:%p '%s'", self, name);
        return NULL;
    }

    if ((args = PyTuple_New(1)) == NULL) {
        Py_DECREF(m);
    }
    Py_XINCREF(o);
    PyTuple_SET_ITEM(args, 0, o);

    DEBUG("call method self:%p '%s'", self, name);
    res = PyEval_CallObjectWithKeywords(m, args, NULL);
    Py_DECREF(m);
    Py_DECREF(args);
    return res;
}

int
remove_from_list(PyListObject *self, PyObject *v)
{
    Py_ssize_t i;

    for (i = 0; i < Py_SIZE(self); i++) {
        if (self->ob_item[i] == v) {
            DEBUG("find item:%p", self->ob_item[i]);
            if (PySequence_DelItem((PyObject*)self, i) == -1) {
                return -1;
            }
            return 1;
        }
    }
    return 1;
}
