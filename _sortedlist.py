from _blist import blist
import collections, bisect, weakref, operator, itertools, sys, threading
try: # pragma: no cover
    izip = itertools.izip
except AttributeError: # pragma: no cover
    izip = zip

__all__ = ['sortedlist', 'weaksortedlist', 'sortedset', 'weaksortedset']

class ReprRecursion(object):
    local = threading.local()
    def __init__(self, ob):
        if not hasattr(self.local, 'repr_count'):
            self.local.repr_count = collections.defaultdict(int)
        self.ob_id = id(ob)
        self.value = self.ob_id in self.local.repr_count

    def __enter__(self):
        self.local.repr_count[self.ob_id] += 1
        return self.value

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.local.repr_count[self.ob_id] -= 1
        if self.local.repr_count[self.ob_id] == 0:
            del self.local.repr_count[self.ob_id]
        return False

class _sortedbase(collections.Sequence):
    def __init__(self, iterable=(), key=None):
        self._key = key
        if key is not None and not hasattr(key, '__call__'):
            raise TypeError("'%s' object is not callable" % str(type(key)))
        if ((isinstance(iterable,type(self))
             or isinstance(self,type(iterable)))
            and iterable._key is key):
            self._blist = blist(iterable._blist)
        else:
            self._blist = blist()
            for v in iterable:
                self.add(v)

    def _from_iterable(self, iterable):
        return self.__class__(iterable, self._key)

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

    def _bisect_left(self, v):
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
            if v < key: lo = mid + 1
            else: hi = mid
        if lo < len(self._blist):
            return lo, self._i2u(self._blist[lo])
        return lo, None

    def _bisect_right(self, v):
        """Same as _bisect_left, but go to the right of equal values"""

        key = self._u2key(v)
        lo = 0
        hi = len(self._blist)
        while lo < hi:
            mid = (lo+hi)//2
            v = self._i2key(self._blist[mid])
            if key < v: hi = mid
            else: lo = mid + 1
        if lo < len(self._blist):
            return lo, self._i2u(self._blist[lo])
        return lo, None

    def bisect_left(self, v):
        """L.bisect_left(v) -> index

        The return value i is such that all e in L[:i] have e < v, and
        all e in a[i:] have e >= v.  So if v already appears in the
        list, i points just before the leftmost v already there.
        """
        return self._bisect_left(v)[0]

    def bisect_right(self, v):
        """L.bisect_right(v) -> index

        Return the index where to insert item v in the list.

        The return value i is such that all e in a[:i] have e <= v,
        and all e in a[i:] have e > v.  So if v already appears in the
        list, i points just beyond the rightmost v already there.
        """
        return self._bisect_right(v)[0]

    bisect = bisect_right

    def add(self, value):
        """Add an element."""
        # Will throw a TypeError when trying to add an object that
        # cannot be compared to objects already in the list.
        i, _ = self._bisect_right(value)
        self._blist.insert(i, self._u2i(value))

    def discard(self, value):
        """Remove an element if it is a member.

        If the element is not a member, do nothing.

        """

        try:
            i, v = self._bisect_left(value)
        except TypeError:
            # Value cannot be compared with values already in the list.
            # Ergo, value isn't in the list.
            return
        i = self._advance(i, value)
        if i >= 0:
            del self._blist[i]

    def __contains__(self, value):
        """x.__contains__(y) <==> y in x"""
        try:
            i, v = self._bisect_left(value)
        except TypeError:
            # Value cannot be compared with values already in the list.
            # Ergo, value isn't in the list.
            return False
        i = self._advance(i, value)
        return i >= 0

    def __len__(self):
        """x.__len__() <==> len(x)"""
        return len(self._blist)

    def __iter__(self):
        """ x.__iter__() <==> iter(x)"""
        return (self._i2u(v) for v in self._blist)

    def __getitem__(self, index):
        """x.__getitem__(y) <==> x[y]"""
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
        """L.__reversed__() -- return a reverse iterator over the list"""
        return (self._i2u(v) for v in reversed(self._blist))

    def index(self, value):
        """L.index(value) -> integer -- return first index of value.

        Raises ValueError if the value is not present.

        """

        try:
            i, v = self._bisect_left(value)
        except TypeError:
            raise ValueError
        i = self._advance(i, value)
        if i >= 0:
            return i
        raise ValueError

    def count(self, value):
        """L.count(value) -> integer -- return number of occurrences of value"""
        try:
            i, _ = self._bisect_left(value)
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

    def pop(self, index=-1):
        """L.pop([index]) -> item -- remove and return item at index (default last).

        Raises IndexError if list is empty or index is out of range.

        """

        rv = self[index]
        del self[index]
        return rv

    def __delslice__(self, i, j):
        """x.__delslice__(i, j) <==> del x[i:j]

        Use of negative indices is not supported.

        """
        del self._blist[i:j]

    def __delitem__(self, i):
        """x.__delitem__(y) <==> del x[y]"""
        del self._blist[i]

