# This file based loosely on Python's list_tests.py.

import unittest
import sys
from . import seq_tests
import blist
import random
import gc

def CmpToKey(mycmp):
    'Convert a cmp= function into a key= function'
    class K(object):
        def __init__(self, obj):
            self.obj = obj
        def __lt__(self, other):
            return mycmp(self.obj, other.obj) == -1
    return K

class SortedListTest(seq_tests.CommonTest):
    type2test = blist.sortedlist
    def not_applicable(self):
        pass
    test_repeat = not_applicable
    test_imul = not_applicable
    test_addmul = not_applicable
    test_iadd = not_applicable
    test_getslice = not_applicable
    test_contains_order = not_applicable
    test_contains_fake = not_applicable

    def test_sort(self):
        # based on list_tests.py
        u = self.type2test([1, 0])
        self.assertEqual(list(u), [0, 1])

        u = self.type2test([2,1,0,-1,-2])
        self.assertEqual(list(u), [-2,-1,0,1,2])

        a = self.type2test(reversed(list(range(512))))
        self.assertEqual(list(a), list(range(512)))

        def revcmp(a, b):
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

    def test_mismatched_types(self):
        class NotComparable:
            def __lt__(self, other):
                raise TypeError
            def __cmp__(self, other):
                raise TypeError
        NotComparable = NotComparable()

        sl = self.type2test()
        sl.add(5)
        self.assertRaises(TypeError, sl.add, NotComparable)
        self.assertFalse(NotComparable in sl)
        self.assertEqual(sl.count(NotComparable), 0)
        sl.discard(NotComparable)
        self.assertRaises(ValueError, sl.index, NotComparable)

    def test_order(self):
        stuff = [random.random() for i in range(1000)]
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

        l0 = []
        l1 = [0]
        l2 = [0, 1]

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

class SortedSetTest(SortedListTest, SortedSetMixin):
    type2test = blist.sortedset

class weak_int:
    def __init__(self, v):
        self.value = v
    def __repr__(self):
        return repr(self.value)
    def __lt__(self, other):
        return self.value < other.value
    def __eq__(self, other):
        return self.value == other.value
    def __mod__(self, other):
        if isinstance(other, int):
            return self.value % other
        return self.value % other.value

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

class WeakSortedListTest(unittest.TestCase):
    type2test = blist.weaksortedlist

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

        def revcmp(a, b):
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

class WeakSortedSetTest(WeakSortedListTest, SortedSetMixin):
    type2test = blist.weaksortedset
