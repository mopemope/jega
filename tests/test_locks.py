from jega.locks import *
from pytest import *
import jega
from jega import loop
from util import *



def test_semaphore():
    print_name()

    def func():
        lock = Semaphore()
        lock.acquire()
        r = lock.acquire(blocking=False)
        assert(r == False)
        return r
    
    f = jega.spawn(func)
    r = f.result()
    assert(r == False)


def test_bounded_semaphore():
    print_name()

    def func():
        lock = BoundedSemaphore()
        lock.acquire()
        lock.release()
        lock.release()
    
    f = jega.spawn(func)
    with raises(ValueError):
        f.result()

def test_lock():
    print_name()

    def func():
        lock = Lock()
        lock.acquire()
        r = lock.acquire(blocking=False)
        assert(r == False)
        return r
    
    f = jega.spawn(func)
    r = f.result()
    assert(r == False)

def test_rlock():
    print_name()
    lock = RLock()
    def func1():
        lock.acquire()
        return lock.acquire()

    def func2():
        return lock.acquire(blocking=False)

    f1 = jega.spawn(func1)
    f2 = jega.spawn(func2)
    r = f1.result()
    assert(r == True)
    r = f2.result()
    assert(r == False)

def test_condition():
    print_name()
    d = dummy()
    d.value = 0
    cond = Condition()
    def func1():
        with cond:
            assert(d.value == 0)
            cond.wait()
            assert(d.value == 42)
    def func2():
        with cond:
            d.value = 42
            cond.notify_all()
    f1 = jega.spawn(func1)
    f2 = jega.spawn(func2)
    r = f1.result()
    r = f2.result()


def test_semaphore_timeout():
    print_name()

    def func():
        lock = Semaphore()
        lock.acquire()
        r = lock.acquire(timeout=2)
        return r
    
    f = jega.spawn(func)
    r = f.result()
    assert(r == False)

