from _sortedlist import sortedset
import collections

class sorteddict(collections.MutableMapping):
    __slots__ = ['_sortedkeys', '_map']
    
    def __init__(self, *args, **kw):
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
        self._sortedkeys = sortedset(key=key)
        self.update(*args, **kw)

    def __setitem__(self, key, value):
        try:
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
