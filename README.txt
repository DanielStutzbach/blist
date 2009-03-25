BList: a list-like type with better performance
===============================================

The BList is a type that looks, acts, and quacks like a Python list,
but has better performance for many (but not all) use cases.  The use
cases where the BList is slightly *slower* than Python's list are as
follows (O(log n) vs. O(1)):

1. A large lists where inserts and deletes are only at the end of the
   list (LIFO).

Earlier versions of BList were also slower for large lists that never
change length, but this is no longer true as of version 0.9.6, which
features amortized worst-case O(1) getitem and setitem operations.

With that disclaimer out of the way, here are some of the use cases
where the BLists is dramatically faster than the built-in list:

1. Insertion into or removal from a large list (O(log n) vs. O(n))
2. Taking large slices of large lists (O(log n) vs O(n))
3. Making shallow copies of large lists (O(1) vs. O(n))
4. Changing large slices of large lists (O(log n + log k) vs. O(n + k))
5. Multiplying a list to make a large, sparse list (O(log k) vs. O(kn))

You've probably noticed that we keep referring to "large lists".  For
small lists, BLists and the built-in list have very similar
performance.

So you can see the performance of the BList in more detail, several
performance graphs available at the following link: http://stutzbachenterprises.com/blist/

Example usage:

>>> from blist import *
>>> x = blist([0])             # x is a BList with one element
>>> x *= 2**29                 # x is a BList with > 500 million elements
>>> x.append(5)                # append to x
>>> y = x[4:-234234]           # Take a 500 million element slice from x
>>> del x[3:1024]              # Delete a few thousand elements from x

For comparison, on most systems the built-in list just raises
MemoryError and calls it a day.

The BList has two key features that allow it to pull off this
performance:

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

Installation instructions
-------------------------

Python 2.5 or higher is required.  If building from the source
distribution, the Python header files are also required.  In either
case, just run:

       python setup.py install

The BList module will be installed in the 'site-packages' directory of
your Python installation.  (Unless directed elsewhere; see the
"Installing Python Modules" section of the Python manuals for details
on customizing installation locations, etc.).

If you downloaded the source distribution and wish to run the
associated test suite, you can also run:

        python setup.py test

which will verify the correct installation and functioning of the
package.

Platforms
---------

The BList was developed under Debian Linux.  Since it has no
dependencies other Python, we expect it will work on other 32-bit gcc
platforms without modification.  If you run into trouble building
BList on your platform, please contact us.

Known bugs and limitations
--------------------------

64-bit architectures should work in theory, but have not been tested.

Out-of-memory errors are not always handled correctly and may cause
undefined behavior.  While we plan to fix this, we hope that it does
not arise in practice.

Feedback
--------

We're eager to hear about your experiences with the BList.  Please
send all feedback and bug reports to daniel@stutzbachenterprises.com.
