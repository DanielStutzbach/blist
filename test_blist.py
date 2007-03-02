#!/usr/bin/python
"""
Tests common to BList
"""

import sys
import os

import unittest
#import blist
import cblist as blist
from test import test_support, list_tests

class BListTest(list_tests.CommonTest):
    type2test = blist.BList

    def test_truth(self):
        super(BListTest, self).test_truth()
        self.assert_(not [])
        self.assert_([42])

    def test_identity(self):
        self.assert_([] is not [])

    def test_len(self):
        super(BListTest, self).test_len()
        self.assertEqual(len([]), 0)
        self.assertEqual(len([0]), 1)
        self.assertEqual(len([0, 1, 2]), 3)

def test_main(verbose=None):
    test_support.run_unittest(BListTest)

    # verify reference counting
    import sys
    if verbose:
        import gc
        for i in xrange(5):
            test_support.run_unittest(BListTest)
            gc.set_debug(gc.DEBUG_STATS | gc.DEBUG_UNCOLLECTABLE | gc.DEBUG_INSTANCES)
            gc.collect()


if __name__ == "__main__":
    test_main(verbose=True)
