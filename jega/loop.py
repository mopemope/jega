import jega
import threading
from jega import futures 
import socket 

import signal

#TODO import logger

_MAX_WORKERS = 5

_tls = threading.local()

def _raise_stop_error(*args):
    raise jega.LoopAbortException()

class JegaEventLoop(jega.AbstractEventLoop):
    
    def __init__(self):
        super(JegaEventLoop, self).__init__()
        self._signal_handlers = dict()
        self._default_executor = None
        self._make_self_pipe()
    
    def reset(self):
        self._close_self_pipe()
        self._make_self_pipe()

    def add_signal_handler(self, sig, callback, *args):
        """Add a handler for a signal.  UNIX only.

        Raise ValueError if the signal number is invalid or uncatchable.
        Raise RuntimeError if there is a problem setting up the handler.
        """
        self._check_signal(sig)
        try:
            # set_wakeup_fd() raises ValueError if this is not the
            # main thread.  By calling it early we ensure that an
            # event loop running in another thread cannot add a signal
            # handler.
            signal.set_wakeup_fd(self._csock.fileno())
        except ValueError as exc:
            raise RuntimeError(str(exc))

        handle = self._make_handle(callback, args)
        self._signal_handlers[sig] = handle

        try:
            signal.signal(sig, self._handle_signal)
        except OSError as exc:
            del self._signal_handlers[sig]
            if not self._signal_handlers:
                try:
                    signal.set_wakeup_fd(-1)
                except ValueError as nexc:
                    tulip_log.info('set_wakeup_fd(-1) failed: %s', nexc)

            if exc.errno == errno.EINVAL:
                raise RuntimeError('sig {} cannot be caught'.format(sig))
            else:
                raise

    def _handle_signal(self, sig, arg):
        """Internal helper that is the actual signal handler."""
        handle = self._signal_handlers.get(sig)
        if handle is None:
            return  # Assume it's some race condition.
        if handle.cancelled:
            self.remove_signal_handler(sig)  # Remove it properly.
        else:
            self._add_callback_signalsafe(handle)

    def remove_signal_handler(self, sig):
        """Remove a handler for a signal.  UNIX only.

        Return True if a signal handler was removed, False if not.
        """
        self._check_signal(sig)
        try:
            del self._signal_handlers[sig]
        except KeyError:
            return False

        if sig == signal.SIGINT:
            handler = signal.default_int_handler
        else:
            handler = signal.SIG_DFL

        try:
            signal.signal(sig, handler)
        except OSError as exc:
            if exc.errno == errno.EINVAL:
                raise RuntimeError('sig {} cannot be caught'.format(sig))
            else:
                raise

        if not self._signal_handlers:
            try:
                signal.set_wakeup_fd(-1)
            except ValueError as exc:
                tulip_log.info('set_wakeup_fd(-1) failed: %s', exc)

        return True

    def _check_signal(self, sig):
        """Internal helper to validate a signal.

        Raise ValueError if the signal number is invalid or uncatchable.
        Raise RuntimeError if there is a problem setting up the handler.
        """
        if not isinstance(sig, int):
            raise TypeError('sig must be an int, not {!r}'.format(sig))

        if signal is None:
            raise RuntimeError('Signals are not supported')

        if not (1 <= sig < signal.NSIG):
            raise ValueError(
                'sig {} out of range(1, {})'.format(sig, signal.NSIG))

    def _socketpair(self):
        if hasattr(socket, '__patched__'):
            return socket._socketpair()
        return socket.socketpair()

    def _close_self_pipe(self):
        self.remove_reader(self._ssock.fileno())
        self._ssock.close()
        self._ssock = None
        self._csock.close()
        self._csock = None

    def _make_self_pipe(self):
        self._ssock, self._csock = self._socketpair()
        self._ssock.setblocking(False)
        self._csock.setblocking(False)
        self.add_reader(self._ssock.fileno(), self._read_from_self)

    def _read_from_self(self):
        try:
            self._ssock.recv(1)
        except (BlockingIOError, InterruptedError):
            #TODO logging
            pass

    def _write_to_self(self):
        try:
            self._csock.send(b'x')
        except (BlockingIOError, InterruptedError):
            #TODO logging
            pass
    
    def destroy(self):
        self._close_self_pipe()

    def wrap_future(self, future):
        if isinstance(future, futures.Future):
            return future  # Don't wrap our own type of Future.
        new_future = futures.Future()
        future.add_done_callback(
            lambda future:
                self.call_soon_threadsafe(new_future._copy_state, future))
        return new_future

    def run(self):
        self.run_on_main_task(self._run)

    def run_once(self):
        self.run_on_main_task(self._run_once)

    def run_forever(self):
        handle = self.call_repeatedly(24 * 3600, lambda: None)
        try:
            self.run()
        finally:
            handle.cancel()

    def run_until_complete(self, future, timeout=None):
        future.add_done_callback(_raise_stop_error)

        if timeout is None:
            self.run_forever()
        else:
            handle = self.call_later(timeout, stop_loop)
            self.run()
            handle.cancel()

        return future.result()

    def stop(self):
        self.call_soon(_raise_stop_error)
        self.switch()

    def call_soon_threadsafe(self, callback, *args):
        handle = self.call_soon(callback, *args)
        self._write_to_self()
        return handle

    def run_in_executor(self, executor, callback, *args):
        if executor is None:
            executor = self._default_executor
            if executor is None:
                executor = futures.ThreadPoolExecutor(_MAX_WORKERS)
                self._default_executor = executor
        return self.wrap_future(executor.submit(callback, *args))

    def set_default_executor(self, executor):
        self._default_executor = executor

    def getaddrinfo(self, host, port, family=0, type=0, proto=0, flags=0):
        return self.run_in_executor(None, socket.getaddrinfo,
                                    host, port, family, type, proto, flags)

    def getnameinfo(self, sockaddr, flags=0):
        return self.run_in_executor(None, socket.getnameinfo, sockaddr, flags)

    def _add_callback_signalsafe(self, handle):
        """Like _add_callback() but called from a signal handler."""
        self._add_callback(handle)
        self._write_to_self()
    
    def set_socket_acceptor(self, sock, acceptor):
        sock.setblocking(0)
        if hasattr(sock, "patched"):
            accept = sock.accept_block
        else:
            accept = sock.accept

        self.add_reader(sock, internal_acceptor, accept, acceptor)

def internal_acceptor(accept, acceptor):
    res = accept()
    if res:
        spawn(acceptor, res[0], res[1])

def get_event_loop():
    """Get the current event loop singleton object.
    """
    try:
        return _tls.loop
    except AttributeError:
        # create loop only for main thread
        if threading.current_thread().name == 'MainThread':
            _tls.loop = JegaEventLoop()
            return _tls.loop
        raise RuntimeError('there is no event loop created in the current thread')

_executor = jega.TaskExecutor(get_event_loop())
_submit = _executor.submit

def spawn(cb, *args):
    return _submit(cb, *args)

