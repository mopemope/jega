import sys


__all__ = ['patch_socket']

originals = {}

def patch_socket(aggressive=False):
    patch_module('socket')

def patch_item(module, attr, newitem):
    NONE = object()
    olditem = getattr(module, attr, NONE)
    if olditem is not NONE:
        originals.setdefault(module.__name__, {}).setdefault(attr, olditem)

    #patch
    setattr(module, attr, newitem)

def patch_module(name, items=None):
    nm = 'j' + name

    jega_mod = __import__('jega.ext.' + nm)
    jega_mod = getattr(jega_mod, "ext")
    jega_mod = getattr(jega_mod, nm)

    module_name = getattr(jega_mod, '__patch__', name)
    module = __import__(module_name)
    if items is None:
        items = getattr(jega_mod, '__patched__', None)
        if items is None:
            raise AttributeError('%r does not have __patched__' % jega_mod)
    for attr in items:
        desc = getattr(jega_mod, attr, None)
        if desc:
            patch_item(module, attr, getattr(jega_mod, attr))
    setattr(module, "__patched__", True)

def slurp_properties(source, destination, ignore=[], srckeys=None):
    """Copy properties from *source* (assumed to be a module) to
    *destination* (assumed to be a dict).

    *ignore* lists properties that should not be thusly copied.
    *srckeys* is a list of keys to copy, if the source's __all__ is
    untrustworthy.
    """
    if srckeys is None:
        srckeys = source.__all__
    destination.update(dict([(name, getattr(source, name))
                              for name in srckeys
                                if not (name.startswith('__') or name in ignore)
                            ]))


