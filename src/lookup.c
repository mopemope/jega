#include "lookup.h"
#include "loop.h"
#include "socket.h"
#include "ares.h"
#include "ares_setup.h"
#include "inet_ntop.h"
#include "inet_net_pton.h"
#include "greensupport.h"

#include "picoev.h"

#define CHECK_INTERVAL 1

typedef struct{
    void *result;
    int family;
    PyObject *greenlet;
    LoopObject *loop;
    char retval;
    char called;
} lookup_ctx_t;

typedef struct{
    int flag;
    int ares_flag;
} nameinfo_flag_t;

//TODO loop
/* static ares_channel channel = NULL; */

static nameinfo_flag_t flag_map[] = {
    {NI_NUMERICHOST, ARES_NI_NUMERICHOST},
    {NI_NUMERICSERV, ARES_NI_NUMERICSERV},
    {NI_NOFQDN, ARES_NI_NOFQDN},
    {NI_NAMEREQD, ARES_NI_NAMEREQD},
    {NI_DGRAM, ARES_NI_DGRAM}
};

static int init_channel(LoopObject *loop);

static int dealloc_ctx(lookup_ctx_t *ctx);

static void state_cb(void *data, int fd, int read, int write);

static void process_callback(picoev_loop* loop, int fd, int events, void* cb_arg);

int
init_lookup(LoopObject *loop)
{
    int status;

    status = ares_library_init(ARES_LIB_INIT_ALL);
    if (status != ARES_SUCCESS) {
        PyErr_SetString(PyExc_IOError, ares_strerror(status));
        return -1;
    }
    if (init_channel(loop) == -1) {
        ares_library_cleanup();
        return -1;
    }
    return 1;
}

void
destroy_lookup(LoopObject *loop)
{
    /* ares_cancel(channel); */
    /* ares_destroy(loop->channel); */
    /* ares_library_cleanup(); */
}

static int
init_channel(LoopObject *loop)
{
    int status;
    struct ares_options options;
    int optmask = 0;

    options.sock_state_cb_data = loop;
    options.sock_state_cb = state_cb;
    options.timeout = 3000;
    options.tries = 2;
    optmask |= ARES_OPT_SOCK_STATE_CB;
    optmask |= ARES_OPT_TRIES;
    optmask |= ARES_OPT_TIMEOUTMS;

    status = ares_init_options(&loop->channel, &options, optmask);
    if (status != ARES_SUCCESS) {
        PyErr_SetString(PyExc_IOError, ares_strerror(status));
        return -1;
    }
    
    return 1;
}

int
reset_channel(LoopObject *loop)
{
    /* ares_cancel(channel); */
    ares_library_cleanup();
    return init_lookup(loop);
}

static void
state_cb(void *data, int fd, int read, int write)
{
    int event = 0;
    LoopObject *loop = (LoopObject*)data;

    PDEBUG("loop:%p fd:%d read:%d write:%d", loop, fd, read, write);
    if (read || write) {
        if (!is_active_event(loop, fd)) {
            if (read) {
                event |= PICOEV_READ;
            }
            if (write) {
                event |= PICOEV_WRITE;
            }
            add_event(loop, fd, event, CHECK_INTERVAL, process_callback, loop);
        }

    } else {
        del_event(loop, fd);
    }
    DEBUG("end state_cb");
}

int
convert_nameinfo_flag(int flag)
{
    int nameflg = ARES_NI_LOOKUPHOST|ARES_NI_LOOKUPSERVICE;
    int i, tmp = flag;
    nameinfo_flag_t n;

    for (i = 0; i < 5; i++) {
        n = flag_map[i];
        if (n.flag & flag) {
            nameflg |= n.ares_flag;
            tmp &= ~n.flag;
        }
        if (!tmp) {
            return nameflg;
        }
    }

    PyErr_Format(PyExc_IOError, "Invalid value 0x%x", flag);
    return -1;
}

