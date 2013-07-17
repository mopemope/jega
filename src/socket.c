#include "socket.h"
#include "lookup.h"

#include "ares_setup.h"
#include "inet_ntop.h"
#include "inet_net_pton.h"


/* exceptions */
static PyObject *socket_error = NULL;
PyObject *socket_gaierror = NULL;
static PyObject *socket_timeout = NULL;

static int defaulttimeout = -1; /* Default timeout for new sockets */

static int
configure_sock(int fd)
{
    int on = 1, r;
    r = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    if (r == -1) {
        PyErr_SetFromErrno(socket_error);
        return -1;
    }
    r = fcntl(fd, F_SETFL, O_NONBLOCK);
    if (r == -1) {
        PyErr_SetFromErrno(socket_error);
        return -1;
    }
    return 1;
}

static int
setip_inet(LoopObject *loop, int family, PyObject *args, struct sockaddr *sa, int *addrlen)
{
    char *host;
    int port;
    struct sockaddr_in *addr;
    void *sin_addr = NULL;

    if (!PyTuple_Check(args)) {
        PyErr_SetString(PyExc_TypeError, "arg must be tuple");
        return -1;
    }
    if (!PyArg_ParseTuple(args, "eti:getsockaddr", "idna", &host, &port)) {
        return -1;
    }

    DEBUG("self %s:%d", host, port);

    if (host[0] == '\0') {
        host = "0.0.0.0";
    }
    addr = (struct sockaddr_in *)sa;
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    *addrlen = sizeof *addr;

    if (ares_inet_pton(AF_INET, host, &addr->sin_addr.s_addr) > 0) {
        return 1;
    }
    if ((sin_addr = async_getsockaddr(loop, host, AF_INET)) == NULL) {
        return -1;
    }
    memcpy(&addr->sin_addr, sin_addr, sizeof(struct in_addr));
    PyMem_Free(sin_addr);
    return 1;
}

static int
setip_inet6(LoopObject *loop, int family, PyObject *args, struct sockaddr *sa, int *addrlen)
{
    char *host;
    int port, flowinfo = 0, scope_id = 0;
    struct sockaddr_in6 *sa6;
    void *sin_addr = NULL;

    if (!PyTuple_Check(args)) {
        PyErr_SetString(PyExc_TypeError, "arg must be tuple");
        return -1;
    }

    if (!PyArg_ParseTuple(args, "eti|ii:getsockaddr", "idna", &host, &port, &flowinfo, &scope_id)) {
        return -1;
    }

    DEBUG("self %s:%d", host, port);

    if (host[0] == '\0') {
        host = "::1";
    }
    sa6 = (struct sockaddr_in6 *)sa;
    if (ares_inet_pton(AF_INET6, host, &sa6->sin6_addr.s6_addr) > 0) {
        goto fin;
    }
    if ((sin_addr = async_getsockaddr(loop, host, AF_INET)) == NULL) {
        return -1;
    }
    memcpy(&sa6->sin6_addr, sin_addr, sizeof(struct ares_in6_addr));
    PyMem_Free(sin_addr);
fin:
    sa6->sin6_family = family;
    sa6->sin6_port = htons((short)port);
    sa6->sin6_flowinfo = flowinfo;
    sa6->sin6_scope_id = scope_id;
    *addrlen = sizeof *sa6;
    return 1;
}

static int
getsockaddr(LoopObject *loop, int family, PyObject *args, struct sockaddr *addr, int *addrlen)
{
    switch(family) {
        case AF_INET:
            return setip_inet(loop, family, args, addr, addrlen);
        case AF_INET6:
            return setip_inet6(loop, family, args, addr, addrlen);
        default:
            PyErr_SetString(PyExc_ValueError, "not support family");
            return -1;
    }
    return -1;
}

static int
match_proto(int proto, int *socktype)
{
    DEBUG("proto:%d", proto);

    if (proto) {
        switch(proto) {
            case 6:
                *socktype = SOCK_STREAM;
                //DEBUG("match_proto SOCK_STREAM");
                return proto;
            case 17:
                *socktype = SOCK_DGRAM;
                //DEBUG("match_proto SOCK_DGRAM");
                return proto;
            default:
                PyErr_SetString(socket_gaierror, "ai_socktype not supported");
                return -1;
        }
    }
    *socktype = 0;
    return proto;
}

