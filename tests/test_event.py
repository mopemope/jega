from jega.event import *
from pytest import *
import jega
from jega import loop
from util import *


def test_event_simple():
    print_name()
    ev = Event()

    def waiter():
        r = ev.wait()
        return r

    f1 = jega.spawn(waiter)
    f2 = jega.spawn(ev.set)

    r = f1.result()
    assert(True == r)
    r = f2.result()
    assert(None == r)


def test_event_timeout():
    print_name()
    event_loop = loop.get_event_loop()

    ev = Event()

    def waiter():
        assert(False == ev.wait(1))

    jega.spawn(waiter)
    event_loop.call_later(2, ev.set)
    event_loop.run()

def test_event_clear():
    print_name()
    event_loop = loop.get_event_loop()

    ev = Event()

    def waiter():
        assert(True == ev.wait())
        ev.clear()

    jega.spawn(waiter)
    jega.spawn(ev.set)
    event_loop.run()
    assert(False == ev.is_set())
 