int
make_nameinfo_sockaddr(char* hostp, int port, int flowinfo, int scope_id, struct sockaddr_in6 *sa6)
{
    struct sockaddr_in *sa = (struct sockaddr_in *)sa6;

    if (ares_inet_pton(AF_INET, hostp, &sa->sin_addr.s_addr) > 0) {
        sa->sin_family = AF_INET;
        sa->sin_port = htons(port);
        return sizeof(struct sockaddr_in);
    }else if (ares_inet_pton(AF_INET6, hostp, &sa6->sin6_addr.s6_addr) > 0) {
        sa6->sin6_family = AF_INET6;
        sa6->sin6_port = htons(port);
        sa6->sin6_flowinfo = flowinfo;
        sa6->sin6_scope_id = scope_id;
        return sizeof(struct sockaddr_in6);
    }
    return -1;
}


static void
process_callback(picoev_loop* main_loop, int fd, int events, void* cb_arg)
{
    LoopObject *loop = (LoopObject*)cb_arg;

    int readfd = ARES_SOCKET_BAD, writefd = ARES_SOCKET_BAD;

    PDEBUG("process_callback loop:%p fd:%d events:%d", loop, fd, events);

    if (unlikely(events & PICOEV_ERR)) {
        RDEBUG("PICOEV ERR closed fd");
        del_event(loop, fd);
        PyErr_SetString(socket_gaierror, "Could not contact DNS servers");
        ares_destroy(loop->channel);
        init_channel(loop);
    }else{
        if (events & PICOEV_READ) {
            readfd = fd;
        }
        if (events & PICOEV_WRITE) {
            writefd = fd;
        }
        update_event_timeout(loop, fd, CHECK_INTERVAL);
    }
    /* PDEBUG("process_callback events:%d fd:%d readfd:%d writefd:%d", events, fd, readfd, writefd); */
    ares_process_fd(loop->channel, readfd, writefd);
}

static void*
wait_lookup(lookup_ctx_t *ctx)
{
    PyObject *ret = NULL;
    void *result = NULL;

    while (!ctx->called && !PyErr_Occurred()) {
        DEBUG("wait....");
        ret = loop_switch(ctx->loop);
        Py_XDECREF(ret);
        if (ret == NULL) {
            break;
        }
    }
    if (!ctx->called && !PyErr_Occurred()) {
        DEBUG("wait....");
        ret = loop_switch(ctx->loop);
        Py_XDECREF(ret);
    }
    DEBUG("finish ctx:%p ctx->greenlet:%p" ,ctx, ctx->greenlet);
    result = ctx->result;
    DEBUG("result:%p", result);
    dealloc_ctx(ctx);
    return result;
}

static lookup_ctx_t*
init_ctx(LoopObject *loop)
{
    lookup_ctx_t *ctx;
    
    ctx = (lookup_ctx_t *)PyMem_Malloc(sizeof(lookup_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }
    memset(ctx, 0, sizeof(lookup_ctx_t));
    ctx->greenlet = greenlet_getcurrent();
    ctx->retval = 1;
    ctx->loop = loop;
    Py_INCREF(ctx->loop);
    GDEBUG("alloc ctx:%p greenlet:%p", ctx, ctx->greenlet);
    return ctx;
}

static int 
dealloc_ctx(lookup_ctx_t *ctx)
{
    Py_DECREF(ctx->loop);
    Py_DECREF(ctx->greenlet);
    GDEBUG("dealloc ctx:%p", ctx);
    PyMem_Free(ctx);
    return 1;
}


static PyObject*
getaddr_inet4(char *ch)
{
    char tmp[sizeof "255.255.255.255"];
    if (ares_inet_ntop(AF_INET, ch, tmp, sizeof(tmp)) == NULL) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }

#ifdef PY3
    return PyUnicode_FromString(tmp);
#else
    return PyBytes_FromString(tmp);
#endif
}

static PyObject*
getaddr_inet6(char *ch)
{
    char tmp[sizeof("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255")];
    if (ares_inet_ntop(AF_INET6, ch, tmp, sizeof(tmp)) == NULL) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }
#ifdef PY3
    return PyUnicode_FromString(tmp);
