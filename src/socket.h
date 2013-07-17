#ifndef SOCKET_H 
#define SOCKET_H 

#include "jega.h"
#include "loop.h"

PyObject *socket_gaierror;

PyObject* SocketMod_getaddrinfo(LoopObject *loop, PyObject *args, PyObject *kwargs);

PyObject* SocketMod_getnameinfo(LoopObject *loop, PyObject *args);

PyObject* SocketMod_gethostbyaddr(LoopObject *loop, PyObject *args);

PyObject* SocketMod_gethostbyname(LoopObject *loop, PyObject *args);

PyObject* SocketMod_gethostbyname_ex(LoopObject *loop, PyObject *args);

int setup_socket_mod(void);


#endif
