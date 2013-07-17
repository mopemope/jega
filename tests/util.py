import sys
import jega

class dummy(object):
    pass

def print_name():
    f = sys._getframe(1)
    file_name  =f.f_globals["__file__"]
    func_name =  f.f_code.co_name
    print("\n\x1B[31m%s:%s\x1B[0m\n" % (file_name, func_name))

def check_stop(func):
    def _f():
        loop = jega.get_event_loop()
        r = func()
        assert(loop.running == False)
    
    return _f