#else
    return PyBytes_FromString(tmp);
#endif
}

int
is_af_inet_address(PyObject *ip)
{
    char *ch = NULL;
    char tmp[sizeof "255.255.255.255"];
    PyObject *latin_item = NULL;

#ifdef PY3
    latin_item = PyUnicode_AsLatin1String(ip);
    ch = PyBytes_AsString(latin_item);
#else
    ch = PyBytes_AsString(ip);
#endif
    if (ares_inet_ntop(AF_INET, ch, tmp, sizeof(tmp)) == NULL) {
        Py_XDECREF(latin_item);
        return 0;
    }

    Py_XDECREF(latin_item);
    return 1;
}


static PyObject*
gethost_data(struct hostent *host)
{
    char **ch;
    int res;
    PyObject *tpl = NULL, *name_list = NULL, *addr_list = NULL, *tmp = NULL;
    int family;
    
    DEBUG("hostent:%p", host);

    family = host->h_addrtype;

    name_list = PyList_New(0);
    if (name_list == NULL) {
        goto error;
    }
    addr_list = PyList_New(0);
    if (addr_list == NULL) {
        goto error;
    }

    if (host->h_aliases) {
        for (ch = host->h_aliases; *ch != NULL; ch++) {
            if (*ch != host->h_name && strcmp(*ch, host->h_name)) {
                //DEBUG("h_aliases %s", *ch);
#ifdef PY3
                tmp = PyUnicode_FromString(*ch);
#else
                tmp = PyBytes_FromString(*ch);
#endif
                if (tmp == NULL) {
                    goto error;
                }
                res = PyList_Append(name_list, tmp);
                Py_DECREF(tmp);

                if (res == -1) {
                    goto error;
                }
            }
        }
    }

    for (ch = host->h_addr_list; *ch != NULL; ch++) {

        switch (family) {

            case AF_INET:
                tmp = getaddr_inet4(*ch);
                break;
            case AF_INET6:
                tmp = getaddr_inet6(*ch);
                break;

            default:
                PyErr_SetString(PyExc_IOError, "unsupported address family");
                goto error;;
        }
        if (tmp == NULL) {
            goto error;
        }
        res = PyList_Append(addr_list, tmp);
        Py_DECREF(tmp);

        if (res == -1) {
            goto error;
        }
    }

    tpl = Py_BuildValue("sOO", host->h_name, name_list, addr_list);
error:
    Py_XDECREF(name_list);
    Py_XDECREF(addr_list);
    return tpl;
}

static void
gethostbyname_cb(void *arg, int status, int timeouts, struct hostent *host)
{
    char ip[INET6_ADDRSTRLEN];
    lookup_ctx_t *ctx = (lookup_ctx_t *)arg;
    PyObject *ret = NULL;
    
    if (status == ARES_ECANCELLED || status == ARES_EDESTRUCTION) {
        DEBUG("destroy channel:%p", ctx);
        return;
    }
    DEBUG("call gethostbyname_cb ctx:%p greenlet:%p", ctx, ctx->greenlet);
    ctx->called = 1;
    if (!host || status != ARES_SUCCESS) {
        if (!PyErr_Occurred()) {
            RDEBUG("error %s", ares_strerror(status));
            PyErr_SetString(socket_gaierror, ares_strerror(status));
        }
        ctx->retval = -1;
        goto fin;
    }
    inet_ntop(host->h_addrtype, host->h_addr_list[0], ip, INET6_ADDRSTRLEN);

    DEBUG("IP:%s", ip);
#ifdef PY3
    ctx->result = PyUnicode_FromString(ip);
#else
    ctx->result = PyBytes_FromString(ip);
#endif
fin:
    ret = greenlet_switch(ctx->greenlet, NULL, NULL);
    Py_XDECREF(ret);
}

