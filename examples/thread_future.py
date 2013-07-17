from jega import futures

executor = futures.ThreadPoolExecutor(5)

def _call(a, b):
    c = a + b
    print(c)
    return c

f1 = executor.submit(_call, 1, 2)
f2 = executor.submit(_call, 1, 2)

futures.wait([f1, f2])

