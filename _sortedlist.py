from _blist import blist
import collections, bisect, weakref

__all__ = ['sortedlist', 'weaksortedlist', 'sortedset', 'weaksortedset']

class sortedlist(collections.MutableSet, collections.Sequence):
    def __init__(self, seq=(), key=None):
        self._key = key
        self._blist = blist()
        for v in seq:
            self.add(v)

    def _u2key(self, value):
        "Convert a user-object to the key"
        if self._key is None:
            return value
        else:
            return self._key(value)

    def _u2i(self, value):
        "Convert a user-object to the internal representation"
        if self._key is None:
            return value
        else:
            return (self._key(value), value)

    def _i2u(self, value):
        "Convert an internal object to a user-object"
        if self._key is None:
            return value
        else:
            return value[1]

    def _i2key(self, value):
        "Convert an internal object to the key"
        if self._key is None:
            return value
        else:
            return value[0]

    def _bisect(self, v):
        """Locate the point in the list where v would be inserted.

        Returns an (i, value) tuple:
          - i is the position where v would be inserted
          - value is the current user-object at position i

        This is the key function to override in subclasses.  They must
        accept a user-object v and return a user-object value.
        """

        key = self._u2key(v)
        lo = 0
        hi = len(self._blist)
        while lo < hi:
            mid = (lo+hi)//2
            v = self._i2key(self._blist[mid])
            if v < key: lo = mid+1
            else: hi = mid
        if lo < len(self._blist):
            return lo, self._i2u(self._blist[lo])
        return lo, None

    def add(self, value):
        # Will throw a TypeError when trying to add an object that
        # cannot be compared to objects already in the list.
        i, _ = self._bisect(value)
        self._blist.insert(i, self._u2i(value))

    def discard(self, value):
        try:
            i, v = self._bisect(value)
        except TypeError:
            # Value cannot be compared with values already in the list.
            # Ergo, value isn't in the list.
            return
        i = self._advance(i, value)
        if i >= 0:
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
        i = self._advance(i, value)
        return i >= 0

    def __len__(self):
        return len(self._blist)

    def __iter__(self):
        return (self._i2u(v) for v in self._blist)

    def __getitem__(self, index):
        if isinstance(index, slice):
            rv = self.__class__()
            rv._blist = self._blist[index]
            rv._key = self._key
            return rv
        return self._i2u(self._blist[index])

    def _advance(self, i, value):
        "Do a linear search through all items with the same key"
        key = self._u2key(value)
        while i < len(self._blist):
            if self._i2u(self._blist[i]) == value:
                return i
            elif key < self._i2key(self._blist[i]):
                break
            i += 1
        return -1

    def __reversed__(self):
        return (self._i2u(v) for v in reversed(self._blist))

    def index(self, value):
        try:
            i, v = self._bisect(value)
        except TypeError:
            raise ValueError
        i = self._advance(i, value)
        if i >= 0:
            return i
        raise ValueError

    def count(self, value):
        try:
            i, _ = self._bisect(value)
        except TypeError:
            return 0
        key = self._u2key(value)
        count = 0
        while True:
            i = self._advance(i, value)
            if i == -1:
                return count
            count += 1
            i += 1

    def __repr__(self):
        return ('sortedlist([%s])'
                % ', '.join(repr(self._i2u(v)) for v in self._blist))

class sortedset(sortedlist):
    def add(self, value):
        if value in self: return
        sortedlist.add(self, value)

    def __repr__(self):
        return ('sortedset([%s])'
                % ', '.join(repr(self._i2u(v)) for v in self._blist))

class weaksortedlist(sortedlist):
    def _bisect(self, value):
        key = self._u2key(value)
        lo = 0
        hi = len(self._blist)
        while lo < hi:
            mid = (lo+hi)//2
            n, v = self._squeeze(mid)
            hi -= n
            if n and hi == len(self._blist):
                continue
            if self._i2key(self._blist[mid]) < key: lo = mid+1
            else: hi = mid
        n, v = self._squeeze(lo)
        return lo, v

    def _u2i(self, value):
        if self._key is None:
            return weakref.ref(value)
        else:
            return (self._key(value), weakref.ref(value))

    def _i2u(self, value):
        if self._key is None:
            return value()
        else:
            return value[1]()

    def _i2key(self, value):
        if self._key is None:
            return value()
        else:
            return value[0]

    def __iter__(self):
        i = 0
        while i < len(self._blist):
            n, v = self._squeeze(i)
            if v is None: break
            yield v
            i += 1

    def _squeeze(self, i):
        n = 0
        while i < len(self._blist):
            v = self._i2u(self._blist[i])
            if v is None:
                del self._blist[i]
                n += 1
            else:
                return n, v
        return n, None

    def __getitem__(self, index):
        if isinstance(index, slice):
            return sortedlist.__getitem__(self, index)
        n, v = self._squeeze(index)
        if v is None:
            raise IndexError('list index out of range')
        return v

    def __reversed__(self):
        i = len(self._blist)-1
        while i >= 0:
            n, v = self._squeeze(i)
            if not n:
                yield v
            i -= 1

    def _advance(self, i, value):
        "Do a linear search through all items with the same key"
        key = self._u2key(value)
        while i < len(self._blist):
            n, v = self._squeeze(i)
            if v is None:
                break
            if v == value:
                return i
            elif key < self._i2key(self._blist[i]):
                break
            i += 1
        return -1

    def __repr__(self):
        store = [self._i2u(r) for r in self._blist]
        store = [r for r in store if r is not None]
        return 'weakreflist(%s)' % repr(store)

class weaksortedset(weaksortedlist):
    def add(self, value):
        if value in self: return
        weaksortedlist.add(self, value)

    def __repr__(self):
        store = [self._i2u(r) for r in self._blist]
        store = [r for r in store if r is not None]
        return 'weakrefset(%s)' % repr(store)