static void
gethostbyname_ex_cb(void *arg, int status, int timeouts, struct hostent *host)
{
    PyObject *ret;

    lookup_ctx_t *ctx = (lookup_ctx_t *)arg;
   
    if (status == ARES_ECANCELLED || status == ARES_EDESTRUCTION) {
        PDEBUG("destroyed channel:%p", ctx);
        return;
    }

    if (ctx->greenlet == NULL) {
        RDEBUG("***");
        return;
    }

    DEBUG("call gethostbyname_ex_cb ctx:%p greenlet:%p called:%d", ctx, ctx->greenlet, ctx->called);
    ctx->called = 1;

    if (!host || status != ARES_SUCCESS) {
        /* RDEBUG("error %s", ares_strerror(status)); */
        if (!PyErr_Occurred()) {
            RDEBUG("error %s", ares_strerror(status));
            PyErr_SetString(socket_gaierror, ares_strerror(status));
        }
        ctx->retval = -1;
        goto fin;
    }

    ctx->result = gethost_data(host);
fin:
    ret = greenlet_switch(ctx->greenlet, NULL, NULL);
    Py_XDECREF(ret);
}

static void
getnameinfo_cb(void *arg, int status, int timeouts, char *name, char *service)
{
    PyObject *ret;

    lookup_ctx_t *ctx = (lookup_ctx_t *)arg;

    if (status == ARES_ECANCELLED || status == ARES_EDESTRUCTION) {
        DEBUG("destroy channel:%p", ctx);
        return;
    }
    DEBUG("call getnameinfo_cb ctx:%p greenlet:%p", ctx, ctx->greenlet);
    ctx->called = 1;
    if (!name || status != ARES_SUCCESS) {
        if (!PyErr_Occurred()) {
            RDEBUG("error %s", ares_strerror(status));
            PyErr_SetString(socket_gaierror, ares_strerror(status));
        }
        ctx->retval = -1;
        goto fin;
    }


#ifdef PY3
    ctx->result = PyUnicode_FromString(name);
#else
    ctx->result = PyBytes_FromString(name);
#endif
fin:
    ret = greenlet_switch(ctx->greenlet, NULL, NULL);
    Py_XDECREF(ret);
}

static void
getnameinfo_ex_cb(void *arg, int status, int timeouts, char *name, char *service)
{
    PyObject *ret;

    lookup_ctx_t *ctx = (lookup_ctx_t *)arg;

    if (status == ARES_ECANCELLED || status == ARES_EDESTRUCTION) {
        DEBUG("destroy channel:%p", ctx);
        return;
    }
    DEBUG("call getnameinfo_ex_cb ctx:%p greenlet:%p", ctx, ctx->greenlet);
    ctx->called = 1;

    if (!name || status != ARES_SUCCESS) {
        if (!PyErr_Occurred()) {
            RDEBUG("error %s", ares_strerror(status));
            PyErr_SetString(socket_gaierror, ares_strerror(status));
        }
        ctx->retval = -1;
        goto fin;
    }

    ctx->result = Py_BuildValue("ss", name, service);
fin:
    ret = greenlet_switch(ctx->greenlet, NULL, NULL);
    Py_XDECREF(ret);
}

static void
getsockaddr_cb(void *arg, int status, int timeouts, struct hostent *host)
{
    void *data;
    PyObject *ret;

    lookup_ctx_t *ctx = (lookup_ctx_t *)arg;
    if (status == ARES_ECANCELLED || status == ARES_EDESTRUCTION) {
        DEBUG("destroy channel:%p", ctx);
        return;
    }

    DEBUG("call getsockaddr_cb ctx:%p greenlet:%p", ctx, ctx->greenlet);
    ctx->called = 1;

    if (!host || status != ARES_SUCCESS) {
        if (!PyErr_Occurred()) {
            RDEBUG("error %s", ares_strerror(status));
            PyErr_SetString(socket_gaierror, ares_strerror(status));
        }
        ctx->retval = -1;
        goto fin;
    }

    switch(ctx->family) {
        case AF_INET:
            data = PyMem_Malloc(host->h_length);
            memcpy(data, host->h_addr_list[0], host->h_length);
            ctx->result = data;
            break;
        case AF_INET6:
            data = PyMem_Malloc(host->h_length);
            memcpy(data, host->h_addr_list[0], host->h_length);
            //memcpy(&sa6->sin6_addr, host->h_addr_list[0], host->h_length);
            ctx->result = data;
            break;
    }

    /*
    char ip[INET6_ADDRSTRLEN];
    int i = 0;

    DEBUG("*******");
    for (i = 0; host->h_addr_list[i]; ++i) {
        inet_ntop(host->h_addrtype, host->h_addr_list[i], ip, sizeof(ip));
        printf("Found %s\n", ip);
    }
    DEBUG("*******");
    */
fin:
    ret = greenlet_switch(ctx->greenlet, NULL, NULL);
    Py_XDECREF(ret);
}

