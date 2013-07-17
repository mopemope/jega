Jega
============

Jega is a concurrent networking and cooperative multitasking library for Python3.

Features:

- Fast event loop based on picoev_.
- Lightweight task based on greenlet_ and fast scheduler.
- Cooperative synchronization primitives: locks, events, queues, channels
- Futures API (almost) compatible with the standard library
- DNS queries performed through c-ares_ or a threadpool.
- Cooperative versions of several standard library modules
- Ability to use standard library and 3rd party modules written for standard blocking sockets

Jega is inspired by gevent_ and evergreen_ and PEP3156. 

Requirements
---------------------------------

Jega requires **Python 3.x >= 3.2** . and **greenlet >= 0.4.0**.

Jega supports Linux, FreeBSD (Mac OS X not test).

Installation
============

Install from pypi::

  $ easy_install -ZU jega 

If you install Jega with lastest source code, run ``setup.py``::

   $ python setup.py develop
   $ python setup.py install

Running tests
======================

Jega use py.test_.

From the toplevel directory, run: ``py.test tests/``


.. _picoev: https://github.com/kazuho/picoev
.. _greenlet: http://pypi.python.org/pypi/greenlet
.. _py.test: http://pypi.python.org/pypi/pytest
.. _evergreen: http://pypi.python.org/pypi/evergreen
.. _gevent: http://www.gevent.org
.. _c-ares: http://c-ares.haxx.se/