static int
match_socktype(int socktype)
{
    DEBUG("socktype:%d", socktype);

    if (socktype) {

        if ((socktype & SOCK_STREAM) != 0 ||
            (socktype & SOCK_DGRAM) != 0 ||
            (socktype & SOCK_RAW) != 0 ) {
            return socktype;
        } else {
            PyErr_SetString(socket_gaierror, "ai_socktype not supported");
            return -1;
        }
    }
    return socktype;
}

static int
append_addrinfo_result(PyObject *list, int socktype)
{
    PyObject *t = NULL;
    int ret = -1;
    if ((socktype & SOCK_STREAM) != 0) {
        t = Py_BuildValue("ii", SOCK_STREAM, 6);
    } else if ((socktype & SOCK_DGRAM) != 0) {
        t = Py_BuildValue("ii", SOCK_DGRAM, 17);
    } else if ((socktype & SOCK_RAW) != 0) {
        t = Py_BuildValue("ii", SOCK_RAW, 0);
    } else {
        PyErr_SetString(socket_gaierror, "ai_socktype not supported");
        return ret;
    }
    //DEBUG("tuple:%p", t);
    ret = PyList_Append(list, t);
    Py_XDECREF(t);
    return ret;
}

static int
append_iplists(PyObject *list, PyObject *iplist, int check)
{
    PyObject *iter;
    PyObject *item;
    int ret = -1;

    iter = PyObject_GetIter(iplist);
    if (iter == NULL) {
        return ret;
    }
    while((item = PyIter_Next(iter))) {
        if (check) {
            if (PySequence_Contains(list, item) == 1) {
                Py_DECREF(item);
                continue;
            }
        }
        if (PyList_Append(list, item) == -1) {
            Py_DECREF(item);
            goto fin;
        }
        Py_DECREF(item);
    }
    ret = 1;
fin:
    Py_DECREF(iter);
    return ret;
}

static PyObject*
getsocktype_list(int socktype, int proto)
{
    int s;
    PyObject *list;

    list = PyList_New(0);
    if (list == NULL) {
        return NULL;
    }
    if (match_socktype(socktype) < 0) {
        //error
        goto err;
    }
    if (match_proto(proto, &s) < 0) {
        //error
        goto err;
    }

    DEBUG("s:%d socktype:%d proto:%d", s, socktype, proto);

    if (socktype > 0) {
        /* if (s > 0 && socktype != s) { */
            /* //error */
            /* PyErr_SetString(socket_gaierror, "ai_socktype not supported"); */
            /* goto err; */
        /* } else{ */
            /* //return 1 tuple's list */
            /* DEBUG("return 1 tuple's list"); */
            /* if (append_addrinfo_result(list, socktype) < 0) { */
                /* goto err; */
            /* } */
        /* } */
        if (append_addrinfo_result(list, socktype) < 0) {
            goto err;
        }
    } else{
        if (proto > 0) {
            if (append_addrinfo_result(list, s) < 0) {
                goto err;
            }
        } else{
            if (append_addrinfo_result(list, SOCK_STREAM) < 0) {
                goto err;
            }
            if (append_addrinfo_result(list, SOCK_DGRAM) < 0) {
                goto err;
            }
            if (append_addrinfo_result(list, SOCK_RAW) < 0) {
                goto err;
            }
        }
    }
    return list;
err:
    Py_DECREF(list);
    return NULL;
}