static void
gethostbyaddr_cb(void *arg, int status, int timeouts, struct hostent *host)
{
    PyObject *ret;

    lookup_ctx_t *ctx = (lookup_ctx_t *)arg;

    if (status == ARES_ECANCELLED || status == ARES_EDESTRUCTION) {
        DEBUG("destroy channel:%p", ctx);
        return;
    }
    DEBUG("call gethostbyaddr_cb ctx:%p greenlet:%p", ctx, ctx->greenlet);
    ctx->called = 1;

    if (!host || status != ARES_SUCCESS) {
        if (!PyErr_Occurred()) {
            RDEBUG("error %s", ares_strerror(status));
            PyErr_SetString(socket_gaierror, ares_strerror(status));
        }
        ctx->retval = -1;
        goto fin;
    }

    ctx->result = gethost_data(host);
fin:
    ret = greenlet_switch(ctx->greenlet, NULL, NULL);
    Py_XDECREF(ret);
}


PyObject*
async_gethostbyname(LoopObject *loop, char *name, int family)
{
    lookup_ctx_t *ctx = init_ctx(loop);
    if (ctx == NULL) {
        //Error
        return NULL;
    }
    //DEBUG("start");
    ares_gethostbyname(loop->channel, name, family, gethostbyname_cb, ctx);
    //DEBUG("end");
    return (PyObject*)wait_lookup(ctx);
}

PyObject*
async_gethostbyname_ex(LoopObject *loop, char *name, int family)
{
    lookup_ctx_t *ctx = init_ctx(loop);
    if (ctx == NULL) {
        //Error
        return NULL;
    }
    //DEBUG("start");
    ares_gethostbyname(loop->channel, name, family, gethostbyname_ex_cb, ctx);
    //DEBUG("end");
    return (PyObject*)wait_lookup(ctx);
}

PyObject*
async_gethostbyaddr(LoopObject *loop, char *ip)
{
    char addr[INET6_ADDRSTRLEN];
    int addr_len;
    int family;
    lookup_ctx_t *ctx = init_ctx(loop);
    if (ctx == NULL) {
        //Error
        return NULL;
    }

    if (ares_inet_pton(AF_INET, ip, addr) > 0) {
        family = AF_INET;
        addr_len = 4;
        //DEBUG("AF_INET");
    }else if (ares_inet_pton(AF_INET6, ip, addr) > 0) {
        family = AF_INET6;
        addr_len = 16;
        //DEBUG("AF_INET6");
    }else {
        //hostbyname
        DEBUG("Not ip addr, try gethostbyname %s", ip);
        PyMem_Free(ctx);
        return NULL;
    }
    //DEBUG("start");
    ares_gethostbyaddr(loop->channel, addr, addr_len, family, gethostbyaddr_cb, ctx);
    //DEBUG("end");
    return (PyObject*)wait_lookup(ctx);
}

void*
async_getsockaddr(LoopObject *loop, char *name, int family)
{
    lookup_ctx_t *ctx = init_ctx(loop);
    if (ctx == NULL) {
        //Error
        return NULL;
    }
    ctx->family = family;
    //DEBUG("start async_getsockaddr");
    ares_gethostbyname(loop->channel, name, family, getsockaddr_cb, ctx);
    //DEBUG("fin async_getsockaddr");
    return wait_lookup(ctx);
}

