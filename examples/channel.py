import jega 

loop = jega.get_event_loop()
c = loop.make_channel()

def _send(chan):
    print("** start send")
    a = 1 
    chan.send(a)
    print("** sended:%s" % a)
    return a


def _recv(chan):
    print("** start receive")
    r = chan.recv()
    # r = 1
    print("** received:%s" % r)
    return r 

f1 = jega.spawn(_send, c)
f2 = jega.spawn(_recv, c)

loop.run_until_complete(f1)
assert(f1.result() == f2.result())
f1.result()

r = c.recv()
print("received:%s" % r)
print("finish")

