#!/usr/bin/python

"""
Copyright (C) 2007 Stutzbach Enterprises, LLC.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
"""


"""
Current known failures:
- .sort() is unimplemented
"""

import sys
import os

import unittest
from blist import BList
#BList = list
from test import test_support, list_tests

limit = 8
n = 512

class BListTest(list_tests.CommonTest):
    type2test = BList

    def test_truth(self):
        super(BListTest, self).test_truth()
        self.assert_(not BList())
        self.assert_(BList([42]))

    def test_identity(self):
        self.assert_(BList([]) is not BList([]))

    def test_len(self):
        super(BListTest, self).test_len()
        self.assertEqual(len(BList()), 0)
        self.assertEqual(len(BList([0])), 1)
        self.assertEqual(len(BList([0, 1, 2])), 3)

    def test_append(self):
        lst = BList()
        t = tuple(range(n))
        for i in range(n):
            lst.append(i)
            self.assertEqual(tuple(lst), t[:i+1])

    def test_delstuff(self):
        lst = BList(range(n))
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
        lst2 = BList(range(limit+1))
        self.assertEqual(tuple(lst2), tuple(range(limit+1)))
        del lst2[1]
        del lst2[-1]
        self.assertEqual(tuple(lst2), (0,) + tuple(range(2,limit)))
        
    def test_insert_and_del(self):
        lst = BList(range(n))
        t = tuple(range(n))
        lst.insert(200, 0)
        self.assertEqual(tuple(lst), (t[0:200] + (0,) + t[200:]))
        del lst[200:]
        self.assertEqual(tuple(lst), tuple(range(200)))

    def test_mul3(self):
        lst = BList(range(3))
        self.assertEqual(tuple(lst*3), tuple(range(3)*3))

    def test_extendspam(self):
        a = BList('spam')
        a.extend('eggs')
        self.assertEqual(list(a), list('spameggs'))

    def test_bigmul1(self):
        x = BList([0])
        for i in range(290) + [1000, 10000, 100000, 1000000, 10000000, 2**29]:
            self.assertEqual(len(x*i), i)

    def test_badinit(self):
        self.assertRaises(TypeError, BList, 0, 0, 0)

    def test_copyself(self):
        x = BList(range(n))
        x[:] = x

    def test_nohash(self):
        x = BList()
        d = {}
        self.assertRaises(TypeError, d.__setitem__, x, 5)

    def test_collapseboth(self):
        x = BList(range(512))
        del x[193:318]

    def test_collapseright(self):
        x = BList(range(512))
        del x[248:318]

    def test_badrepr(self):
        class BadExc(Exception):
            pass

        class BadRepr:
            def __repr__(self):
                raise BadExc

        x = BList([BadRepr()])
        self.assertRaises(BadExc, repr, x)
        x = BList(range(n))
        x.append(BadRepr())
        self.assertRaises(BadExc, repr, x)

    def test_slice0(self):
        x = BList(range(n))
        x[slice(5,3,1)] = []
        self.assertEqual(x, list(range(n)))
        x = BList(range(n))
        self.assertRaises(ValueError, x.__setitem__, slice(5,3,1), [5,3,2])
        del x[slice(5,3,1)]
        self.assertEqual(x, list(range(n)))

    def test_badindex(self):
        x = BList()
        self.assertRaises(TypeError, x.__setitem__, 's', 5)

    def test_comparelist(self):
        x = BList(range(n))
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
        x = BList(range(n))
        y = BList(range(n-1))
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
        x = BList(range(n))
        y = tuple(range(n))
        self.assert_(x != y)

    def test_indexempty(self):
        x = BList(range(10))
        self.assertRaises(ValueError, x.index, 'spam')

    def test_indexargs(self):
        x = BList(range(10))
        self.assertEqual(x.index(5,1,-1), 5)
        self.assertRaises(ValueError, x.index, 5, -1, -9)
        self.assertRaises(ValueError, x.index, 8, 1, 4)
        self.assertRaises(ValueError, x.index, 0, 1, 4)

    def test_reversebig(self):
        x = BList(range(n))
        x.reverse()
        self.assertEqual(x, range(n-1,-1,-1))

    def test_badconcat(self):
        x = BList()
        y = 'foo'
        self.assertRaises(TypeError, x.__add__, y)

    def test_bad_assign(self):
        x = BList(range(n))
        self.assertRaises(TypeError, x.__setitem__, slice(1,10,2), 5)

def test_main(verbose=None):
    test_support.run_unittest(BListTest)

    # verify reference counting
    import sys
    if verbose:
        import gc
        test_support.run_unittest(BListTest)
        #gc.set_debug(gc.DEBUG_STATS | gc.DEBUG_UNCOLLECTABLE | gc.DEBUG_INSTANCES | gc.DEBUG_LEAK)
        gc.collect()
        print len(gc.get_objects())


if __name__ == "__main__":
    test_main(verbose=True)
