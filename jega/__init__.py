from _jega import *
from jega.loop import *

import sys
is_py3 = sys.hexversion >=  0x03000000
is_py33 = sys.hexversion >= 0x03030000

if is_py3:
    def exc_clear():
        pass
else:
    def exc_clear():
        sys.exc_clear()

if is_py3:
    string_types = str,
    integer_types = int,
    class_types = type,
    text_type = str
    binary_type = bytes

    MAXSIZE = sys.maxsize
else:
    string_types = basestring,
    integer_types = (int, long)
    class_types = (type, types.ClassType)
    text_type = unicode
    binary_type = str
