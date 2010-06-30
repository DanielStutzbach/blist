from . import mapping_tests
import blist

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

    def test_repr(self):
        d = self._empty_mapping()
        self.assertEqual(repr(d), 'sorteddict({})')
        d[1] = 2
        self.assertEqual(repr(d), 'sorteddict({1: 2})')
        d = self._empty_mapping()
        d[1] = d
        self.assertEqual(repr(d), 'sorteddict({1: sorteddict({...})})')

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
