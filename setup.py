#!/usr/bin/env

import ez_setup
ez_setup.use_setuptools()

from setuptools import setup, Extension

setup(name='blist',
      version='0.9.2',
      description='a list-like type with better asymptotic performance',
      author='Stutzbach Enterprises, LLC',
      author_email='daniel@stutzbachenterprises.com',
      url='http://stutzbachenterprises.com/blist/',
      license = "BSD",
      keywords = "blist list b+tree btree fast copy-on-write sparse",
      ext_modules=[Extension('blist', ['blist.c'])],
      provides = ['blist'],
      test_suite = "test_blist.test_suite",
      install_requires = ["python>=2.5"],
      zip_safe = False, # zips are broken on cygwin for C extension modules
      classifiers = [
            'Development Status :: 4 - Beta',
            'Intended Audience :: Developers',
            'Intended Audience :: Science/Research',
            'License :: OSI Approved :: BSD License',
            'Programming Language :: C',
            ],

      long_description="""
BList: a list-like type with better asymptotic performance
==========================================================

The BList is a type that looks, acts, and quacks like a Python list(),
but has better performance for many (but not all) use cases.  Below
are some of the unique features of the BList:

- just as fast as a Python list() when the list is small
- insertion or removal from the list takes O(log n) time
- getslice runs in O(log n) time and uses O(log n) memory, regardless of slice size
- making a shallow copy runs in O(1) time and uses O(1) memory
- setslice runs in O(log n + log k) time if the inserted slice is a BList of length k
- multipling a BList by k takes O(log k) time and O(log k) memory

Example:

>>> from blist import *
>>> x = blist([0])
>>> x *= 2**29
>>> x.append(5)
>>> y = x[4:-234234]
>>> del x[3:1024]

None of the above operations have a noticeable delay, even though the
lists have over 500 million elements due to line 3.  The BList has two
key features that allow it to pull this off this performance:

1. Internally, a B+Tree is a wide, squat tree.  Each node has a
   maximum of 128 children.  If the entire list contains 128 or fewer
   objects, then there is only one node, which simply contains an array
   of the objects.  In other words, for short lists, a BList works just
   like Python's array-based list() type.  Thus, it has the same good
   performance on small lists.

2. The BList type features transparent copy-on-write.  If a non-root
   node needs to be copied (as part of a getslice, copy, setslice, etc.),
   the node is shared between multiple parents instead of being copied.
   If it needs to be modified later, it will be copied at that time.
   This is completely behind-the-scenes; from the user's point of view,
   the BList works just like a regular Python list.

So you can see the performance of the BList in more detail, several
performance graphs available at the following link: http://stutzbachenterprises.com/blist/
""",
            
      )
