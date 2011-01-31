from _sortedlist import sortedset
import collections, sys
from _blist import blist

class missingdict(dict):
    def __missing__(self, key):
        return self._missing(key)

class KeysView(collections.KeysView, collections.Sequence):
    def __getitem__(self, index):
        return self._mapping._sortedkeys[index]
    def __reversed__(self):
        return reversed(self._mapping._sortedkeys)
    def index(self, key):
        return self._mapping._sortedkeys.index(key)
    def count(self, key):
        return self._mapping.count(key)
    def _from_iterable(cls, it):
        return sortedset(key=self._mapping._sortedkeys.key)
    def bisect_left(self, key):
        return self._mapping._sortedkeys.bisect_left(key)
    def bisect_right(self, key):
        return self._mapping._sortedkeys.bisect_right(key)
    bisect = bisect_right

class ItemsView(collections.ItemsView, collections.Sequence):
    def __getitem__(self, index):
        if isinstance(index, slice):
            keys = self._mapping._sortedkeys[index]
            return self._from_iterable((key, self._mapping[key])
                                       for key in keys)
        key = self._mapping.sortedkeys[index]
        return (key, self._mapping[key])
    def index(self, item):
        key, value = item
        i = self._mapping._sortedkeys.index(key)
        if self._mapping[key] == value:
            return i
        raise ValueError
    def count(self, item):
        return 1 if item in self else 0
    def _from_iterable(cls, it):
        return sortedset(key=lambda item:
                             self._mapping._sortedkeys.key(item[0]))

class ValuesView(collections.ValuesView, collections.Sequence):
    def __getitem__(self, index):
        if isinstance(index, slice):
            keys = self._mapping._sortedkeys[index]
            rv = sortedset(key=self._mapping._sortedkeys.key)
            rv.update(self._mapping[key] for key in keys)
            return rv
        key = self._mapping.sortedkeys[index]
        return self._mapping[key]

class sorteddict(collections.MutableMapping):
    def __init__(self, *args, **kw):
        if hasattr(self, '__missing__'):
            self._map = missingdict()
            self._map._missing = self.__missing__
        else:
            self._map = dict()
        key = None
        if len(args) > 0:
            if hasattr(args[0], '__call__'):
                key = args[0]
                args = args[1:]
            elif len(args) > 1:
                raise TypeError("'%s' object is not callable" %
                                args[0].__class__.__name__)
        if len(args) > 1:
            raise TypeError('sorteddict expected at most 2 arguments, got %d'
                            % len(args))
        if len(args) == 1 and isinstance(args[0], sorteddict) and key is None:
            key = args[0]._sortedkeys._key
        self._sortedkeys = sortedset(key=key)
        self.update(*args, **kw)

    if sys.version_info[0] < 3:
        def keys(self):
            return self._sortedkeys.copy()
        def items(self):
            return blist((key, self[key]) for key in self)
        def values(self):
            return blist(self[key] for key in self)
        def viewkeys(self):
            return KeysView(self)
        def viewitems(self):
            return ItemsView(self)
        def viewvalues(self):
            return ValuesView(self)
    else:
        def keys(self):
            return KeysView(self)
        def items(self):
            return ItemsView(self)
        def values(self):
            return ValuesView(self)

    def __setitem__(self, key, value):
        try:
            if key not in self._map:
                self._sortedkeys.add(key)
            self._map[key] = value
        except:
            if key not in self._map:
                self._sortedkeys.discard(key)
            raise

    def __delitem__(self, key):
        self._sortedkeys.discard(key)
        del self._map[key]

    def __getitem__(self, key):
        return self._map[key]

    def __iter__(self):
        return iter(self._sortedkeys)

    def __len__(self):
        return len(self._sortedkeys)

    def copy(self):
        return sorteddict(self)

    @classmethod
    def fromkeys(cls, keys, value=None, key=None):
        if key is not None:
            rv = cls(key)
        else:
            rv = cls()
        for key in keys:
            rv[key] = value
        return rv

    def __repr__(self):
        return 'sorteddict(%s)' % repr(self._map)

    def __eq__(self, other):
        if not isinstance(other, sorteddict):
            return False
        return self._map == other._map