static PyObject*
convert_getaddrifo_result(PyObject *hosts, PyObject *socktypes, PyObject *port, int family)
{
    PyObject *hiter = NULL, *siter = NULL;
    PyObject *hitem = NULL, *sitem = NULL;
    PyObject *socktype = NULL, *proto = NULL;
    PyObject *sockaddr = NULL;
    PyObject *result_item = NULL;
    PyObject *result = NULL;
    int ret;

    result = PyList_New(0);
    if (result == NULL) {
        goto fin;;
    }

    hiter = PyObject_GetIter(hosts);
    if (hiter == NULL) {
        Py_DECREF(result);
        result = NULL;
        goto fin;
    }

    while((hitem =  PyIter_Next(hiter))) {
        siter = PyObject_GetIter(socktypes);
        if (siter == NULL) {
            Py_DECREF(hitem);
            goto fin;
        }
        //DEBUG("hitem %p", hitem);
        while((sitem =  PyIter_Next(siter))) {
            sockaddr = Py_BuildValue("OO", hitem, port);
            if (sockaddr == NULL) {
                Py_DECREF(sitem);
                Py_DECREF(siter);
                Py_DECREF(result);
                result = NULL;
                goto fin;
            }

            socktype = PyTuple_GET_ITEM(sitem, 0);
            proto = PyTuple_GET_ITEM(sitem, 1);

            //DEBUG("socktype:%p proto:%p", socktype, proto);
            if (family == AF_UNSPEC) {
                //TODO check AF_INET
                if (is_af_inet_address(hitem)) {
                    family = AF_INET;
                } else{
                    family = AF_INET6;
                }
            }
            result_item = Py_BuildValue("iOOsO", family, socktype, proto, "", sockaddr);
            ret = PyList_Append(result, result_item);

            Py_DECREF(result_item);
            Py_DECREF(sockaddr);

            Py_DECREF(sitem);
            if (ret == -1) {
                Py_DECREF(siter);
                Py_DECREF(result);
                result = NULL;
                goto fin;
            }
        }
        Py_DECREF(siter);
        Py_DECREF(hitem);

    }
    //TODO count check
fin:
    Py_XDECREF(hiter);
    Py_DECREF(hosts);
    Py_DECREF(socktypes);
    return result;
}

static PyObject*
getaddrinfohosts(LoopObject *loop, char *name, int family)
{
    PyObject *ret = NULL;
    PyObject *list = NULL;
    PyObject *iplist = NULL;
    char *namev4 = NULL, *namev6 = NULL;

    //ip list
    list = PyList_New(0);
    if (list == NULL) {
        return NULL;
    }
    DEBUG("name:%s family:%d", name, family);
    //TODO DECREF iplist ???
    if (family == AF_UNSPEC) {
        
        if (name[0] == '\0') {
            namev4 = "0.0.0.0";
        } else {
            namev4 = name;
        }
        
        DEBUG("get ipv4");

        ret = async_gethostbyname_ex(loop, namev4, AF_INET);
        DEBUG("AF_INET4 ret:%p", ret);
        if (ret == NULL) {
            goto err;
        }
        iplist = PyTuple_GET_ITEM(ret, 2);
        if (iplist == NULL) {
            goto err;
        }
        if (append_iplists(list, iplist, 0) == -1) {
            goto err;
        }
        Py_DECREF(ret);

        if (name[0] == '\0') {
            namev6 = "::1";
        } else {
            namev6 = name;
        }

        DEBUG("get ipv6");

        ret = async_gethostbyname_ex(loop, namev6, AF_INET6);
        DEBUG("AF_INET6 ret:%p", ret);
        if (ret != NULL) {
            iplist = PyTuple_GET_ITEM(ret, 2);
            if (iplist == NULL) {
                goto err;
            }
            if (append_iplists(list, iplist, 1) == -1) {
                goto err;
            }
        } else{
            PyErr_Clear();
        }

    } else if (family == AF_INET) {
        if (name[0] == '\0') {
            name = "0.0.0.0";
        }
        ret = async_gethostbyname_ex(loop, name, AF_INET);
        if (ret == NULL) {
            goto err;
        }
        iplist = PyTuple_GET_ITEM(ret, 2);
        if (iplist == NULL) {
            goto err;
        }
        if (append_iplists(list, iplist, 0) == -1) {
            goto err;
        }
    } else if (family == AF_INET6) {
        if (name[0] == '\0') {
            name = "::1";
        }
        ret = async_gethostbyname_ex(loop, name, AF_INET6);
        if (ret == NULL) {
            goto err;
        }
        iplist = PyTuple_GET_ITEM(ret, 2);
        if (iplist == NULL) {
            goto err;
        }
        if (append_iplists(list, iplist, 0) == -1) {
            goto err;
        }
    } else{
        //Not supoort
        PyErr_SetString(socket_error, "Not support family");
        return NULL;
    }
    Py_XDECREF(ret);
    return list;
err:
    DEBUG("Lookup Error Occuerd");
    Py_XDECREF(ret);
    Py_DECREF(list);
    return NULL;
}

