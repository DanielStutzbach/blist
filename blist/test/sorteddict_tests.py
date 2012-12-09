import bisect
import sys
import blist
from blist.test import mapping_tests

def CmpToKey(mycmp):
    'Convert a cmp= function into a key= function'
    class K(object):
        def __init__(self, obj):
            self.obj = obj
        def __lt__(self, other):
            return mycmp(self.obj, other.obj) == -1
    return K

class sorteddict_test(mapping_tests.TestHashMappingProtocol):
    type2test = blist.sorteddict

    def _reference(self):
        """Return a dictionary of values which are invariant by storage
        in the object under test."""
        return {1:2, 3:4, 5:6}

    class Collider(object):
        def __init__(self, x):
            self.x = x
        def __repr__(self):
            return 'Collider(%r)' % self.x
        def __eq__(self, other):
            return self.__class__ == other.__class__ and self.x == other.x
        def __lt__(self, other):
            if type(self) != type(other):
                return NotImplemented
            return self.x < other.x
        def __hash__(self):
            return 42

    def test_repr(self):
        d = self._empty_mapping()
        self.assertEqual(repr(d), 'sorteddict({})')
        d[1] = 2
        self.assertEqual(repr(d), 'sorteddict({1: 2})')
        d = self._empty_mapping()
        d[1] = d
        self.assertEqual(repr(d), 'sorteddict({1: sorteddict({...})})')
        d = self._empty_mapping()
        d[self.Collider(1)] = 1
        d[self.Collider(2)] = 2
        self.assertEqual(repr(d),
                         'sorteddict({Collider(1): 1, Collider(2): 2})')
        d = self._empty_mapping()
        d[self.Collider(2)] = 2
        d[self.Collider(1)] = 1
        self.assertEqual(repr(d),
                         'sorteddict({Collider(1): 1, Collider(2): 2})')

        class Exc(Exception): pass

        class BadRepr(object):
            def __repr__(self):
                raise Exc()

        d = self._full_mapping({1: BadRepr()})
        self.assertRaises(Exc, repr, d)

    def test_mutatingiteration(self):
        pass

    def test_sort(self):
        u = self.type2test.fromkeys([1, 0])
        self.assertEqual(list(u.keys()), [0, 1])

        u = self.type2test.fromkeys([2,1,0,-1,-2])
        self.assertEqual(u, self.type2test.fromkeys([-2,-1,0,1,2]))
        self.assertEqual(list(u.keys()), [-2,-1,0,1,2])
        
        a = self.type2test.fromkeys(reversed(list(range(512))))
        self.assertEqual(list(a.keys()), list(range(512)))
        
        def revcmp(a, b): # pragma: no cover
            if a == b:
                return 0
            elif a < b:
                return 1
            else: # a > b
                return -1
        u = self.type2test.fromkeys([2,1,0,-1,-2], key=CmpToKey(revcmp))
        self.assertEqual(list(u.keys()), [2,1,0,-1,-2])
        
        # The following dumps core in unpatched Python 1.5:
        def myComparison(x,y):
           xmod, ymod = x%3, y%7
           if xmod == ymod:
               return 0
           elif xmod < ymod:
               return -1
           else: # xmod > ymod
               return 1

        self.type2test.fromkeys(list(range(12)), key=CmpToKey(myComparison))

        #def selfmodifyingComparison(x,y):
        #    z[x+y] = None
        #    return cmp(x, y)
        #z = self.type2test(CmpToKey(selfmodifyingComparison))
        #self.assertRaises(ValueError, z.update, [(i,i) for i in range(12)])
        
        self.assertRaises(TypeError, self.type2test.fromkeys, 42, 42, 42, 42)

    def test_view_indexing_without_key(self):
      self._test_view_indexing(key=None)

    def test_view_indexing_with_key(self):
      self._test_view_indexing(key=lambda x: -x)

    def _test_view_indexing(self, key):
      expected_items = [(3, "first"), (7, "second")]
      if key is not None:
        u = self.type2test(key, expected_items)
        expected_items.sort(key=lambda item: key(item[0]))
      else:
        u = self.type2test(expected_items)
        expected_items.sort()
      expected_keys, expected_values = list(zip(*expected_items))

      if sys.version_info[0] < 3:
        keys = u.viewkeys()
        values = u.viewvalues()
        items = u.viewitems()
      else:
        keys = u.keys()
        values = u.values()
        items = u.items()

      for i in range(len(expected_items)):
        self.assertEqual(keys[i], expected_keys[i])
        self.assertEqual(values[i], expected_values[i])
        self.assertEqual(items[i], expected_items[i])

      for i in range(-1, len(expected_items)+1):
        for j in range(-1, len(expected_items)+1):
          self.assertEqual(keys[i:j], blist.sortedset(expected_keys[i:j]))
          self.assertEqual(values[i:j], list(expected_values[i:j]))
          self.assertEqual(items[i:j], blist.sortedset(expected_items[i:j]))

      self.assertEqual(list(reversed(keys)), list(reversed(expected_keys)))
      for i, key in enumerate(expected_keys):
        self.assertEqual(keys.index(key), expected_keys.index(key))
        self.assertEqual(keys.count(key), 1)
        self.assertEqual(keys.bisect_left(key), i)
        self.assertEqual(keys.bisect_right(key), i+1)
      self.assertEqual(keys.count(object()), 0)
      self.assertRaises(ValueError, keys.index, object())

      for item in expected_items:
        self.assertEqual(items.index(item), expected_items.index(item))
        self.assertEqual(items.count(item), 1)
      self.assertEqual(items.count((7, "foo")), 0)
      self.assertEqual(items.count((object(), object())), 0)
      self.assertRaises(ValueError, items.index, (7, "foo"))
      self.assertRaises(ValueError, items.index, (object(), object()))
