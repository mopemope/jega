from pytest import *
import socket
import jega
from jega import loop
from util import *

# @check_stop
def test_executor():
    print_name()
    
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)
    assert(executor)

# @check_stop
def test_executor_submit():
    print_name()
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _call(a, b):
        return a + b

    f = executor.submit(_call, 1, 2)
    assert(3 == f.result())


def test_executor_until_complete():
    print_name()
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _call(a, b):
        return a + b

    f = executor.submit(_call, 1, 2)
    r = event_loop.run_until_complete(f)
    assert(3 == r)

def test_executor_until_complete_exc():
    print_name()
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _call(a, b):
        return a / b

    f = executor.submit(_call, 1, 0)
    with raises(ZeroDivisionError):
        r = event_loop.run_until_complete(f)

# @check_stop
# def test_executor_submit_err():
    # print_name()
    # event_loop = loop.get_event_loop()
    # executor = jega.TaskExecutor(event_loop)

    # def _call(a, b):
        # return a + b

    # f = executor.submit(_call, 1, 2)
    # with raises(jega.InvalidStateError):
        # f.result()


# @check_stop
def test_future_exc():
    print_name()
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _call(a, b):
        return a / b

    f = executor.submit(_call, 1, 0)
    with raises(ZeroDivisionError):
        r = f.result()


# @check_stop
def test_future_exc2():
    print_name()
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _call(a, b):
        return a / b

    f = executor.submit(_call, 1, 0)
    try:
        r = f.result()
    except ZeroDivisionError as exc:
        pass
    e = f.exception()
    # print(e)
    assert(e)
    assert(type(e) == ZeroDivisionError)

# @check_stop
def test_future_large():
    import functools
    print_name()
    size = 1024 * 4
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop)

    def _call(a):
        return a 
    futures = []
    for i in range(size):
        f = executor.submit(_call, i)
        futures.append(f)
    r = functools.reduce(lambda x, y: x + y.result(), futures, 0)
    r2 = functools.reduce(lambda x, y: x + y, range(size), 0)
    assert(r == r2)


def test_future_large2():
    import functools
    print_name()
    size = 1024 * 4
    event_loop = loop.get_event_loop()
    executor = jega.TaskExecutor(event_loop, 2)

    def _call(a):
        return a 
    futures = []
    for i in range(size):
        f = executor.submit(_call, i)
        futures.append(f)
    r = functools.reduce(lambda x, y: x + y.result(), futures, 0)
    r2 = functools.reduce(lambda x, y: x + y, range(size), 0)
    assert(r == r2)