PyObject*
async_getnameinfo(LoopObject *loop, struct sockaddr *addr, int addrlen)
{
    int flags = ARES_NI_NUMERICHOST;
    lookup_ctx_t *ctx = init_ctx(loop);
    if (ctx == NULL) {
        //Error
        return NULL;
    }

    ares_getnameinfo(loop->channel, addr, addrlen, flags, getnameinfo_cb, ctx);
    return (PyObject*)wait_lookup(ctx);
}

PyObject*
async_getnameinfo_ex(LoopObject *loop, struct sockaddr_in6 *addr, int addrlen, int flag)
{
    int ares_flag = 0;
    lookup_ctx_t *ctx = init_ctx(loop);
    if (ctx == NULL) {
        //Error
        return NULL;
    }
    ares_flag = convert_nameinfo_flag(flag);
    if (ares_flag == -1) {
        return NULL;
    }
    ares_getnameinfo(loop->channel, (struct sockaddr *)addr, addrlen, ares_flag, getnameinfo_ex_cb, ctx);
    return (PyObject*)wait_lookup(ctx);
}

static PyObject *
getipstring(LoopObject *loop, struct sockaddr *addr, int addrlen)
{
    return async_getnameinfo(loop, addr, addrlen);
}

int
getsockaddrlen(int family, socklen_t *len_ret)
{
    switch (family) {
        case AF_UNIX:
            *len_ret = sizeof (struct sockaddr_un);
            return 1;
        case AF_INET:
            *len_ret = sizeof (struct sockaddr_in);
            return 1;
        case AF_INET6:
            *len_ret = sizeof (struct sockaddr_in6);
            return 1;
        default:
            PyErr_SetString(PyExc_OSError, "getsockaddrlen: bad family");
            return 0;
    }
}

static PyObject*
getsockaddr_inet(LoopObject *loop, struct sockaddr *addr, int addrlen)
{
    struct sockaddr_in *sa;
    PyObject *ret = NULL;
    PyObject *addrobj = NULL;

    DEBUG("addrlen:%d", addrlen);
    addrobj = getipstring(loop, addr, addrlen);
    if(addrobj) {
        sa = (struct sockaddr_in *)addr;
        ret = Py_BuildValue("Oi", addrobj, ntohs(sa->sin_port));
        Py_DECREF(addrobj);
    }
    return ret;
}


static PyObject*
getsockaddr_un(struct sockaddr *addr, int addrlen)
{
    struct sockaddr_un *un = (struct sockaddr_un*)addr;
#ifdef linux
    if (un->sun_path[0] == 0) {
        addrlen -= offsetof(struct sockaddr_un, sun_path);
        return PyBytes_FromStringAndSize(un->sun_path, addrlen);
    }
    else
#endif
    {
        return PyUnicode_FromString(un->sun_path);
    }
}

static PyObject*
getsockaddr_inet6(LoopObject *loop, struct sockaddr *addr, int addrlen)
{
    struct sockaddr_in6 *sa;
    PyObject *ret = NULL;
    PyObject *addrobj = getipstring(loop, addr, addrlen);
    if(addrobj) {
        sa = (struct sockaddr_in6 *)addr;
        ret = Py_BuildValue("Oiii", addrobj, ntohs(sa->sin6_port), sa->sin6_flowinfo, sa->sin6_scope_id);
        Py_DECREF(addrobj);
    }
    return ret;
}

PyObject*
getaddrtuple(LoopObject *loop, int family, struct sockaddr *addr, int addrlen)
{
    if(addrlen <= 0) {
        PyErr_SetString(PyExc_IOError, "not support family");
        return NULL;
    }
    switch (family) {

        case AF_UNIX:
            return getsockaddr_un(addr, addrlen);
        case AF_INET:
            return getsockaddr_inet(loop, addr, addrlen);
        case AF_INET6:
            return getsockaddr_inet6(loop, addr, addrlen);
        default:
            return Py_BuildValue("is#", addr->sa_family, addr->sa_data, sizeof(addr->sa_data));
    }
    PyErr_SetString(PyExc_IOError, "not support family");
    return NULL;
}
