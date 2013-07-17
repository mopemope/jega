from pytest import *
import socket
import jega
from jega.ext import jsocket
from jega import loop
from util import *

HOST = "bitbucket.org"
PORT = 80
HOST_IPS = ["131.103.20.167", "131.103.20.168"]
LISTEN_PORT = 8080

# @check_stop
def test_accept_timeout():
    print_name()
    with raises(jsocket.timeout):
        s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(jsocket.SOL_SOCKET, jsocket.SO_REUSEADDR, 1)
        s.bind(("", LISTEN_PORT))
        s.listen(1)
        s.settimeout(2)
        result = s.accept()

# @check_stop
def test_accept():
    print_name()
    
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _accept():
        ss = jsocket.socket(jsocket.AF_INET, jsocket.SOCK_STREAM)
        ss.setsockopt(jsocket.SOL_SOCKET, jsocket.SO_REUSEADDR, 1)
        ss.bind(("0.0.0.0", LISTEN_PORT))
        ss.listen(1)
        s, a = ss.accept()
        assert(a[0] == "127.0.0.1")
        assert(s.recv(10) == b"A" * 10)
        return "accept"

    def _conn():
        s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("", LISTEN_PORT))
        a = s.send(b"A" * 10)
        return "conn"
    f1 = executor.submit(_accept)
    f2 = executor.submit(_conn)
    r = event_loop.run_until_complete(f1)
    assert(r == "accept")
    r = event_loop.run_until_complete(f2)
    assert(r == "conn")

# @check_stop
def test_bind():
    print_name()
    ss = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ss.setsockopt(jsocket.SOL_SOCKET, jsocket.SO_REUSEADDR, 1)
    ss.bind(("", LISTEN_PORT))
    assert(ss)

# @check_stop
def test_bind_fail():
    print_name()
    ss = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ss.setsockopt(jsocket.SOL_SOCKET, jsocket.SO_REUSEADDR, 1)
    ss.bind(("", LISTEN_PORT))
    with raises(jsocket.error):
        ss.bind(("", LISTEN_PORT))

# @check_stop
def test_close():
    print_name()
    s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    s.close()

# @check_stop
def test_close_duble():
    print_name()
    ts = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ts.connect((HOST, PORT))
    ts.close()
    ts.close()

# @check_stop
def test_connect():
    print_name()
    s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    assert(s)

# @check_stop
def test_connect_fail():
    print_name()
    s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    with raises(jsocket.gaierror):
        s.connect(("google.comaaa", 80))

# @check_stop
def test_connect_ex():
    print_name()
    ts = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ts.connect_ex((HOST, PORT))
    assert(ts)

def test_connect_ex_fail():
    print_name()
    ts = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    with raises(jsocket.gaierror):
        ts.connect_ex(("google.comaaa", PORT))

# @check_stop
def test_detach():
    print_name()
    ts = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    fd = ts.detach()
    assert(fd > 2)

# @check_stop
def test_fileno():
    print_name()
    ts = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    fd = ts.fileno()
    assert(fd > 2)

# @check_stop
def test_getpeername():
    print_name()
    ts = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ts.connect((HOST, PORT))
    assert(ts)
    host, port = ts.getpeername()
    assert(host in HOST_IPS)
    assert(port == 80)

# @check_stop
def test_sockname():
    print_name()
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    shost, sport = s.getsockname()
    ts = jsocket.socket(jsocket.AF_INET, jsocket.SOCK_STREAM)
    host, port = ts.getsockname()
    assert(host == shost)
    assert(port == sport)

# @check_stop
def test_getsockopt():
    print_name()
    ts = jsocket.socket(jsocket.AF_INET, jsocket.SOCK_STREAM)
    tr = ts.getsockopt(jsocket.IPPROTO_TCP, jsocket.TCP_NODELAY)
    assert(1 == tr)

# @check_stop
def test_listen():
    print_name()
    s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(jsocket.SOL_SOCKET, jsocket.SO_REUSEADDR, 1)
    s.bind(("", LISTEN_PORT))
    s.listen(1)
    assert(s)

# @check_stop
def test_timeout():
    print_name()
    val = 5
    s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(val)
    assert(val == s.gettimeout())