static PyObject*
getservbyname_socktype(int *socktype, char *name)
{
    struct servent *s = NULL;
    int sock = *socktype;

    DEBUG("service %s sock %d", name, sock);
    Py_BEGIN_ALLOW_THREADS
    if (sock == 0) {
        s = getservbyname(name, "tcp");
        DEBUG("servent %p", s);
        *socktype = SOCK_STREAM;
        if (s == NULL) {
            s = getservbyname(name, "udp");
            DEBUG("servent %p", s);
            *socktype = SOCK_DGRAM;
        }
    } else if (sock == SOCK_STREAM) {
        s = getservbyname(name, "tcp");
        DEBUG("servent %p", s);
    } else if (sock == SOCK_DGRAM) {
        s = getservbyname(name, "udp");
        DEBUG("servent %p", s);
    } else{
        //Not support
    }
    Py_END_ALLOW_THREADS
    if (s == NULL) {
        //TODO set error
        PyErr_SetString(socket_error, "service/proto not found");
        return NULL;
    }
    DEBUG("get port %ld", (long) ntohs(s->s_port));
    return PyLong_FromLong((long) ntohs(s->s_port));
}

static PyObject*
getaddrinfo_result(LoopObject *loop, char *name, PyObject *port, int family, int socktype, int proto)
{
    PyObject *socktypes = NULL, *hosts = NULL;

    DEBUG("name:%s family:%d socktype:%d proto:%d", name, family, socktype, proto);

    socktypes = getsocktype_list(socktype, proto);
    if (socktypes == NULL) {
        //error
        return NULL;
    }

    //gethostbyname
    hosts = getaddrinfohosts(loop, name, family);
    if (hosts == NULL) {
        //error
        Py_DECREF(socktypes);
        return NULL;
    }

    return convert_getaddrifo_result(hosts, socktypes, port, family);

}

static PyObject*
call_origin_getaddrinfo(PyObject *args)
{
    DEBUG("%p", args);

    PyObject *sys_modules = PySys_GetObject("modules");
    PyObject *m = PyDict_GetItemString(sys_modules, "socket");
    if (m == NULL) {
        m = PyImport_ImportModule("socket");
        if (m == NULL) {
            return NULL;
        }
    }
    PyObject *md = PyModule_GetDict(m);
    if (!md) {
        return NULL;
    }
    PyObject *f = PyDict_GetItemString(md, "getaddrinfo");
    //DEBUG("%s", o->ob_type->tp_name);
    if (!f) {
        return NULL;
    }
    //DEBUG("%s", f->ob_type->tp_name);
    return PyObject_Call(f, args, NULL);
}

PyObject*
SocketMod_getaddrinfo(LoopObject *loop, PyObject *args, PyObject *kwargs)
{
    PyObject *hostObj, *portObj, *idna = NULL;
    int family = 0, socktype = 0, proto = 0, flags = 0;
    char *host = NULL, *port = NULL;

    static char *keywords[] = {"host", "port", "family", "type", "proto", "flags", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|iiii:getaddrinfo", keywords,
                &hostObj, &portObj, &family, &socktype, &proto, &flags)) {
        return NULL;
    }

    DEBUG("host:%p port:%p", hostObj, portObj);

    if (hostObj == Py_None) {
        host = NULL;
    } else if (PyUnicode_Check(hostObj)) {
        idna = PyObject_CallMethod(hostObj, "encode", "s", "idna");
        if (!idna) {
            return NULL;
        }
        host = PyBytes_AsString(idna);
    } else if (PyBytes_Check(hostObj)) {
        host = PyBytes_AsString(hostObj);
    } else{
        PyErr_SetString(PyExc_TypeError, "getaddrinfo() argument 1 must be string or None");
        return NULL;
    }

#ifdef PY3
    if (PyLong_Check(portObj)) {
#else
    if (PyInt_Check(portObj)) {
#endif  
        //intport = PyInt_AsLong(portObj);
        //if (intport == -1 && PyErr_Occurred()) {
        //    goto err;
        //}
        //PyOS_snprintf(temp, sizeof(temp), "%ld", intport);
        //port = temp;
    } else if (PyBytes_Check(portObj)) {
        port = PyBytes_AsString(portObj);
    } else if (portObj == Py_None) {
        port = (char *)NULL;
        portObj = PyLong_FromLong((long)0);
    } else{
        PyErr_SetString(socket_error, "Int or String expected");
        goto err;
    }

    if (host == NULL || flags & AI_NUMERICHOST) {
        return call_origin_getaddrinfo(args);
    }

    if (port != NULL) {
        DEBUG("name %s port %s", host, port);
        portObj = getservbyname_socktype(&socktype, port);
        if (portObj == NULL) {
            goto err;
        }
    }
    return getaddrinfo_result(loop, host, portObj, family, socktype, proto);
 err:
    Py_XDECREF(idna);
    return (PyObject *)NULL;
}

