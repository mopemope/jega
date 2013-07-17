import jega
import functools
size = 1024

def _call(a):
    return a 
futures = []
for i in range(size):
    f = jega.spawn(_call, i)
    futures.append(f)
r = functools.reduce(lambda x, y: x + y.result(), futures, 0)
r2 = functools.reduce(lambda x, y: x + y, range(size), 0)
assert(r == r2)
del futures
print("OK")
jega.get_event_loop().stop()


