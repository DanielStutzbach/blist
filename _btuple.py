from _blist import blist
from ctypes import c_int
import collections
class btuple(collections.Sequence):
    def __init__(self, seq=None):
        if isinstance(seq, btuple):
            self._blist = seq._blist
        elif seq is not None:
            self._blist = blist(seq)
        else:
            self._blist = blist()
        self._hash = -1

    def _btuple_or_tuple(self, other, f):
        if isinstance(other, btuple):
            rv = f(self._blist, other._blist)
        elif isinstance(other, tuple):
            rv = f(self._blist, blist(other))
        else:
            return NotImplemented
        if isinstance(rv, blist):
            rv = btuple(rv)
        return rv        
        
    def __hash__(self):
        # Based on tuplehash from tupleobject.c
        if self._hash != -1:
            return self._hash
        
        n = len(self)
        mult = c_int(1000003)
        x = c_int(0x345678)
        for ob in self:
            n -= 1
            y = c_int(hash(ob))
            x = (x ^ y) * mult
            mult += c_int(82520) + n + n
        x += c_int(97531)
        if x == -1:
            x = -2;
        self._hash = x.value
        return self._hash

    def __add__(self, other):
        rv = self._btuple_or_tuple(other, blist.__add__)
        if rv is NotImplemented:
            raise TypeError
        return rv
    def __radd__(self, other):
        rv = self._btuple_or_tuple(other, blist.__radd__)
        if rv is NotImplemented:
            raise TypeError
        return rv
    def __contains__(self, item):
        return item in self._blist
    def __eq__(self, other):
        return self._btuple_or_tuple(other, blist.__eq__)
    def __ge__(self, other):
        return self._btuple_or_tuple(other, blist.__ge__)
    def __gt__(self, other):
        return self._btuple_or_tuple(other, blist.__gt__)
    def __le__(self, other):
        return self._btuple_or_tuple(other, blist.__le__)
    def __lt__(self, other):
        return self._btuple_or_tuple(other, blist.__lt__)
    def __ne__(self, other):
        return self._btuple_or_tuple(other, blist.__ne__)
    def __iter__(self):
        return iter(self._blist)
    def __len__(self):
        return len(self._blist)
    def __getitem__(self, key):
        if isinstance(key, slice):
            return btuple(self._blist[key])
        return self._blist[key]
    def __getslice__(self, i, j):
        return btuple(self._blist[i:j])
    def __repr__(self):
        return 'btuple((' + repr(self._blist)[7:-2] + '))'
    def __str__(self):
        return repr(self)
    def __mul__(self, i):
        return btuple(self._blist * i)
    def __rmul__(self, i):
        return btuple(i * self._blist)
    def count(self, item):
        return self._blist.count(item)
    def index(self, item):
        return self._blist.index(item)

del c_int
del collections
