#ifndef LOOKUP_H
#define LOOKUP_H

#include "jega.h"
#include "loop.h"

typedef union sock_addr {
    struct sockaddr_in in;
    struct sockaddr sa;
    struct sockaddr_un un;
    struct sockaddr_in6 in6;
    struct sockaddr_storage storage;
} sock_addr_t;

#define SAS2SA(x)       (&((x)->sa))

int init_lookup(LoopObject *loop);

void destroy_lookup(LoopObject *loop);

int reset_channel(LoopObject *loop);

int make_nameinfo_sockaddr(char* hostp, int port, int flowinfo, int scope_id, struct sockaddr_in6 *sa6);

int test_ipv4(char *name, struct sockaddr *addr);

PyObject* async_gethostbyname(LoopObject *loop, char *name, int family);

PyObject* async_gethostbyname_ex(LoopObject *loop, char *name, int family);

PyObject* async_gethostbyaddr(LoopObject *loop, char *ip);

void* async_getsockaddr(LoopObject *loop, char *name, int family);

PyObject* async_getnameinfo(LoopObject *loop, struct sockaddr *addr, int addrlen);

PyObject* async_getnameinfo_ex(LoopObject *loop, struct sockaddr_in6 *addr, int addrlen, int flags);

/* utility */
int is_af_inet_address(PyObject *ip);

int getsockaddrlen(int family, socklen_t *len_ret);

PyObject* getaddrtuple(LoopObject *loop, int family, struct sockaddr *addr, int addrlen);

#endif
