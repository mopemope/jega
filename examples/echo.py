import jega
from jega import patch
patch.patch_socket()
import socket

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
s.setsockopt(socket.IPPROTO_TCP, socket.TCP_DEFER_ACCEPT, 1)
s.bind(("", 5000))
s.listen(1024)
loop = jega.get_event_loop()

def echo(sock, addr):
    try:
        recv = sock.recv
        send = sock.send
        while 1:
            buf = recv(4096)
            if not buf:
                return
            send(buf)
    finally:
        sock.close()

loop.set_socket_acceptor(s, echo)
loop.run()

