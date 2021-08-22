# This file based loosely on Python's list_tests.py.

import sys
import collections, operator
import gc
import random
import blist
from blist.test import unittest
from blist.test import list_tests, seq_tests

_pyvers = sys.version_info.major * 1000 + sys.version_info.minor * 10

# Supported only in Python >= 2.6
if _pyvers >= 3000:
    from collections.abc import Set
else:
    from collections import Set


def CmpToKey(mycmp):
    'Convert a cmp= function into a key= function'
    class K(object):
        def __init__(self, obj):
            self.obj = obj
        def __lt__(self, other):
            return mycmp(self.obj, other.obj) == -1
    return K

class SortedBase(object):
    def build_items(self, n):
        return list(range(n))

    def build_item(self, x):
        return x

    def test_empty_repr(self):
        self.assertEqual('%s()' % self.type2test.__name__,
                         repr(self.type2test()))

    def validate_comparison(self, instance):
        if sys.version_info[0] < 3 and isinstance(instance, collections.Set):
            ops = ['ne', 'or', 'and', 'xor', 'sub']
        else:
            ops = ['lt', 'gt', 'le', 'ge', 'ne', 'or', 'and', 'xor', 'sub']
        operators = {}
        for op in ops:
            name = '__'+op+'__'
            operators['__'+op+'__'] = getattr(operator, name)

        class Other(object):
            def __init__(self):
                self.right_side = False
            def __eq__(self, other):
                self.right_side = True
                return True
            __lt__ = __eq__
            __gt__ = __eq__
            __le__ = __eq__
            __ge__ = __eq__
            __ne__ = __eq__
            __ror__ = __eq__
            __rand__ = __eq__
            __rxor__ = __eq__
            __rsub__ = __eq__

        for name, op in operators.items():
            if not hasattr(instance, name): continue
            other = Other()
            op(instance, other)
            self.assertTrue(other.right_side,'Right side not called for %s.%s'
                            % (type(instance), name))

    def test_right_side(self):
        self.validate_comparison(self.type2test())

    def test_delitem(self):
        items = self.build_items(2)
        a = self.type2test(items)
        del a[1]
        self.assertEqual(a, self.type2test(items[:1]))
        del a[0]
        self.assertEqual(a, self.type2test([]))

        a = self.type2test(items)
        del a[-2]
        self.assertEqual(a, self.type2test(items[1:]))
        del a[-1]
        self.assertEqual(a, self.type2test([]))

        a = self.type2test(items)
        self.assertRaises(IndexError, a.__delitem__, -3)
        self.assertRaises(IndexError, a.__delitem__, 2)

        a = self.type2test([])
        self.assertRaises(IndexError, a.__delitem__, 0)

        self.assertRaises(TypeError, a.__delitem__)

    def test_delslice(self):
        items = self.build_items(2)
        a = self.type2test(items)
        del a[1:2]
        del a[0:1]
        self.assertEqual(a, self.type2test([]))

        a = self.type2test(items)
        del a[1:2]
        del a[0:1]
        self.assertEqual(a, self.type2test([]))

        a = self.type2test(items)
        del a[-2:-1]
        self.assertEqual(a, self.type2test(items[1:]))

        a = self.type2test(items)
        del a[-2:-1]
        self.assertEqual(a, self.type2test(items[1:]))

        a = self.type2test(items)
        del a[1:]
        del a[:1]
        self.assertEqual(a, self.type2test([]))

        a = self.type2test(items)
        del a[1:]
        del a[:1]
        self.assertEqual(a, self.type2test([]))

        a = self.type2test(items)
        del a[-1:]
        self.assertEqual(a, self.type2test(items[:1]))

        a = self.type2test(items)
        del a[-1:]
        self.assertEqual(a, self.type2test(items[:1]))

        a = self.type2test(items)
        del a[:]
        self.assertEqual(a, self.type2test([]))

    def test_out_of_range(self):
        u = self.type2test()
        def del_test():
            del u[0]
        self.assertRaises(IndexError, lambda: u[0])
        self.assertRaises(IndexError, del_test)

    def test_bad_mul(self):
        u = self.type2test()
        self.assertRaises(TypeError, lambda: u * 'q')
        def imul_test():
            u = self.type2test()
            u *= 'q'
        self.assertRaises(TypeError, imul_test)

    def test_pop(self):
        lst = self.build_items(20)
        random.shuffle(lst)
        u = self.type2test(lst)
        for i in range(20-1,-1,-1):
            x = u.pop(i)
            self.assertEqual(x, i)
        self.assertEqual(0, len(u))

    def test_reversed(self):
        lst = list(range(20))
        a = self.type2test(lst)
        r = reversed(a)
        self.assertEqual(list(r), list(range(19, -1, -1)))
        if hasattr(r, '__next__'): # pragma: no cover
            self.assertRaises(StopIteration, r.__next__)
        else: # pragma: no cover
            self.assertRaises(StopIteration, r.next)
        self.assertEqual(self.type2test(reversed(self.type2test())),
                         self.type2test())

    def test_mismatched_types(self):
        class NotComparable:
            def __lt__(self, other): # pragma: no cover
                raise TypeError
            def __cmp__(self, other): # pragma: no cover
                raise TypeError
        NotComparable = NotComparable()

        item = self.build_item(5)
        sl = self.type2test()
        sl.add(item)
        self.assertRaises(TypeError, sl.add, NotComparable)
        self.assertFalse(NotComparable in sl)
        self.assertEqual(sl.count(NotComparable), 0)
        sl.discard(NotComparable)
        self.assertRaises(ValueError, sl.index, NotComparable)

    def test_order(self):
        stuff = [self.build_item(random.randrange(1000000))
                 for i in range(1000)]
        if issubclass(self.type2test, Set):
            stuff = set(stuff)
        sorted_stuff = list(sorted(stuff))
        u = self.type2test

        self.assertEqual(sorted_stuff, list(u(stuff)))
        sl = u()
        for x in stuff:
            sl.add(x)
        self.assertEqual(sorted_stuff, list(sl))
        x = sorted_stuff.pop(len(stuff)//2)
        sl.discard(x)
        self.assertEqual(sorted_stuff, list(sl))

    def test_constructors(self):
        # Based on the seq_test, but without adding incomparable types
        # to the list.

        l0 = self.build_items(0)
        l1 = self.build_items(1)
        l2 = self.build_items(2)

        u = self.type2test()
        u0 = self.type2test(l0)
        u1 = self.type2test(l1)
        u2 = self.type2test(l2)

        uu = self.type2test(u)
        uu0 = self.type2test(u0)
        uu1 = self.type2test(u1)
        uu2 = self.type2test(u2)

        v = self.type2test(tuple(u))
        class OtherSeq:
            def __init__(self, initseq):
                self.__data = initseq
            def __len__(self):
                return len(self.__data)
            def __getitem__(self, i):
                return self.__data[i]
        s = OtherSeq(u0)
        v0 = self.type2test(s)
        self.assertEqual(len(v0), len(s))

    def test_sort(self):
        # based on list_tests.py
        lst = [1, 0]
        lst = [self.build_item(x) for x in lst]
        u = self.type2test(lst)
        self.assertEqual(list(u), [0, 1])

        lst = [2,1,0,-1,-2]
        lst = [self.build_item(x) for x in lst]
        u = self.type2test(lst)
        self.assertEqual(list(u), [-2,-1,0,1,2])

        lst = list(range(512))
        lst = [self.build_item(x) for x in lst]
        a = self.type2test(reversed(lst))
        self.assertEqual(list(a), lst)

        def revcmp(a, b): # pragma: no cover
            if a == b:
                return 0
            elif a < b:
                return 1
            else: # a > b
                return -1
        u = self.type2test(u, key=CmpToKey(revcmp))
        self.assertEqual(list(u), [2,1,0,-1,-2])

        # The following dumps core in unpatched Python 1.5:
        def myComparison(x,y):
           xmod, ymod = x%3, y%7
           if xmod == ymod:
               return 0
           elif xmod < ymod:
               return -1
           else: # xmod > ymod
               return 1
        z = self.type2test(list(range(12)), key=CmpToKey(myComparison))

        self.assertRaises(TypeError, self.type2test, 42, 42, 42, 42)

class StrongSortedBase(SortedBase, seq_tests.CommonTest):
    def not_applicable(self):
        pass
    test_repeat = not_applicable
    test_imul = not_applicable
    test_addmul = not_applicable
    test_iadd = not_applicable
    test_getslice = not_applicable
    test_contains_order = not_applicable
    test_contains_fake = not_applicable

    def test_constructors2(self):
        s = "a seq"
        vv = self.type2test(s)
        self.assertEqual(len(vv), len(s))

        # Create from various iteratables
        for s in ("123", "", list(range(1000)), (1.5, 1.2), range(2000,2200,5)):
            for g in (seq_tests.Sequence, seq_tests.IterFunc,
                      seq_tests.IterGen, seq_tests.itermulti,
                      seq_tests.iterfunc):
                self.assertEqual(self.type2test(g(s)), self.type2test(s))
            self.assertEqual(self.type2test(seq_tests.IterFuncStop(s)),
                             self.type2test())
            self.assertEqual(self.type2test(c for c in "123"),
                             self.type2test("123"))
            self.assertRaises(TypeError, self.type2test,
                              seq_tests.IterNextOnly(s))
            self.assertRaises(TypeError, self.type2test,
                              seq_tests.IterNoNext(s))
            self.assertRaises(ZeroDivisionError, self.type2test,
                              seq_tests.IterGenExc(s))

class weak_int:
    def __init__(self, v):
        self.value = v
    def unwrap(self, other):
        if isinstance(other, weak_int):
            return other.value
        return other
    def __hash__(self):
        return hash(self.value)
    def __repr__(self): # pragma: no cover
        return repr(self.value)
    def __lt__(self, other):
        return self.value < self.unwrap(other)
    def __le__(self, other):
        return self.value <= self.unwrap(other)
    def __gt__(self, other):
        return self.value > self.unwrap(other)
    def __ge__(self, other):
        return self.value >= self.unwrap(other)
    def __eq__(self, other):
        return self.value == self.unwrap(other)
    def __ne__(self, other):
        return self.value != self.unwrap(other)
    def __mod__(self, other):
        return self.value % self.unwrap(other)
    def __neg__(self):
        return weak_int(-self.value)

class weak_manager():
    def __init__(self):
        self.all = [weak_int(i) for i in range(10)]
        self.live = [v for v in self.all if random.randrange(2)]
        random.shuffle(self.all)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        del self.all
        gc.collect()

class WeakSortedBase(SortedBase, unittest.TestCase):
    def build_items(self, n):
        return [weak_int(i) for i in range(n)]

    def build_item(self, x):
        return weak_int(x)

    def test_collapse(self):
        items = self.build_items(10)
        u = self.type2test(items)
        del items
        gc.collect()
        self.assertEqual(list(u), [])

    def test_sort(self):
        # based on list_tests.py
        x = [weak_int(i) for i in [1, 0]]
        u = self.type2test(x)
        self.assertEqual(list(u), list(reversed(x)))

        x = [weak_int(i) for i in [2,1,0,-1,-2]]
        u = self.type2test(x)
        self.assertEqual(list(u), list(reversed(x)))

        #y = [weak_int(i) for i in reversed(list(range(512)))]
        #a = self.type2test(y)
        #self.assertEqual(list(a), list(reversed(y)))

        def revcmp(a, b): # pragma: no cover
            if a == b:
                return 0
            elif a < b:
                return 1
            else: # a > b
                return -1
        u = self.type2test(u, key=CmpToKey(revcmp))
        self.assertEqual(list(u), x)

        # The following dumps core in unpatched Python 1.5:
        def myComparison(x,y):
           xmod, ymod = x%3, y%7
           if xmod == ymod:
               return 0
           elif xmod < ymod:
               return -1
           else: # xmod > ymod
               return 1
        x = [weak_int(i) for i in range(12)]
        z = self.type2test(x, key=CmpToKey(myComparison))

        self.assertRaises(TypeError, self.type2test, 42, 42, 42, 42)

    def test_constructor(self):
        with weak_manager() as m:
            wsl = self.type2test(m.all)
        self.assertEqual(list(wsl), m.live)

    def test_add(self):
        with weak_manager() as m:
            wsl = self.type2test()
            for x in m.all:
                wsl.add(x)
            del x
        self.assertEqual(list(wsl), m.live)

    def test_discard(self):
        with weak_manager() as m:
            wsl = self.type2test(m.all)
        x = m.live.pop(len(m.live)//2)
        wsl.discard(x)
        self.assertEqual(list(wsl), m.live)

    def test_contains(self):
        with weak_manager() as m:
            wsl = self.type2test(m.all)
        for x in m.live:
            self.assertTrue(x in wsl)
        self.assertFalse(weak_int(-1) in wsl)

    def test_iter(self):
        with weak_manager() as m:
            wsl = self.type2test(m.all)
        for i, x in enumerate(wsl):
            self.assertEqual(x, m.live[i])

    def test_getitem(self):
        with weak_manager() as m:
            wsl = self.type2test(m.all)
        for i in range(len(m.live)):
            self.assertEqual(wsl[i], m.live[i])

    def test_reversed(self):
        with weak_manager() as m:
            wsl = self.type2test(m.all)
        r1 = list(reversed(wsl))
        r2 = list(reversed(m.live))
        self.assertEqual(r1, r2)

        all = [weak_int(i) for i in range(6)]
        wsl = self.type2test(all)
        del all[-1]
        self.assertEqual(list(reversed(wsl)), list(reversed(all)))

    def test_index(self):
        with weak_manager() as m:
            wsl = self.type2test(m.all)
        for x in m.live:
            self.assertEqual(wsl[wsl.index(x)], x)
        self.assertRaises(ValueError, wsl.index, weak_int(-1))

    def test_count(self):
        with weak_manager() as m:
            wsl = self.type2test(m.all)
        for x in m.live:
            self.assertEqual(wsl.count(x), 1)
        self.assertEqual(wsl.count(weak_int(-1)), 0)

    def test_getslice(self):
        with weak_manager() as m:
            wsl = self.type2test(m.all)
        self.assertEqual(m.live, list(wsl[:]))

class SortedListMixin:
    def test_eq(self):
        items = self.build_items(20)
        u = self.type2test(items)
        v = self.type2test(items, key=lambda x: -x)
        self.assertNotEqual(u, v)

    def test_cmp(self):
        items = self.build_items(20)
        u = self.type2test(items)
        low = u[:10]
        high = u[10:]
        self.assert_(low != high)
        self.assert_(low == u[:10])
        self.assert_(low < high)
        self.assert_(low <= high, str((low, high)))
        self.assert_(high > low)
        self.assert_(high >= low)
        self.assertFalse(low == high)
        self.assertFalse(high < low)
        self.assertFalse(high <= low)
        self.assertFalse(low > high)
        self.assertFalse(low >= high)

        low = u[:5]
        self.assert_(low != high)
        self.assertFalse(low == high)

    def test_update(self):
        items = self.build_items(20)
        u = self.type2test()
        u.update(items)
        self.assertEqual(u, self.type2test(items))

    def test_remove(self):
        items = self.build_items(20)
        u = self.type2test(items)
        u.remove(items[-1])
        self.assertEqual(u, self.type2test(items[:19]))
        self.assertRaises(ValueError, u.remove, items[-1])

    def test_mul(self):
        items = self.build_items(2)
        u1 = self.type2test(items[:1])
        u2 = self.type2test(items)
        self.assertEqual(self.type2test(), u2*0)
        self.assertEqual(self.type2test(), 0*u2)
        self.assertEqual(self.type2test(), u2*0)
        self.assertEqual(self.type2test(), 0*u2)
        self.assertEqual(u2, u2*1)
        self.assertEqual(u2, 1*u2)
        self.assertEqual(u2, u2*1)
        self.assertEqual(u2, 1*u2)
        self.assertEqual(self.type2test(items + items), u2*2)
        self.assertEqual(self.type2test(items + items), 2*u2)
        self.assertEqual(self.type2test(items + items + items), 3*u2)
        self.assertEqual(self.type2test(items + items + items), u2*3)

        class subclass(self.type2test):
            pass
        u3 = subclass(items)
        self.assertEqual(u3, u3*1)
        self.assert_(u3 is not u3*1)

    def test_imul(self):
        items = self.build_items(2)
        items6 = items[:1]*3 + items[1:]*3
        u = self.type2test(items)
        u *= 3
        self.assertEqual(u, self.type2test(items6))
        u *= 0
        self.assertEqual(u, self.type2test([]))
        s = self.type2test([])
        oldid = id(s)
        s *= 10
        self.assertEqual(id(s), oldid)

    def test_repr(self):
        name = self.type2test.__name__
        u = self.type2test()
        self.assertEqual(repr(u), '%s()' % name)
        items = self.build_items(3)
        u.update(items)
        self.assertEqual(repr(u), '%s([0, 1, 2])' % name)
        u = self.type2test()
        u.update([u])
        self.assertEqual(repr(u), '%s([%s(...)])' % (name, name))

    def test_bisect(self):
        items = self.build_items(5)
        del items[0]
        del items[2] # We end up with [1, 2, 4]
        u = self.type2test(items, key=lambda x: -x) # We end up with [4, 2, 1]
        self.assertEqual(u.bisect_left(3), 1)
        self.assertEqual(u.bisect(2), 2) # bisect == bisect_right
        self.assertEqual(u.bisect_right(2), 2)

class SortedSetMixin:
    def test_duplicates(self):
        u = self.type2test
        ss = u()
        stuff = [weak_int(random.randrange(100000)) for i in range(10)]
        sorted_stuff = list(sorted(stuff))
        for x in stuff:
            ss.add(x)
        for x in stuff:
            ss.add(x)
        self.assertEqual(sorted_stuff, list(ss))
        x = sorted_stuff.pop(len(stuff)//2)
        ss.discard(x)
        self.assertEqual(sorted_stuff, list(ss))

    def test_eq(self):
        items = self.build_items(20)
        u = self.type2test(items)
        v = self.type2test(items, key=lambda x: -x)
        self.assertEqual(u, v)

    def test_remove(self):
        items = self.build_items(20)
        u = self.type2test(items)
        u.remove(items[-1])
        self.assertEqual(u, self.type2test(items[:19]))
        self.assertRaises(KeyError, u.remove, items[-1])

class SortedListTest(StrongSortedBase, SortedListMixin):
    type2test = blist.sortedlist

class WeakSortedListTest(WeakSortedBase, SortedListMixin):
    type2test = blist.weaksortedlist

    def test_advance(self):
        items = [weak_int(0), weak_int(0)]
        u = self.type2test(items)
        del items[0]
        gc.collect()
        self.assertEqual(u.count(items[0]), 1)

class SortedSetTest(StrongSortedBase, SortedSetMixin):
    type2test = blist.sortedset

class WeakSortedSetTest(WeakSortedBase, SortedSetMixin):
    type2test = blist.weaksortedset

    def test_repr(self):
        items = self.build_items(20)
        u = self.type2test(items)
        self.assertEqual(repr(u), 'weaksortedset(%s)' % repr(items))