class _weaksortedbase(_sortedbase):
    def _bisect_left(self, value):
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

    def _bisect_right(self, value):
        key = self._u2key(value)
        lo = 0
        hi = len(self._blist)
        while lo < hi:
            mid = (lo+hi)//2
            n, v = self._squeeze(mid)
            hi -= n
            if n and hi == len(self._blist):
                continue
            if key < self._i2key(self._blist[mid]): hi = mid
            else: lo = mid+1
        n, v = self._squeeze(lo)
        return lo, v

    _bisect = _bisect_right

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
        """ x.__iter__() <==> iter(x)"""
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
        """x.__getitem__(y) <==> x[y]"""
        if isinstance(index, slice):
            return _sortedbase.__getitem__(self, index)
        n, v = self._squeeze(index)
        if v is None:
            raise IndexError('list index out of range')
        return v

    def __reversed__(self):
        """L.__reversed__() -- return a reverse iterator over the list"""
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

class _listmixin(object):
    def remove(self, value):
        """L.remove(value) -- remove first occurrence of value.

        Raises ValueError if the value is not present.
        """

        del self[self.index(value)]

    def update(self, iterable):
        """L.update(iterable) -- add all elements from iterable into the list"""

        for item in iterable:
            self.add(item)

    def __mul__(self, k):
        if not isinstance(k, int):
            raise TypeError("can't multiply sequence by non-int of type '%s'"
                            % str(type(int)))
        rv = self.__class__()
        rv._key = self._key
        rv._blist = sum((blist([x])*k for x in self._blist), blist())
        return rv
    __rmul__ = __mul__

    def __imul__(self, k):
        if not isinstance(k, int):
            raise TypeError("can't multiply sequence by non-int of type '%s'"
                            % str(type(int)))
        self._blist = sum((blist([x])*k for x in self._blist), blist())
        return self

    def __eq__(self, other):
        """x.__eq__(y) <==> x==y"""
        return self._cmp_op(other, operator.eq)
    def __ne__(self, other):
        """x.__ne__(y) <==> x!=y"""
        return self._cmp_op(other, operator.ne)
    def __lt__(self, other):
        """x.__lt__(y) <==> x<y"""
        return self._cmp_op(other, operator.lt)
    def __gt__(self, other):
        """x.__gt__(y) <==> x>y"""
        return self._cmp_op(other, operator.gt)
    def __le__(self, other):
        """x.__le__(y) <==> x<=y"""
        return self._cmp_op(other, operator.le)
    def __ge__(self, other):
        """x.__ge__(y) <==> x>=y"""
        return self._cmp_op(other, operator.ge)

class _setmixin(object):
    "Methods that override our base class"

    def add(self, value):
        """Add an element to the set.

        This has no effect if the element is already present.

        """
        if value in self: return
        super(_setmixin, self).add(value)

    def __iter__(self):
        it = super(_setmixin, self).__iter__()
        while True:
            item = next(it)
            n = len(self)
            yield item
            if n != len(self):
                raise RuntimeError('Set changed size during iteration')

def safe_cmp(f):
    def g(self, other):
        if not isinstance(other, collections.Set):
            raise TypeError("can only compare to a set")
        return f(self, other)
    return g

