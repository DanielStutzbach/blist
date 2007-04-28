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
key features that allow it to pull off this performance:

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

BList addiction
---------------

If you fall in love with the BList, you can even do this:
    __builtins__.list = blist

Then, everywhere in your program that calls list() will create a BList
instead.  List comprehensions, [], and built-ins will still return
ordinary list objects, however.

Installation instruction
------------------------

Python 2.5 or 2.5.1 is required.  If building from the source
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
dependencies other Python 2.5, we expect it will work on other 32-bit
gcc platforms without modification.  If you run into trouble building
BList on your platform, please contact us.

Known bugs and limitations
--------------------------

64-bit architectures should work in theory, but have not been tested.

Objects with __del__ methods that modify the BList that triggered the
__del__ may cause undefined behavior.  While we plan to fix this, we
also suggest you rethink your code.

Out-of-memory errors are not always handled correctly and may cause
undefined behavior.  While we plan to fix this, we hope that it does
not arise in practice.

Feedback
--------

We're eager to hear about your experiences with the BList.  Please
send all feedback and bug reports to daniel@stutzbachenterprises.com.
