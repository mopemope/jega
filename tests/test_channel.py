from pytest import *
import socket
import jega
from util import *

def test_channel():

    loop = jega.get_event_loop()
    c = loop.make_channel()

    def _send(chan, data):
        chan.send(data)
        return data


    def _recv(chan):
        r = chan.recv()
        return r 
    data = 1
    f1 = jega.spawn(_send, c, data)
    f2 = jega.spawn(_recv, c)
    assert(f1.result() == f2.result())

def test_buf_channel():

    loop = jega.get_event_loop()
    c = loop.make_channel(2)

    def _send(chan, data):
        chan.send(data)
        chan.send(data)
        return data


    def _recv(chan):
        r1 = chan.recv()
        r2 = chan.recv()
        return r1 + r2

    data = 1
    f1 = jega.spawn(_send, c, data)
    f2 = jega.spawn(_recv, c)
    assert(f1.result() * 2 == f2.result())


def test_buf_channel2():

    loop = jega.get_event_loop()
    c = loop.make_channel(2)

    def _send(chan, data):
        chan.send(data)
        chan.send(data)
        return data


    def _recv(chan):
        r1 = chan.recv()
        r2 = chan.recv()
        r3 = chan.recv()
        return r1 + r2 + r3

    data = 1
    f1 = jega.spawn(_send, c, data)
    f2 = jega.spawn(_recv, c)
    with raises(ValueError):
        assert(f1.result() * 3 == f2.result())