# @check_stop
def test_makefile():
    print_name()
    ts = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ts.connect(("", PORT))
    f = ts.makefile()
    # print(f)
    assert(f)

# @check_stop
def atest_makefile_readwrite():
    print_name()
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _accept():
        s = jsocket.socket(jsocket.AF_INET, jsocket.SOCK_STREAM)
        s.setsockopt(jsocket.SOL_SOCKET, jsocket.SO_REUSEADDR, 1)
        s.bind(("", 8080))
        s.listen(1)
        c, a = s.accept()
        assert(a[0] == "127.0.0.1")
        f = c.makefile(mode="rw")
        assert(f)
        assert(f.read(10) == "A" * 10)
        f.write("B" * 10)
        return "_accept"
    
    def _conn():
        s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("", 8080))
        s.sendall(b"A" * 10)
        assert(s.recv(10) == b"B" * 10)
        return "_conn"

    f1 = executor.submit(_accept)
    f2 = executor.submit(_conn)
    r = event_loop.run_until_complete(f1)
    assert(r == "_accept")
    r = event_loop.run_until_complete(f2)
    assert(r == "_conn")
    event_loop.stop()

# @check_stop
def test_recv():
    print_name()
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _accept():
        s = jsocket.socket(jsocket.AF_INET, jsocket.SOCK_STREAM)
        s.setsockopt(jsocket.SOL_SOCKET, jsocket.SO_REUSEADDR, 1)
        s.bind(("", 8080))
        s.listen(1)
        c, a = s.accept()
        assert(a[0] == "127.0.0.1")
        assert(c.recv(10) == b"A" * 10)
        return "_accept"
    
    def _conn():
        s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("", 8080))
        s.send(b"A" * 10)
        return "_conn"

    f1 = executor.submit(_accept)
    f2 = executor.submit(_conn)
    r = event_loop.run_until_complete(f1)
    assert(r == "_accept")
    r = event_loop.run_until_complete(f2)
    assert(r == "_conn")

# @check_stop
def test_recvfrom():
    print_name()
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _accept():
        s = jsocket.socket(jsocket.AF_INET, jsocket.SOCK_STREAM)
        s.setsockopt(jsocket.SOL_SOCKET, jsocket.SO_REUSEADDR, 1)
        s.bind(("", 8080))
        s.listen(1)
        c, a = s.accept()
        assert(a[0] == "127.0.0.1")
        ret = c.recvfrom(10)
        assert(len(ret) == 2)
        assert(ret[0] == b"A" * 10)
        assert(ret[1] == None)
        return "_accept"
    
    def _conn():
        s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("", 8080))
        s.send(b"A" * 10)
        return "_conn"

    f1 = executor.submit(_accept)
    f2 = executor.submit(_conn)
    r = event_loop.run_until_complete(f1)
    assert(r == "_accept")
    r = event_loop.run_until_complete(f2)
    assert(r == "_conn")

# @check_stop
def test_recvfrom_into():
    print_name()
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _accept():
        s = jsocket.socket(jsocket.AF_INET, jsocket.SOCK_STREAM)
        s.setsockopt(jsocket.SOL_SOCKET, jsocket.SO_REUSEADDR, 1)
        s.bind(("", 8080))
        s.listen(1)
        c, a = s.accept()
        assert(a[0] == "127.0.0.1")
        buf = bytearray(1024)
        nbytes, addr = c.recvfrom_into(buf)
        assert(nbytes == 10)
        assert(buf[:10] == b"A" * 10)
        return "_accept"
    
    def _conn():
        s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("", 8080))
        s.send(b"A" * 10)
        return "_conn"

    f1 = executor.submit(_accept)
    f2 = executor.submit(_conn)
    r = event_loop.run_until_complete(f1)
    assert(r == "_accept")
    r = event_loop.run_until_complete(f2)
    assert(r == "_conn")

# @check_stop
def test_recv_into():
    print_name()
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _accept():
        s = jsocket.socket(jsocket.AF_INET, jsocket.SOCK_STREAM)
        s.setsockopt(jsocket.SOL_SOCKET, jsocket.SO_REUSEADDR, 1)
        s.bind(("", 8080))
        s.listen(1)
        c, a = s.accept()
        assert(a[0] == "127.0.0.1")
        buf = bytearray(1024)
        nbytes = c.recv_into(buf)
        assert(nbytes == 10)
        assert(buf[:10] == b"A" * 10)
        return "_accept"
    
    def _conn():
        s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("", 8080))
        s.send(b"A" * 10)
        return "_conn"

    f1 = executor.submit(_accept)
    f2 = executor.submit(_conn)
    r = event_loop.run_until_complete(f1)
    assert(r == "_accept")
    r = event_loop.run_until_complete(f2)
    assert(r == "_conn")

