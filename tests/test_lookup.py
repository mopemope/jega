from pytest import *
from jega import loop 
from jega.ext import jsocket 
import socket
from util import *

#print(dir(socket))

HOST = "yahoo.com"
HOST_IP = "98.137.149.56"
HOST_IPS = ['98.138.253.109', '206.190.36.45', '98.139.183.24', '69.147.125.65', '72.30.2.43', '98.137.149.56', '209.191.122.70', '67.195.160.76']
FAIL_HOST = "yahoo.coom"
FAIL_HOST_IP = "77.77.77.77"
HOSTS = [HOST, "ir1.fp.vip.gq1.yahoo.com", "ir1.fp.vip.ne1.yahoo.com","ir2.fp.vip.bf1.yahoo.com", "UNKNOWN-98-137-149-X.yahoo.com"]


# @check_stop
def test_getaddrinfo():
    print_name()
    event_loop = loop.get_event_loop();
    tret = event_loop._getaddrinfo(None, 80)
    ret = socket.getaddrinfo(None, 80)
    assert(ret == tret)

# @check_stop
def test_getaddrinfo2():
    print_name()
    event_loop = loop.get_event_loop();
    tret = event_loop._getaddrinfo(HOST, 80, 0, socket.SOCK_STREAM)
    ret = socket.getaddrinfo(HOST, 80, 0, socket.SOCK_STREAM)
    # print(tret)
    # print(ret)
    assert(len(ret) == len(tret))

# @check_stop
def test_getaddrinfo3():
    print_name()
    event_loop = loop.get_event_loop();
    tret = event_loop._getaddrinfo(HOST_IP, 80, 0, socket.SOCK_STREAM)
    ret = socket.getaddrinfo(HOST_IP, 80, 0, socket.SOCK_STREAM)
    # print(tret)
    # print(ret)
    assert(len(ret) == len(tret))

# @check_stop
def test_getaddrinfo_fail():
    print_name()
    with raises(socket.gaierror):
        event_loop = loop.get_event_loop();
        tret = event_loop._getaddrinfo(FAIL_HOST, 80)

# @check_stop
def test_getfqdn():
    print_name()
    tret = jsocket.getfqdn(HOST)
    ret = socket.getfqdn(HOST)
    print(tret)
    print(ret)
    assert(tret in HOSTS) 

# @check_stop
def test_getfqdn2():
    print_name()
    tret = jsocket.getfqdn(HOST_IP)
    ret = socket.getfqdn(HOST_IP)
    print(tret)
    print(ret)
    assert(tret in HOSTS) 

# @check_stop
def test_getfqdn_fail():
    print_name()
    tret = jsocket.getfqdn(FAIL_HOST)
    ret = socket.getfqdn(FAIL_HOST)
    assert(ret == tret)

# @check_stop
def test_gethostbyname():
    print_name()
    ret = jsocket.gethostbyname(HOST)
    assert(ret in HOST_IPS)

# @check_stop
def test_gethostbyname2():
    print_name()
    ret = jsocket.gethostbyname(HOST_IP)
    assert(ret in HOST_IPS)

# @check_stop
def test_gethostbyname_fail():
    print_name()
    with raises(jsocket.gaierror):
        ret = jsocket.gethostbyname(FAIL_HOST)

# @check_stop
def test_gethostbyname_ex():
    print_name()
    tret = jsocket.gethostbyname_ex(HOST)
    ret = socket.gethostbyname_ex(HOST)
    assert(ret[0] == tret[0])
    assert(len(ret[2]) == len(tret[2]))

# @check_stop
def test_gethostbyname_ex2():
    print_name()
    tret = jsocket.gethostbyname_ex(HOST_IP)
    ret = socket.gethostbyname_ex(HOST_IP)
    assert(ret[0] == tret[0])
    assert(len(ret[2]) == len(tret[2]))

# @check_stop
def test_gethostbyname_ex_fail():
    print_name()
    with raises(jsocket.gaierror):
        ret = jsocket.gethostbyname_ex(FAIL_HOST)


# @check_stop
def test_gethostname():
    print_name()
    tret = jsocket.gethostname()
    ret = socket.gethostname()
    assert(ret == tret)

# @check_stop
def test_gethostbyaddr():
    print_name()
    tret = jsocket.gethostbyaddr(HOST)
    ret = socket.gethostbyaddr(HOST)
    print(tret)
    print(ret)
    assert(tret[0] in HOSTS)

# @check_stop
def test_gethostbyaddr2():
    print_name()
    tret = jsocket.gethostbyaddr(HOST_IP)
    ret = socket.gethostbyaddr(HOST_IP)
    print(tret)
    print(ret)
    assert(ret == tret)

# @check_stop
def test_gethostbyaddr3():
    print_name()
    with raises(jsocket.gaierror):
        tret = jsocket.gethostbyaddr(FAIL_HOST)

# @check_stop
def test_getnameinfo():
    print_name()
    event_loop = loop.get_event_loop();
    tret = event_loop._getnameinfo((HOST_IP, 80), socket.NI_NUMERICHOST);
    ret = socket.getnameinfo((HOST_IP, 80), socket.NI_NUMERICHOST);
    assert(ret == tret)

# @check_stop
def test_getnameinfo2():
    print_name()
    event_loop = loop.get_event_loop();
    tret = event_loop._getnameinfo((HOST_IP, 80), socket.NI_NUMERICSERV);
    ret = socket.getnameinfo((HOST_IP, 80), socket.NI_NUMERICSERV);
    assert(ret == tret)

# @check_stop
def test_getnameinfo3():
    print_name()
    with raises(socket.gaierror):
        event_loop = loop.get_event_loop();
        ret = event_loop._getnameinfo((HOST, 80), socket.NI_NUMERICHOST);

