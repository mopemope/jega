import jega
import socket
from util import *

def _callback():
    print("Hello")

def adder(x, y):
    z = x + y 
    print(z)
    assert(z == 3)
    return z

def stopper():
    jega.get_event_loop().stop()

# @check_stop
def test_get_event_loop():
    print_name()
    loop = jega.get_event_loop()
    assert(loop)

# @check_stop
def test_ev_run_simple():
    loop = jega.get_event_loop()
    loop.run()
    assert(True)

# @check_stop
def test_ev_call_later():
    loop = jega.get_event_loop()
    t = loop.call_later(1, _callback)
    loop.run()
    assert(True)

# @check_stop
def test_ev_call_later_args():
    loop = jega.get_event_loop()
    t = loop.call_later(1, adder, 1, 2)
    assert(isinstance(t, jega.Handle))
    assert(t.callback)
    assert(t.args)
    assert(t.cancelled == False)
    loop.run()
    assert(True)

# @check_stop
def test_ev_call_soon():
    loop = jega.get_event_loop()
    t = loop.call_soon(_callback)
    loop.run()
    assert(True)

# @check_stop
def test_ev_call_soon_args():
    loop = jega.get_event_loop()
    t = loop.call_soon(adder, 1, 2)
    loop.run()
    assert(True)

# @check_stop
def test_ev_call_repeat():
    loop = jega.get_event_loop()
    loop.call_later(5, stopper)
    t = loop.call_repeatedly(1, _callback)
    loop.run()
    assert(True)
    t.cancel()
    loop.run()

# @check_stop
def test_add_remove_reader():
    loop = jega.get_event_loop()
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    def call(x, y):
        z = x + y
        assert(z == 4)
        loop.remove_reader(s)

    s.connect(("localhost", 22))
    loop.add_reader(s, call, 1, 3)
    loop.run()

