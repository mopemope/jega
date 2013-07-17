#include "local.h"
#include "greensupport.h"

#define LOCAL_MOD_NAME "local"

static PyObject *dict_key;

PyObject*
get_thread_local(PyObject *current)
{
    PyObject *dict = NULL;

    if (PyObject_HasAttr(current, dict_key) == 0) {
        dict = PyDict_New();
        if (dict == NULL) {
            return NULL;
        }
        if(PyObject_SetAttr(current, dict_key, dict) == -1) {
            Py_DECREF(dict);
            return NULL;
        }
    }
    
    return PyObject_GetAttr(current, dict_key);
}

static PyObject*
get_local_dict(void)
{
    PyObject *current;
    PyObject *dict = NULL;

    current = greenlet_getcurrent();
    Py_XDECREF(current);
    if (current == NULL) {
        return NULL;
    }

    if (PyObject_HasAttr(current, dict_key) == 0) {
        dict = PyDict_New();
        if (dict == NULL) {
            return NULL;
        }
        if(PyObject_SetAttr(current, dict_key, dict) == -1) {
            Py_DECREF(dict);
            return NULL;
        }
    }
    
    return PyObject_GetAttr(current, dict_key);
}

static PyObject*
LocalObject_getattribute(PyObject *self, PyObject *attr)
{
    PyObject *local;
    
    DEBUG("self:%p attr:%p", self, attr);

    local = get_local_dict();
    if (local == NULL) {
        return NULL;
    }
    
    return PyDict_GetItem(local, attr);
}

static int 
LocalObject_setattribute(PyObject *self, PyObject *attr, PyObject *value)
{
    PyObject *local;
    
    DEBUG("self:%p attr:%p value:%p", self, attr, value);

    local = get_local_dict();
    DEBUG("local:%p", local);

    if (local == NULL) {
        return -1;
    }
    if (value) {
        if (PyDict_SetItem(local, attr, value) == -1) {
            RDEBUG("Error");
            return -1;
        }
    } else {
        if (PyDict_DelItem(local, attr) == -1) {
            return -1;
        }
    }
    return 0;
}

static void
LocalObject_dealloc(LocalObject *self)
{
    GDEBUG("self %p", self);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyMethodDef LocalObject_methods[] = {
    {NULL, NULL}
};

static PyMemberDef LocalObject_members[] = {
    {NULL}  
};

PyTypeObject LocalObjectType = {
#ifdef PY3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                    /* ob_size */
#endif
    MODULE_NAME "." LOCAL_MOD_NAME ".local",             /*tp_name*/
    sizeof(LocalObject), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)LocalObject_dealloc, /*tp_dealloc*/
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
    (getattrofunc)LocalObject_getattribute,                         /*tp_getattro*/
    (setattrofunc)LocalObject_setattribute,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,        /*tp_flags*/
    "Local Object",           /* tp_doc */
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    LocalObject_methods,          /* tp_methods */
    LocalObject_members,        /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                      /* tp_init */
    0,                         /* tp_alloc */
    0,                           /* tp_new */
    0,                           /* tp_del */
};

static PyMethodDef LocalMod_methods[] = {
    {NULL, NULL}           /* sentinel */
};

PyObject* 
init_local_module(PyObject *m)
{
    PyObject *d, *sd, *v;
    PyObject *sys_modules, *module;
    PyMethodDef *ml;
    

#ifdef PY3
    PyObject *mod_name = PyUnicode_FromString(MODULE_NAME "." LOCAL_MOD_NAME);
#else
    PyObject *mod_name = PyBytes_FromString(MODULE_NAME "." LOCAL_MOD_NAME);
#endif
    
    if(mod_name == NULL){
        return NULL;
    }

    LocalObjectType.tp_new = PyType_GenericNew;
    if(PyType_Ready(&LocalObjectType) < 0){
        return NULL;
    }

    sys_modules = PySys_GetObject("modules");
    d = PyModule_GetDict(m);
    module = PyDict_GetItem(d, mod_name);
    if(module == NULL) {
        module = PyModule_New(MODULE_NAME "." LOCAL_MOD_NAME);
        if(module != NULL) {
            PyDict_SetItem(sys_modules, mod_name, module);
            PyModule_AddObject(m, LOCAL_MOD_NAME, module);
        }
    }
    sd = PyModule_GetDict(module);
    for(ml = LocalMod_methods; ml->ml_name != NULL; ml++){
        v = PyCFunction_NewEx(ml, (PyObject *)NULL, mod_name);
        if(v == NULL) {
            goto fin;
        }
        if(PyDict_SetItemString(sd, ml->ml_name, v) != 0){
            Py_DECREF(v);
            return NULL;
        }
        Py_DECREF(v);
    }

fin:
    Py_DECREF(mod_name);

    Py_INCREF(&LocalObjectType);
    PyModule_AddObject(module, "local", (PyObject *)&LocalObjectType);
    
#ifdef PY3
    dict_key = PyUnicode_FromString("_jega_local_dict__");
#else
    dict_key = PyBytes_FromString("_jega_local_dict__");
#endif
    return module;
}
