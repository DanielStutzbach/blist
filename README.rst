BList: a list-like type with better performance
===============================================

The BList is a type that looks, acts, and quacks like a Python list,
but has better performance for for modifying large lists.  For small
lists (fewer than 128 elements), BLists and the built-in list have
very similar performance.

Here are some of the use cases where the BLists asymptotically
outperforms the built-in list:

========================================== ================  =========
Use Case                                   BList             list
========================================== ================  =========
Insertion into or removal from a list      O(log n)          O(n)
Taking slices of lists                     O(log n)          O(n)
Making shallow copies of lists             O(1)              O(n)
Changing slices of lists                   O(log n + log k)  O(n+k)
Multiplying a list to make a sparse list   O(log k)          O(kn)
Maintain a sorted lists with bisect.insort O(log**2 n)       O(n)
========================================== ================  =========

So you can see the performance of the BList in more detail, several
performance graphs available at the following link:
http://stutzbachenterprises.com/blist/

Example usage:

>>> from blist import *
>>> x = blist([0])             # x is a BList with one element
>>> x *= 2**29                 # x is a BList with > 500 million elements
>>> x.append(5)                # append to x
>>> y = x[4:-234234]           # Take a 500 million element slice from x
>>> del x[3:1024]              # Delete a few thousand elements from x

For comparison, on most systems the built-in list just raises
MemoryError and calls it a day.

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
package.  The tests regrettably do not work on Python 3.

Feedback
--------

We're eager to hear about your experiences with the BList.  Please
send all feedback and bug reports to daniel@stutzbachenterprises.com.
Bug reports and feature requests may be reported on our bug tracker
at: http://github.com/DanielStutzbach/blist/issues

How we test
-----------

In addition to the tests include in the source distribution, we
perform the following to additional rigor to our testing process:

    1. We use a "fuzzer": a program that randomly generates list
       operations, performs them using both the BList and the built-in
       list, and compares the results.

    2. We use a modified Python interpreter where we have replaced the
       array-based built-in list with the BList.  Then, we run all of
       the regular Python unit tests.