PyDoc_STRVAR(getaddrinfo_doc, "");

PyObject*
SocketMod_getnameinfo(LoopObject *loop, PyObject *args)
{
    PyObject *sa = NULL;
    int addrlen = 0, flags, port, flowinfo, scope_id;
    char *hostp;
    struct sockaddr_in6 sa6;

    if (!PyArg_ParseTuple(args, "Oi:getnameinfo", &sa, &flags)) {
        return NULL;
    }

    DEBUG("loop:%p", loop);

    if (!PyTuple_Check(sa)) {
        PyErr_SetString(PyExc_TypeError, "getnameinfo() argument 1 must be a tuple");
        return NULL;
    }

    if (!PyArg_ParseTuple(sa, "si|ii", &hostp, &port, &flowinfo, &scope_id)) {
        return NULL;
    }

    addrlen = make_nameinfo_sockaddr(hostp, port, flowinfo, scope_id, &sa6);
    if (addrlen < 0) {
        PyErr_SetString(socket_gaierror, "ip address is invalid");
        return NULL;
    }
    return async_getnameinfo_ex(loop, &sa6, addrlen, flags);
}

PyDoc_STRVAR(getnameinfo_doc,"");

PyObject*
SocketMod_gethostbyaddr(LoopObject *loop, PyObject *args)
{
    char *ip;
    PyObject *addrobj = NULL, *temp = NULL;
    PyObject *latin_item = NULL;
    char *tempip;

    if (!PyArg_ParseTuple(args, "s:gethostbyaddr", &ip)) {
        return NULL;
    }

    DEBUG("loop:%p", loop);

    addrobj = async_gethostbyaddr(loop, ip);

    if (addrobj == NULL && PyErr_Occurred()) {
        return NULL;
    } else if (addrobj == NULL) {

        //hostname
        DEBUG("hostname:%s", ip);
        temp = async_gethostbyname(loop, ip, AF_UNSPEC);
        if (temp == NULL) {
            return NULL;
        }

#ifdef PY3
        latin_item = PyUnicode_AsLatin1String(temp);
        tempip = PyBytes_AsString(latin_item);
#else
        tempip = PyBytes_AsString(temp);
#endif
        DEBUG("temp ip:%s", tempip);

        addrobj = async_gethostbyaddr(loop, tempip);
        
        Py_XDECREF(latin_item);
        Py_XDECREF(temp);
    }
    return addrobj;
}

PyDoc_STRVAR(gethostbyaddr_doc,"");

static PyObject*
internal_gethostbyname(LoopObject *loop, char *name, int family)
{
    return async_gethostbyname(loop, name, family);
}

PyObject*
SocketMod_gethostbyname(LoopObject *loop, PyObject *args)
{
    char *name;
    if (!PyArg_ParseTuple(args, "s:gethostbyname", &name)) {
        return NULL;
    }
    DEBUG("loop:%p", loop);
    return internal_gethostbyname(loop, name, AF_INET);
}

PyDoc_STRVAR(gethostbyname_doc,"");

static PyObject*
internal_gethostbyname_ex(LoopObject *loop, char *name, int family)
{
    DEBUG("name:%s family:%d", name, family);
    return async_gethostbyname_ex(loop, name, family);
}

PyObject*
SocketMod_gethostbyname_ex(LoopObject *loop, PyObject *args)
{
    char *name;

    if (!PyArg_ParseTuple(args, "s:gethostbyname_ex", &name)) {
        return NULL;
    }
    return internal_gethostbyname_ex(loop, name, AF_INET);
}

PyDoc_STRVAR(gethostbyname_ex_doc,"");

int
setup_socket_mod(void)
{
    PyObject *sys_modules = PySys_GetObject("modules");
    PyObject *m = PyDict_GetItemString(sys_modules, "socket");
    if (m == NULL) {
        m = PyImport_ImportModule("socket");
        if (m == NULL) {
            return -1;
        }
    }
    PyObject *md = PyModule_GetDict(m);
    if (!md) {
        return -1;
    }
    socket_error = PyDict_GetItemString(md, "error");
    socket_gaierror = PyDict_GetItemString(md, "gaierror");
    socket_timeout = PyDict_GetItemString(md, "timeout");
    return 1;
}