class _setmixin2(collections.MutableSet):
    "methods that override or supplement the collections.MutableSet methods"

    __ror__ = collections.MutableSet.__or__
    __rand__ = collections.MutableSet.__and__
    __rxor__ = collections.MutableSet.__xor__

    if sys.version_info[0] < 3: # pragma: no cover
        __lt__ = safe_cmp(collections.MutableSet.__lt__)
        __gt__ = safe_cmp(collections.MutableSet.__gt__)
        __le__ = safe_cmp(collections.MutableSet.__le__)
        __ge__ = safe_cmp(collections.MutableSet.__ge__)

    def __ior__(self, it):
        if self is it:
            return self
        for value in it:
            self.add(value)
        return self

    def __isub__(self, it):
        if self is it:
            self.clear()
            return self
        for value in it:
            self.discard(value)
        return self

    def __ixor__(self, it):
        if self is it:
            self.clear()
            return self
        for value in it:
            if value in self:
                self.discard(value)
            else:
                self.add(value)
        return self

    def __rsub__(self, other):
        return self._from_iterable(other) - self

    def _make_set(self, iterable):
        if isinstance(iterable, collections.Set):
            return iterable
        return self._from_iterable(iterable)

    def difference(self, *args):
        """Return a new set with elements in the set that are not in the others."""
        rv = self.copy()
        rv.difference_update(*args)
        return rv

    def intersection(self, *args):
        """Return a new set with elements common to the set and all others."""
        rv = self.copy()
        rv.intersection_update(*args)
        return rv

    def issubset(self, other):
        """Test whether every element in the set is in *other*."""
        return self <= self._make_set(other)

    def issuperset(self, other):
        """Test whether every element in *other* is in the set."""
        return self >= self._make_set(other)

    def symmetric_difference(self, other):
        """Return a new set with elements in either the set or *other*
        but not both."""

        return self ^ self._make_set(other)

    def union(self, *args):
        """Return the union of sets as a new set.

        (i.e. all elements that are in either set.)

        """
        rv = self.copy()
        for arg in args:
            rv |= self._make_set(arg)
        return rv

    def update(self, *args):
        """Update the set, adding elements from all others."""
        for arg in args:
            self |= self._make_set(arg)

    def difference_update(self, *args):
        """Update the set, removing elements found in others."""
        for arg in args:
            self -= self._make_set(arg)

    def intersection_update(self, *args):
        """Update the set, keeping only elements found in it and all others."""
        for arg in args:
            self &= self._make_set(arg)

    def symmetric_difference_update(self, other):
        """Update the set, keeping only elements found in either set,
        but not in both."""

        self ^= self._make_set(other)

    def clear(self):
        """Remove all elements"""
        del self._blist[:]

    def copy(self):
        return self[:]

class sortedlist(_sortedbase, _listmixin):
    """sortedlist(iterable=(), key=None) -> new sorted list

    Keyword arguments:
    iterable -- items used to initially populate the sorted list
    key -- a function to return the sort key of an item

    A sortedlist is indexable like a list, but always keeps its
    members in sorted order.

    """

    def __repr__(self):
        """x.__repr__() <==> repr(x)"""
        if not self: return 'sortedlist()'
        with ReprRecursion(self) as r:
            if r: return 'sortedlist(...)'
            return ('sortedlist(%s)' % repr(list(self)))

    def _cmp_op(self, other, op):
        if not (isinstance(other,type(self)) or isinstance(self,type(other))):
            return NotImplemented
        if len(self) != len(other):
            if op is operator.eq:
                return False
            if op is operator.ne:
                return True
        for x, y in izip(self, other):
            if x != y:
                return op(x, y)
        return op in (operator.eq, operator.le, operator.ge)

class weaksortedlist(_listmixin, _weaksortedbase):
    """weaksortedlist(iterable=(), key=None) -> new sorted weak list

    Keyword arguments:
    iterable -- items used to initially populate the sorted list
    key -- a function to return the sort key of an item

    A weaksortedlist is indexable like a list, but always keeps its
    items in sorted order.  The weaksortedlist weakly references its
    members, so items will be discarded after there is no longer a
    strong reference to the item.

    """

    def __repr__(self):
        """x.__repr__() <==> repr(x)"""
        if not self: return 'weaksortedlist()'
        with ReprRecursion(self) as r:
            if r: return 'weaksortedlist(...)'
            return 'weaksortedlist(%s)' % repr(list(self))

    def _cmp_op(self, other, op):
        if not (isinstance(other,type(self)) or isinstance(self,type(other))):
            return NotImplemented
        for x, y in izip(self, other):
            if x != y:
                return op(x, y)
        return op in (operator.eq, operator.le, operator.ge)

class sortedset(_setmixin, _sortedbase, _setmixin2):
    """sortedset(iterable=(), key=None) -> new sorted set

    Keyword arguments:
    iterable -- items used to initially populate the sorted set
    key -- a function to return the sort key of an item

    A sortedset is similar to a set but is also indexable like a list.
    Items are maintained in sorted order.

    """

    def __repr__(self):
        """x.__repr__() <==> repr(x)"""
        if not self: return 'sortedset()'
        with ReprRecursion(self) as r:
            if r: return 'sortedset(...)'
            return ('sortedset(%s)' % repr(list(self)))

class weaksortedset(_setmixin, _weaksortedbase, _setmixin2):
    """weaksortedset(iterable=(), key=None) -> new sorted weak set

    Keyword arguments:
    iterable -- items used to initially populate the sorted set
    key -- a function to return the sort key of an item

    A weaksortedset is similar to a set but is also indexable like a
    list.  Items are maintained in sorted order.  The weaksortedset
    weakly references its members, so items will be discarded after
    there is no longer a strong reference to the item.

    """

    def __repr__(self):
        """x.__repr__() <==> repr(x)"""
        if not self: return 'weaksortedset()'
        with ReprRecursion(self) as r:
            if r: return 'weaksortedset(...)'
            return 'weaksortedset(%s)' % repr(list(self))
