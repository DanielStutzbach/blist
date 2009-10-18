from _blist import blist
import collections, bisect, weakref

__all__ = ['sortedlist', 'weaksortedlist', 'sortedset', 'weaksortedset']

class sortedlist(collections.MutableSet, collections.Sequence):
    def _bisect(self, v):
        i = bisect.bisect_left(self, v)
        if i < len(self._blist):
            return i, self._blist[i]
        return i, None

    def __init__(self, seq=()):
        self._blist = blist(seq)
        self._blist.sort()

    def add(self, value):
        # Will throw a TypeError when trying to add an object that
        # cannot be compared to objects already in the list.
        i, _ = self._bisect(value)
        self._blist.insert(i, value)

    def discard(self, value):
        try:
            i, v = self._bisect(value)
        except TypeError:
            # Value cannot be compared with values already in the list.
            # Ergo, value isn't in the list.
            return
        if i < len(self._blist) and v == value:
            del self._blist[i]

    def clear(self):
        self._blist.clear()

    def __contains__(self, value):
        try:
            i, v = self._bisect(value)
        except TypeError: 
            # Value cannot be compared with values already in the list.
            # Ergo, value isn't in the list.
            return False
        if i == len(self._blist): return False
        return v == value

    def __len__(self):
        return len(self._blist)

    def __iter__(self):
        return iter(self._blist)

    def __getitem__(self, index):
        if isinstance(index, slice):
            return sortedlist(self._blist[index])
        return self._blist[index]

    def __reversed__(self):
        return reversed(self._blist)

    def index(self, value):
        try:
            i, v = self._bisect(value)
        except TypeError:
            raise ValueError
        if i == len(self._blist) or v != value:
            raise ValueError
        return i

    def count(self, value):
        try:
            i, _ = self._bisect(value)
        except TypeError:
            return 0
        start = i
        while i < len(self._blist) and self._blist[i] == value:
            i += 1
        return i - start

    def __repr__(self):
        return 'sortedlist' + repr(self._blist)[5:]

class sortedset(sortedlist):
    def add(self, value):
        i, v = self._bisect(value)
        if i < len(self._blist) and v == value:
            return
        self._blist.insert(i, value)

class weaksortedlist(sortedlist):
    def _bisect(self, value):
        if isinstance(value, weakref.ref):
            value = value()
        if value is None:
            raise ValueError
        lo = 0
        hi = len(self._blist)
        while lo < hi:
            mid = (lo+hi)//2
            n, v = self.__squeeze(mid)
            hi -= n
            if n and hi == len(self._blist): 
                continue
            if v < value: lo = mid+1
            else: hi = mid
        n, v = self.__squeeze(lo)
        return lo, v

    def __init__(self, seq=()):
        self._blist = blist(weakref.ref(value) for value in sorted(seq))

    def add(self, value):
        sortedlist.add(self, weakref.ref(value))

    def __iter__(self):
        i = 0
        while i < len(self._blist):
            n, v = self.__squeeze(i)
            if v is None: break
            yield v
            i += 1

    def __squeeze(self, i):
        n = 0
        while i < len(self._blist):
            r = self._blist[i]
            v = r()
            if v is None:
                del self._blist[i]
                n += 1
            else:
                return n, v
        return n, None

    def __getitem__(self, index):
        if isinstance(index, slice):
            rv = weaksortedlist()
            rv._blist = self._blist[index]
            return rv
        
        n, v = self.__squeeze(index)
        if v is None:
            raise IndexError('list index out of range')
        return v

    def __reversed__(self):
        i = len(self._blist)-1
        while i >= 0:
            n, v = self.__squeeze(i)
            if not n:
                yield v
            i -= 1

    def count(self, value):
        try:
            i, v = self._bisect(value)
        except TypeError:
            return 0
        start = i
        while i < len(self._blist) and v == value:
            i += 1
            _, v = self.__squeeze(i)
        return i - start
    
    def __repr__(self):
        store = [r() for r in self._blist]
        store = [r for r in store if r is not None]
        return 'weakreflist(%s)' % repr(store)

class weaksortedset(weaksortedlist):
    def add(self, value):
        i, v = self._bisect(value)
        if i < len(self._blist) and v == value:
            return
        self._blist.insert(i, weakref.ref(value))
