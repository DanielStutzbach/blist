blist: a list-like type with better performance
===============================================

The ``blist`` is a drop-in replacement for the Python list that provides
better performance when modifying large lists.  The blist package also
provides ``sortedlist``, ``sortedset``, ``weaksortedlist``,
``weaksortedset``, ``sorteddict``, and ``btuple`` types.

Full documentation is at the link below:

http://stutzbachenterprises.com/blist-doc/

Python's built-in list is a dynamically-sized array; to insert or
remove an item from the beginning or middle of the list, it has to
move most of the list in memory, i.e., O(n) operations.  The blist
uses a flexible, hybrid array/tree structure and only needs to move a
small portion of items in memory, specifically using O(log n)
operations.

For small lists, the blist and the built-in list have virtually
identical performance.

To use the blist, you simply change code like this:

>>> items = [5, 6, 2]
>>> more_items = function_that_returns_a_list()

to:

>>> from blist import blist
>>> items = blist([5, 6, 2])
>>> more_items = blist(function_that_returns_a_list())

Here are some of the use cases where the blist asymptotically
outperforms the built-in list:

========================================== ================  =========
Use Case                                   blist             list
========================================== ================  =========
Insertion into or removal from a list      O(log n)          O(n)
Taking slices of lists                     O(log n)          O(n)
Making shallow copies of lists             O(1)              O(n)
Changing slices of lists                   O(log n + log k)  O(n+k)
Multiplying a list to make a sparse list   O(log k)          O(kn)
Maintain a sorted lists with bisect.insort O(log**2 n)       O(n)
========================================== ================  =========

So you can see the performance of the blist in more detail, several
performance graphs available at the following link:
http://stutzbachenterprises.com/blist/

Example usage:

>>> from blist import *
>>> x = blist([0])             # x is a blist with one element
>>> x *= 2**29                 # x is a blist with > 500 million elements
>>> x.append(5)                # append to x
>>> y = x[4:-234234]           # Take a 500 million element slice from x
>>> del x[3:1024]              # Delete a few thousand elements from x

Other data structures
---------------------

The blist package provides other data structures based on the blist:

- sortedlist
- sortedset
- weaksortedlist
- weaksortedset
- sorteddict
- btuple

These additional data structures are only available in Python 2.6 or
higher, as they make use of Abstract Base Classes.

The sortedlist is a list that's always sorted.  It's iterable and
indexable like a Python list, but to modify a sortedlist the same
methods you would use on a Python set (add, discard, or remove).

>>> from blist import sortedlist
>>> my_list = sortedlist([3,7,2,1])
>>> my_list
sortedlist([1, 2, 3, 7])
>>> my_list.add(5)
>>> my_list[3]
5
>>>

The sortedlist constructor takes an optional "key" argument, which may
be used to change the sort order just like the sorted() function.

>>> from blist import sortedlist
>>> my_list = sortedlist([3,7,2,1], key=lambda i: -i)
sortedlist([7, 3, 2, 1]
>>>

The sortedset is a set that's always sorted.  It's iterable and
indexable like a Python list, but modified like a set.  Essentially,
it's just like a sortedlist except that duplicates are ignored.

>>> from blist import sortedset
>>> my_set = sortedset([3,7,2,2])
sortedset([2, 3, 7]
>>>

The weaksortedlist and weaksortedset are weakref variations of the
sortedlist and sortedset.

The sorteddict works just like a regular dict, except the keys are
always sorted.  The sorteddict should not be confused with Python
2.7's OrderedDict type, which remembers the insertion order of the
keys.

>>> from blist import sorteddict
>>> my_dict = sorteddict({1: 5, 6: 8, -5: 9})
>>> my_dict.keys()
[-5, 1, 6]
>>>

The btuple is a drop-in replacement for the built-in tuple.  Compared
to the built-in tuple, the btuple offers the following advantages:

- Constructing a btuple from a blist takes O(1) time.
- Taking a slice of a btuple takes O(n) time, where n is the size of
  the original tuple.  The size of the slice does not matter.

>>> from blist import blist, btuple
>>> x = blist([0])             # x is a blist with one element
>>> x *= 2**29                 # x is a blist with > 500 million elements
>>> y = btuple(x)              # y is a btuple with > 500 million elements

Installation instructions
-------------------------

Python 2.5 or higher is required.  If building from the source
distribution, the Python header files are also required.  In either
case, just run:

       python setup.py install

If you're running Linux and see a bunch of compilation errors from
GCC, you probably do not have the Python header files installed.
They're usually located in a package called something like
"python2.6-dev".

The blist package will be installed in the 'site-packages' directory of
your Python installation.  (Unless directed elsewhere; see the
"Installing Python Modules" section of the Python manuals for details
on customizing installation locations, etc.).

If you downloaded the source distribution and wish to run the
associated test suite, you can also run:

        python setup.py test

which will verify the correct installation and functioning of the
package.  The tests require Python 2.6 or higher.

Feedback
--------

We're eager to hear about your experiences with the blist.  You can
email me at daniel@stutzbachenterprises.com.  Alternately, bug reports
and feature requests may be reported on our bug tracker at:
http://github.com/DanielStutzbach/blist/issues

How we test
-----------

In addition to the tests include in the source distribution, we
perform the following to add extra rigor to our testing process:

    1. We use a "fuzzer": a program that randomly generates list
       operations, performs them using both the blist and the built-in
       list, and compares the results.

    2. We use a modified Python interpreter where we have replaced the
       array-based built-in list with the blist.  Then, we run all of
       the regular Python unit tests.