# @check_stop
def test_send():
    print_name()
    
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _accept():
        ss = jsocket.socket(jsocket.AF_INET, jsocket.SOCK_STREAM)
        ss.setsockopt(jsocket.SOL_SOCKET, jsocket.SO_REUSEADDR, 1)
        ss.bind(("0.0.0.0", LISTEN_PORT))
        ss.listen(1)
        s, a = ss.accept()
        assert(a[0] == "127.0.0.1")
        assert(s.recv(10) == b"A" * 10)
        return "accept"

    def _conn():
        s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("", LISTEN_PORT))
        a = s.send(b"A" * 10)
        return "conn"
    f1 = executor.submit(_accept)
    f2 = executor.submit(_conn)
    r = event_loop.run_until_complete(f1)
    assert(r == "accept")
    r = event_loop.run_until_complete(f2)
    assert(r == "conn")

# @check_stop
def test_sendall():
    print_name()
    
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _accept():
        ss = jsocket.socket(jsocket.AF_INET, jsocket.SOCK_STREAM)
        ss.setsockopt(jsocket.SOL_SOCKET, jsocket.SO_REUSEADDR, 1)
        ss.bind(("0.0.0.0", LISTEN_PORT))
        ss.listen(1)
        s, a = ss.accept()
        assert(a[0] == "127.0.0.1")
        assert(s.recv(10) == b"A" * 10)
        return "accept"

    def _conn():
        s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("", LISTEN_PORT))
        a = s.sendall(b"A" * 10)
        return "conn"
    f1 = executor.submit(_accept)
    f2 = executor.submit(_conn)
    r = event_loop.run_until_complete(f1)
    assert(r == "accept")
    r = event_loop.run_until_complete(f2)
    assert(r == "conn")

# @check_stop
def test_sendto():
    print_name()
    
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _accept():
        ss = jsocket.socket(jsocket.AF_INET, jsocket.SOCK_STREAM)
        ss.setsockopt(jsocket.SOL_SOCKET, jsocket.SO_REUSEADDR, 1)
        ss.bind(("0.0.0.0", LISTEN_PORT))
        ss.listen(1)
        s, a = ss.accept()
        assert(a[0] == "127.0.0.1")
        assert(s.recv(10) == b"A" * 10)
        return "accept"

    def _conn():
        s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("", LISTEN_PORT))
        a = s.sendto(b"A" * 10, ("127.0.0.1", 8080))
        return "conn"

    f1 = executor.submit(_accept)
    f2 = executor.submit(_conn)
    r = event_loop.run_until_complete(f1)
    assert(r == "accept")
    r = event_loop.run_until_complete(f2)
    assert(r == "conn")


def test_setblocking():
    print_name()
    ts = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ts.setblocking(True)
    ts.setblocking(False)
    assert(ts)

# @check_stop
def test_shutdown():
    print_name()
    
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _accept():
        ss = jsocket.socket(jsocket.AF_INET, jsocket.SOCK_STREAM)
        ss.setsockopt(jsocket.SOL_SOCKET, jsocket.SO_REUSEADDR, 1)
        ss.bind(("0.0.0.0", LISTEN_PORT))
        ss.listen(1)
        s, a = ss.accept()
        assert(a[0] == "127.0.0.1")
        assert(s.recv(10) == b"A" * 10)
        s.shutdown(jsocket.SHUT_RDWR)
        return "accept"

    def _conn():
        s = jsocket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("", LISTEN_PORT))
        a = s.sendto(b"A" * 10, ("127.0.0.1", 8080))
        s.shutdown(jsocket.SHUT_RDWR)
        return "conn"

    f1 = executor.submit(_accept)
    f2 = executor.submit(_conn)
    r = event_loop.run_until_complete(f1)
    assert(r == "accept")
    r = event_loop.run_until_complete(f2)
    assert(r == "conn")


