#!/usr/bin/python
from __future__ import print_function

"""
Copyright 2007-2010 Stutzbach Enterprises, LLC (daniel@stutzbachenterprises.com)

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
   3. The name of the author may not be used to endorse or promote
      products derived from this software without specific prior written
      permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

"""


import sys
import os

import unittest, operator
import blist, pickle
from blist import _blist
#BList = list
from blist.test import test_support, list_tests, sortedlist_tests, btuple_tests
from blist.test import sorteddict_tests, test_set

limit = _blist._limit
n = 512//8 * limit

class BListTest(list_tests.CommonTest):
    type2test = blist.blist

    def test_delmul(self):
        x = self.type2test(list(range(10000)))
        for i in range(100):
            del x[len(x)//4:3*len(x)//4]
            x *= 2

    def test_truth(self):
        super(BListTest, self).test_truth()
        self.assert_(not self.type2test())
        self.assert_(self.type2test([42]))

    def test_identity(self):
        self.assert_(self.type2test([]) is not self.type2test([]))

    def test_len(self):
        super(BListTest, self).test_len()
        self.assertEqual(len(self.type2test()), 0)
        self.assertEqual(len(self.type2test([0])), 1)
        self.assertEqual(len(self.type2test([0, 1, 2])), 3)

    def test_append2(self):
        lst = self.type2test()
        t = tuple(range(n))
        for i in range(n):
            lst.append(i)
            self.assertEqual(tuple(lst), t[:i+1])

    def test_delstuff(self):
        lst = self.type2test(list(range(n)))
        t = tuple(range(n))
        x = lst[4:258]
        self.assertEqual(tuple(x), tuple(t[4:258]))
        x.append(-1)
        self.assertEqual(tuple(x), tuple(t[4:258] + (-1,)))
        self.assertEqual(tuple(lst), t)
        lst[200] = 6
        self.assertEqual(tuple(x), tuple(t[4:258] + (-1,)))
        self.assertEqual(tuple(lst), tuple(t[0:200] + (6,) + t[201:]))
        del lst[200]
        self.assertEqual(tuple(lst), tuple(t[0:200] + t[201:]))

    def test_del1(self):
        lst2 = self.type2test(list(range(limit+1)))
        self.assertEqual(tuple(lst2), tuple(range(limit+1)))
        del lst2[1]
        del lst2[-1]
        self.assertEqual(tuple(lst2), (0,) + tuple(range(2,limit)))

    def test_insert_and_del(self):
        lst = self.type2test(list(range(n)))
        t = tuple(range(n))
        lst.insert(200, 0)
        self.assertEqual(tuple(lst), (t[0:200] + (0,) + t[200:]))
        del lst[200:]
        self.assertEqual(tuple(lst), tuple(range(200)))

    def test_mul3(self):
        lst = self.type2test(list(range(3)))
        self.assertEqual(tuple(lst*3), tuple(list(range(3))*3))

    def test_mul(self):
        x = self.type2test(list(range(limit**2)))
        for i in range(10):
            self.assertEqual(len(x*i), i*limit**2)

    def test_extendspam(self):
        a = self.type2test('spam')
        a.extend('eggs')
        self.assertEqual(list(a), list('spameggs'))

    def test_bigmul1(self):
        x = self.type2test([0])
        for i in list(range(290)) + [1000, 10000, 100000, 1000000, 10000000, 2**29]:
            self.assertEqual(len(x*i), i)

    def test_badinit(self):
        self.assertRaises(TypeError, self.type2test, 0, 0, 0)

    def test_copyself(self):
        x = self.type2test(list(range(n)))
        x[:] = x

    def test_nohash(self):
        x = self.type2test()
        d = {}
        self.assertRaises(TypeError, d.__setitem__, x, 5)

    def test_collapseboth(self):
        x = self.type2test(list(range(512)))
        del x[193:318]

    def test_collapseright(self):
        x = self.type2test(list(range(512)))
        del x[248:318]

    def test_badrepr(self):
        class BadExc(Exception):
            pass

        class BadRepr:
            def __repr__(self):
                raise BadExc

        x = self.type2test([BadRepr()])
        self.assertRaises(BadExc, repr, x)
        x = self.type2test(list(range(n)))
        x.append(BadRepr())
        self.assertRaises(BadExc, repr, x)

    def test_slice0(self):
        x = self.type2test(list(range(n)))
        x[slice(5,3,1)] = []
        self.assertEqual(x, list(range(n)))
        x = self.type2test(list(range(n)))
        self.assertRaises(ValueError, x.__setitem__, slice(5,3,1), [5,3,2])
        del x[slice(5,3,1)]
        self.assertEqual(x, list(range(n)))

    def test_badindex(self):
        x = self.type2test()
        self.assertRaises(TypeError, x.__setitem__, 's', 5)

    def test_comparelist(self):
        x = self.type2test(list(range(n)))
        y = list(range(n-1))
        self.assert_(not (x == y))
        self.assert_(x != y)
        self.assert_(not (x < y))
        self.assert_(not (x <= y))
        self.assert_(x > y)
        self.assert_(x >= y)

        y = list(range(n))
        self.assert_(x == y)
        self.assert_(y == x)

        y[100] = 6
        self.assert_(not (x == y))
        self.assert_(x != y)

    def test_compareblist(self):
        x = self.type2test(list(range(n)))
        y = self.type2test(list(range(n-1)))
        self.assert_(not (x == y))
        self.assert_(x != y)
        self.assert_(not (x < y))
        self.assert_(not (x <= y))
        self.assert_(x > y)
        self.assert_(x >= y)

        y[100] = 6
        self.assert_(not (x == y))
        self.assert_(x != y)

    def test_comparetuple(self):
        x = self.type2test(list(range(n)))
        y = tuple(range(n))
        self.assert_(x != y)

    def test_indexempty(self):
        x = self.type2test(list(range(10)))
        self.assertRaises(ValueError, x.index, 'spam')

    def test_indexargs(self):
        x = self.type2test(list(range(10)))
        self.assertEqual(x.index(5,1,-1), 5)
        self.assertRaises(ValueError, x.index, 5, -1, -9)
        self.assertRaises(ValueError, x.index, 8, 1, 4)
        self.assertRaises(ValueError, x.index, 0, 1, 4)

    def test_reversebig(self):
        x = self.type2test(list(range(n)))
        x.reverse()
        self.assertEqual(x, list(range(n-1,-1,-1)))

    def test_badconcat(self):
        x = self.type2test()
        y = 'foo'
        self.assertRaises(TypeError, operator.add, x, y)

    def test_bad_assign(self):
        x = self.type2test(list(range(n)))
        self.assertRaises(TypeError, x.__setitem__, slice(1,10,2), 5)

    def sort_evil(self, after):
        class EvilCompare:
            count = 0
            num_raises = 0
            def __init__(self, x):
                self.x = x
            def __lt__(self, other):
                EvilCompare.count += 1
                if EvilCompare.count > after:
                    EvilCompare.num_raises += 1
                    raise ValueError
                return self.x < other.x

        x = self.type2test(EvilCompare(x) for x in range(n))
        from random import shuffle
        shuffle(x)
        self.assertRaises(ValueError, x.sort)
        self.assertEqual(EvilCompare.num_raises, 1)
        x = [a.x for a in x]
        x.sort()
        self.assertEquals(x, list(range(n)))

    def test_sort_evil_small(self):
        self.sort_evil(limit * 5)

    def test_sort_evil_big(self):
        self.sort_evil(n + limit)

    def test_big_extend(self):
        x = self.type2test([1])
        x.extend(range(n))
        self.assertEqual(tuple(x), (1,) + tuple(range(n)))

    def test_big_getslice(self):
        x = self.type2test([0]) * 65536
        self.assertEqual(len(x[256:512]), 256)

    def test_modify_original(self):
        x = self.type2test(list(range(1024)))
        y = x[:]
        x[5] = 'z'
        self.assertEqual(tuple(y), tuple(range(1024)))
        self.assertEqual(x[5], 'z')
        self.assertEqual(tuple(x[:5]), tuple(range(5)))
        self.assertEqual(tuple(x[6:]), tuple(range(6, 1024)))

    def test_modify_copy(self):
        x = self.type2test(list(range(1024)))
        y = x[:]
        y[5] = 'z'
        self.assertEqual(tuple(x), tuple(range(1024)))
        self.assertEqual(y[5], 'z')
        self.assertEqual(tuple(y[:5]), tuple(range(5)))
        self.assertEqual(tuple(y[6:]), tuple(range(6, 1024)))

    def test_bigsort(self):
        x = self.type2test(list(range(100000)))
        x.sort()

    def test_sort_twice(self):
        y = blist.blist(list(range(limit+1)))
        for i in range(2):
            x = blist.blist(y)
            x.sort()
            self.assertEqual(tuple(x), tuple(range(limit+1)))

    def test_LIFO(self):
        x = blist.blist()
        for i in range(1000):
            x.append(i)
        for j in range(1000-1,-1,-1):
            self.assertEqual(x.pop(), j)

    def pickle_test(self, pickler, x):
        y = pickler.dumps(x)
        z = pickler.loads(y)
        self.assertEqual(x, z)
        self.assertEqual(repr(x), repr(z))

    def pickle_tests(self, pickler):
        self.pickle_test(pickler, blist.blist())
        self.pickle_test(pickler, blist.blist(list(range(limit))))
        self.pickle_test(pickler, blist.blist(list(range(limit+1))))
        self.pickle_test(pickler, blist.blist(list(range(n))))

        x = blist.blist([0])
        x *= n
        self.pickle_test(pickler, x)
        y = blist.blist(x)
        y[5] = 'x'
        self.pickle_test(pickler, x)
        self.pickle_test(pickler, y)

    def test_pickle(self):
        self.pickle_tests(pickle)

    def test_types(self):
        type(blist.blist())
        type(iter(blist.blist()))
        type(iter(reversed(blist.blist())))

    def test_iterlen_empty(self):
        it = iter(blist.blist())
        if hasattr(it, '__next__'): # pragma: no cover
            self.assertRaises(StopIteration, it.__next__)
        else: # pragma: no cover
            self.assertRaises(StopIteration, it.next)
        self.assertEqual(it.__length_hint__(), 0)

    def test_sort_floats(self):
        x = blist.blist([0.1, 0.2, 0.3])
        x.sort()

tests = [BListTest,
         sortedlist_tests.SortedListTest,
         sortedlist_tests.WeakSortedListTest,
         sortedlist_tests.SortedSetTest,
         sortedlist_tests.WeakSortedSetTest,
         btuple_tests.bTupleTest,
         sorteddict_tests.sorteddict_test
         ]
tests += test_set.test_classes

def test_suite():
    suite = unittest.TestSuite()
    for test in tests:
        suite.addTest(unittest.TestLoader().loadTestsFromTestCase(test))
    return suite

def test_main(verbose=None):
    test_support.run_unittest(*tests)

if __name__ == "__main__":
    test_main(verbose=True)
