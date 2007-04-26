#!/usr/bin/python

"""

Copyright 2007 Stutzbach Enterprises, LLC (daniel@stutzbachenterprises.com)

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

Motivation and Design Goals
---------------------------

The goal of this module is to provide a list-like type that has
better asymptotic performance than Python lists, while maintaining
similar performance for lists with few items.

I was driven to write this type by the work that I do.  I frequently
need to work with large lists, and run into efficiency problems when I
need to insert or delete elements in the middle of the list.  I'd like
a type that looks, acts, and quacks like a Python list while offering
good asymptotic performance for all common operations.

A could make a type that has good asymptotic performance, but poor
relative performance on small lists.  That'd be pretty easy to achieve
with, say, red-black trees.  While sometimes I do need good asymptotic
performance, other times I need the speed of Python's array-based
lists for operating on a small list in a tight loop.  I don't want to
have to think about which one to use.  I want one type with good
performance in both cases.

In other words, it should "just work".

I don't propose replacing the existing Python list implementation.  I
am neither that ambitious, and I appreciate how tightly optimized and
refined the existing list implementation is.  I would like to see this
type someday included in Python's collections module, so users with
similar needs can make use of it.  

The data structure I've created to solve the problem is a variation of
a B+Tree, hence I call it the "BList".  It has good asymptotic
performance for all operations, even for some operations you'd expect
to still be O(N).  For example:

    >>> from blist import BList
    >>> n = 10000000               # n = 10 million
    >>> b = BList([0])             # O(1)
    >>> bigb = b * n               # O(log n)
    >>> bigb2 = bigb[1:-1]         # O(log n)
    >>> del bigb2[5000000]         # O(log n)
    
With BLists, even taking a slice (line 4) takes O(log n) time.  This
wonderful feature is because BLists can implement copy-on-write.  More
on that later.

Thus far, I have only implemented a Python version of BList, as a
working prototype.  Obviously, the Python implementation isn't very
efficient at all, but it serves to illustrate the important algorithms.
Later, I plan to implement a C version, which should have comparable
performance to ordinary Python lists when operating on small lists.
The Python version of BLists only outperforms Python lists when the
lists are VERY large.

Basic Idea
----------

BLists are based on B+Trees are a dictionary data structure where each
element is a (key, value) pair and the keys are kept in sorted order.
B+Trees internally use a tree representation.  All data is stored in
the leaf nodes of the tree, and all leaf nodes are at the same level.
Unlike binary trees, each node has a large number of children, stored
as an array of references within the node.  The B+Tree operations ensure
that each node always has between "limit/2" and "limit" children
(except the root which may have between 0 and "limit" children).  When
a B+Tree has fewer than "limit/2" elements, they will all be contained
in a single node (the root).

Wikipedia has a diagram that may be helpful for understanding the
basic structure of a B+Tree:
    http://en.wikipedia.org/wiki/B+_tree

Of course, we don't want a dictionary.  We want a list.  

In BLists, the "key" is implicit: it's the in-order location of the value.
Instead of keys, each BList node maintains a count of the total number
of data elements beneath it in the tree.  This allows walking the tree
efficiently by keeping track of how far we've moved when passing by a
child node.  The tree structure gives us O(log n) for most operations,
asymptotically.

When the BList has fewer than "limit/2" data elements, they are all
stored in the root node.  In other words, for small lists a BList
essentially reduces to an array.  It should have almost identical
performance to a regular Python list, as only one or two extra if()
statements will be needed per method call.

Adding elements
---------------

Elements are inserted recursively.  Each node determines which child
node contains the insertion point, and calls the insertion routine of
that child.

When we add elements to a BList node, the node may overflow (i.e.,
have more than "limit" elements).  Instead of overflowing, the node
creates a new BList node and gives half of its elements to the new
node.  When the inserting function returns, the function informs its
parent about the new sibling.  This causes the parent to add the new
node as a child.  If this causes the parent to overflow, it creates a
sibling of its own, notifies its parent, and so on.

When the root of the tree overflows, it must increase the depth of the
tree.  The root creates two new children and splits all of its former
references between these two children (i.e., all former children are now
grandchildren).

Removing an element
-------------------

Removing an element is also done recursively.  Each node determines
which child node contains the element to be removed, and calls the
removal routine of that child.

Removing an element may cause an underflow (i.e., fewer than "limit/2"
elements).  It's the parent's job to check if a child has underflowed
after any operation that might cause an underflow.  The parent must
then repair the child, either by borrowing elements from one of the
child's sibling or merging the child with one of its sibling.  It the
parent performs a merge, this may also cause its parent to underflow.

If a node has only one element, the tree collapses.  The node replaces
its one child with its grandchildren.  When removing a single element,
this can only happen at the root.

Removing a range
----------------

The __delslice__ method to remove a range of elements is the most
complex operation for a BList to perform.  The first step is to locate
the common parent of all the elements to be removed.  The parent
deletes any children who will be completely deleted (i.e., they are
entirely within the range to be deleted).  The parent also has to deal
with two children who may be partially deleted: they contain the left
and right boundaries of the deletion range.

The parent calls the deletion operation recursively on these two
children.  When the call returns, the children must return a valid
BList, but they may be in an underflow state, and, worse, they may
have needed to collapse the tree.  To make life a little easier, the
children return an integer indicating how many levels of the tree
collapsed (if any).  The parent now has two adjacent subtrees of
different heights that need to be put back into the main tree (to keep
it balanced).

To accomplish this goal, we use a merge-tree operation, defined below.
The parent merges the two adjacent subtrees into a single subtree,
then merges the subtree with one of its other children.  If it has no
other children, then the parent collapses to become the subtree and
indicates to its parent the total level of collapse.

Merging subtrees
----------------

The __delslice__ method needs a way to merge two adjacent subtrees of
potentially different heights.  Because we only need to merge *adjacent*
subtrees, we don't have to handle inserting a subtree into the middle of
another.  There are only two cases: the far-left and the far-right.  If
the two subtrees are the same height, this is a pretty simple operation where
we join their roots together.  If the trees are different heights, we
merge the smaller into the larger as follows.  Let H be the difference
in their heights.  Then, recurse through the larger tree by H levels
and insert the smaller subtree there.

Retrieving a range and copy-on-write
------------------------------------

One of the most powerful features of BLists is the ability to support
copy-on-write.  Thus far we have described a BLists as a tree
structure where parents contain references to their children.  None of
the basic tree operations require the children to maintain references
to their parents or siblings.  Therefore, it is possible for a child
to have *multiple parents*.  The parents can happily share the child
as long as they perform read-only operations on it.  If a parent wants
to modify a child in any way, it first checks the child's reference
count.  If it is 1, the parent has the only reference and can proceed.
Otherwise, the parent must create a copy of the child, and relinquish
its reference to the child.

Creating a copy of a child doesn't implicitly copy the child's
subtree.  It just creates a new node with a new reference to the
child.  In other words, the child and the copy are now joint parents
of their children.

This assumes that no other code will gain references to internal BList
nodes.  The internal nodes are never exposed to the user, so this is a
safe assumption.  In the worst case, if the user manages to gain a
reference to an internal BList node (such as through the gc module),
it will just prevent the BList code from modifying that node.  It will
create a copy instead.  User-visible nodes (i.e., the root of a tree)
have no parents and are never shared children.

Why is this copy-on-write operation so useful?

Consider the common idiom of performing an operation on a slice of a
list.  Normally, this requires making a copy of that region of the
list, which is expensive if the region is large.  With copy-on-write,
__getslice__ takes logarithmic time and logarithmic memory.

As a fun but slightly less practical example, ever wanted to make
REALLY big lists?  Copy-on-write also allows for a logarithmic time
and logarithmic memory implementation of __mul__.

>>> little_list = BList([0])
>>> big_list = little_list * 2**512           <-- 220 milliseconds
>>> print big_list.__len__()
13407807929942597099574024998205846127479365820592393377723561443721764030073546976801874298166903427690031858186486050853753882811946569946433649006084096

(iterating over big_list is not recommended)

Comparison of cost of operations with list()
---------------------------------------------

n is the size of "self", k is the size of the argument.  For slice
operations, k is the length of the slice.  For __mul__, k is the value
of the argument.

   Operation            list               BList
---------------     ------------  -----------------------
init from seq       O(k)          O(k)
copy                O(k)          O(1)
append              O(1)          O(log n)
insert              O(n)          O(log n)
__mul__             O(n*k)        O(log k)
__delitem__         O(n)          O(log n)
__len__             O(1)          O(1)
iteration           O(n)          O(n)
__getslice__        O(k)          O(log n)
__delslice__        O(n)          O(log n + k)
__setslice__        O(n+k)        O(log n + log k)        [1]
extend              O(k)          O(log n + log k)        [1]
__sort__            O(n*log n)    O(n*log n)              [2]
index               O(k)          O(log n + k)
remove              O(n)          O(n)
count               O(n)          O(n)
extended slicing    O(k)          O(k*log n)
__cmp__             O(min(n,k))   O(min(n,k))

[1]: Plus O(k) if the sequence being added is not also a BList
[2]: list.__sort__ requires O(n) worst-case extra memory, while BList.__sort
     requires only (log n) extra memory

For BLists smaller than "limit" elements, each operation essentially
reduces to the equivalent list operation, so there is little-to-no
overhead for the common case of small lists.


Implementation Details
======================

Structure
---------

Each node has four member variables:

leaf:     true if this node is a leaf node (has user data as children),
          false if this node is an interior node (has other nodes as children)

children: an array of references to the node's children

n:        the total number of user data elements below the node.
          equal to len(children) for leaf nodes

refcount: None for a root node,
          otherwise, the number of other nodes with references to this node
                     (i.e., parents)
          
Global Constants
----------------

limit:    the maximum size of .children, must be even and >= 8
half:     limit//2, the minimum size of .children for a valid node,
          other than the root

Definitions
-----------

- The only user-visible node is the root node.
- All leaf nodes are at the same height in the tree.
- If the root node has exactly one child, the root node must be a leaf node.
- Nodes never maintain references to their parents or siblings, only to
  their children.
- Users call methods of the user-node, which may call methods of its
  children, who may call their children recursively.
- A node's user-visible elements are numbered from 0 to self.n-1.  These are
  called "positions".  
- A node's children are numbered 0 to len(self.children)-1.  These are
  called "indexes" and should not be confused with positions.

- Completely private functions (called via self.) may temporarily
  violate these invariants.
- Functions exposed to the user must ensure these invariants are true
  when they return.
- Some functions are visible to the parent of a child node.  When
  these functions return, the invariants must be true as if the child
  were a root node.

Conventions
-----------

- Function that may be called by either users or the object's parent
  either do not begin with underscores, or they begin and end with __.
- A function that may only be called by the object itself begins with
  __ and do not end with underscores.
- Functions that may be called by an object's parent, but not by the user,
  begin with a single underscore.

Other rules
-----------

- If a function may cause a BList to overflow, the function has the
  following return types:
  - None, if the BList did not overflow
  - Otherwise, a valid BList subtree containing a new right-hand sibling
    for the BList that was called.
- BList objects may modify their children if the child's .refcount is
  1.  If the .refcount is greater than 1, the child is shared by another
  parent. The object must copy the child, decrement the child's reference
  counter, and modify the copy instead.
- If an interior node has only one child, before returning it must
  collapse the tree so it takes on the properties of its child.  This
  may mean the interior node becomes a leaf.
- An interior node may return with no children.  The parent must then
  remove the interior node from the parent's children.
- If a function may cause an interior node to collapse, it must have
  the following return types:
  - 0, if the BList did not collapse, or if it is now empty (self.n == 0)
  - A positive integer indicating how many layers have collapsed (i.e., how
    much shorter the subtree is compared to before the function call).
- If a user-visible function does not modify the BList, the BList's
  internal structure must not change.  This is important for
  supporting iterators.

Observations
------------

- User-nodes always have a refcount of at least 1
- User-callable methods may not cause the reference counter to decrement.
- If a parent calls a child's method that may cause the child to
  underflow, the parent must detect the underflow and merge the child
  before returning.

Pieces not implemented here that will be needed in a C version
--------------------------------------------------------------

- __deepcopy__
- support for pickling
- container-type support for the garbage collector

Suspected Bugs:
 - None currently, but needs more testing
 - Passes test_list.py :-)

User-visible Differences from list():
 - If you modify the list in the middle of an iteration and continue
   to iterate, the behavior is different.  BList iteration could be
   implemented the same way as in list, but then iteration would have
   O(n * log n) cost instead of O(n).  I'm okay with the way it is.

Miscellaneous:
 - All of the reference counter stuff is redundant with the reference
   counting done internally on Python objects.  In C we can just peak
   at the reference counter stored in all Python objects.

"""

import copy, types
from itertools import *

########################################################################
# Global constants

limit = 8               # Maximum size, currently low (for testing purposes)
half = limit//2         # Minimum size
assert limit % 2 == 0   # Must be divisible by 2
assert limit >= 8       # The code assumes each block is at least this big

PARANOIA = 2            # Checks reference counters
DEBUG    = 1            # Checks correctness
NO_DEBUG = 0            # No checking

debugging_level = NO_DEBUG

leaked_reference = False

########################################################################
# Simulate utility functions from the Python C API.  These functions
# help us detect the case where we have a self-referential list and a
# user has asked us to print it...
Py_Repr = []
def Py_ReprEnter(obj):
    if obj in Py_Repr: return 1
    Py_Repr.append(obj)
    return 0

def Py_ReprLeave(obj):
    for i in range(len(Py_Repr)-1,-1,-1):
        if Py_Repr[i] == obj:
            del Py_Repr[i]
            break

# Needed for sort
builtin_cmp = cmp

########################################################################
# Decorators are for error-checking and code clarity.  They verify
# (most of) the invariances given above.  They're replaced with no_op()
# if debugging_level == NO_DEBUG.

def modifies_self(f):
    "Decorator for member functions which require write access to self"
    def g(self, *args, **kw):
        assert self.refcount == 1 or self.refcount is None
        rv = f(self, *args, **kw)
        assert self.refcount == 1 or not self.refcount, self.refcount
        return rv
    return g

def parent_callable(f):
    "Indicates the member function may be called by the BList's parent"
    def g(self, *args, **kw):
        #self._check_invariants()
        rv = f(self, *args, **kw)
        self._check_invariants()
        return rv
    return g

def user_callable(f):
    "Indicates a user callable function"
    def g(self, *args, **kw):
        assert self.refcount >= 1 or self.refcount is None
        refs = self.refcount
        self._check_invariants()
        rv = f(self, *args, **kw)
        assert self.refcount == refs
        self._check_invariants()
        return rv
    return g

def may_overflow(f):
    "Indicates the member function may cause an overflow"
    def g(self, *args, **kw):
        rv = f(self, *args, **kw)
        if rv is not None:
            assert isinstance(rv, BList)
            rv._check_invariants()
        self._check_invariants()
        return rv
    return g

def may_collapse(f):
    "Indicates the member function may collapse the subtree"
    def g(self, *args, **kw):
        #height1 = self._get_height()   ## Not reliable just before collapse
        rv = f(self, *args, **kw)
        #height2 = self._get_height()
        assert isinstance(rv, int) and rv >= 0
        self._check_invariants()
        return rv
    return g

def no_op(f):
    return f

if debugging_level == 0:
    modifies_self = no_op
    parent_callable = no_op
    user_callable = no_op
    may_overflow = no_op
    may_collapse = no_op

########################################################################
# Utility functions and decorators for fixing up index parameters.

def sanify_index(n, i):
    if isinstance(i, slice): return i
    if i < 0:
        i += n
    return i

def strong_sanify_index(n, i):
    if isinstance(i, slice): return i
    if i < 0:
        i += n
        if i < 0:
            i = 0
    elif i > n:
        i = n
    return i

def allow_negative1(f):
    "Decarator for allowing a negative position as the first argument"
    def g(self, i, *args, **kw):
        i = sanify_index(self.n, i)
        return f(self, i, *args, **kw)
    return g

def allow_negative2(f):
    "Decarator for allowing a negative position as the 1st and 2nd args"
    def g(self, i, j, *args, **kw):
        i = sanify_index(self.n, i)
        j = sanify_index(self.n, j)
        return f(self, i, j, *args, **kw)
    return g

########################################################################
# An extra constructor and the main class

def _BList(other=[]):
    "Create a new BList for internal use"

    self = BList(other)
    self.refcount = 1
    return self

class BList(object):
    __slots__ = ('leaf', 'children', 'n', 'refcount')

    def _check_invariants(self):
        if debugging_level == NO_DEBUG: return
        try:
            if debugging_level == PARANOIA:
                self.__check_reference_count()
            if self.leaf:
                assert self.n == len(self.children)
            else:
                assert self.n == sum(child.n for child in self.children)
                assert len(self.children) > 1 or len(self.children) == 0, len(self.children)
                for child in self.children:
                    assert isinstance(child, BList)
                    assert half <= len(child.children) <= limit
            assert self.refcount >= 1 or self.refcount is None \
                   or (self.refcount == 0 and not self.children)
        except:
            print self.debug()
            raise

    def _check_invariants_r(self):
        if debugging_level == NO_DEBUG: return
        self._check_invariants()
        if self.leaf: return
        for c in self.children:
            c._check_invariants_r()

    def __init__(self, seq=[]):
        self.leaf = True

        # Points to children
        self.children = []

        # Number of leaf elements that are descendents of this node
        self.n = 0

        # User visible objects have a refcount of None
        self.refcount = None

        # We can copy other BLists in O(1) time :-)
        if isinstance(seq, BList):
            self.__become(seq)
            self._check_invariants()
            return

        self.__init_from_seq(seq)

    ####################################################################
    # Useful internal utility functions

    @modifies_self
    def __become(self, other):
        "Turns self into a clone of other"

        if id(self) == id(other):
            self._adjust_n()
            return
        if not other.leaf:
            for child in other.children:
                child._incref()
        if other.refcount is not None:
            other._incref()  # Other may be one of our children
        self.__forget_children()
        self.n = other.n
        self.children[:] = other.children
        self.leaf = other.leaf
        if other.refcount is not None:
            other._decref()

    @parent_callable
    @modifies_self
    def _adjust_n(self):
        "Recompute self.n"
        if self.leaf:
            self.n = len(self.children)
        else:
            self.n = sum(x.n for x in self.children)

    @parent_callable
    def _locate(self, i):
        """We are searching for the child that contains leaf element i.

        Returns a 3-tuple: (the child object, our index of the child,
                            the number of leaf elements before the child)
        """
        if self.leaf:
            return self.children[i], i, i

        so_far = 0
        for k in range(len(self.children)):
            p = self.children[k]
            if i < so_far + p.n:
                return p, k, so_far
            so_far += p.n
        else:
            return self.children[-1], len(self.children)-1, so_far - p.n

    def __get_reference_count(self):
        "Figure out how many parents we have"
        import gc

        # Count the number of times we are pointed to by a .children
        # list of a BList

        gc.collect()
        objs = gc.get_referrers(self)
        total = 0
        for obj in objs:
            if isinstance(obj, list):
                # Could be a .children
                objs2 = gc.get_referrers(obj)
                for obj2 in objs2:
                    # Could be a BList
                    if isinstance(obj2, BList):
                        total += len([x for x in obj2.children if x is self])
        return total

    def __check_reference_count(self):
        "Validate that we're counting references properly"
        total = self.__get_reference_count()

        if self.refcount is not None:
            # The caller may be about to increment the reference counter, so
            # total == self.refcount or total+1 == self.refcount are OK
            assert total == self.refcount or total+1 == self.refcount,\
                   (total, self.refcount)

        # Reset the flag to avoid repeatedly raising the assertion
        global leaked_reference
        x = leaked_reference
        leaked_reference = False
        assert not x, x

    def _decref(self):
        assert self.refcount is not None
        assert self.refcount > 0
        if self.refcount == 1:
            # We're going to be garbage collected.  Remove all references
            # to other objects.
            self.__forget_children()
        self.refcount -= 1

    def _incref(self):
        assert self.refcount is not None
        self.refcount += 1

    @parent_callable
    def _get_height(self):
        """Find the current height of the tree.

        We could keep an extra few bytes in each node rather than
        figuring this out dynamically, which would reduce the
        asymptotic complexitiy of a few operations.  However, I
        suspect it's not worth the extra overhead of updating it all
        over the place.
        """

        if self.leaf:
            return 1
        return 1 + self.children[-1]._get_height()

    @modifies_self
    def __forget_children(self, i=0, j=None):
        "Remove links to some of our children, decrementing their refcounts"
        if j is None: j = len(self.children)
        if not self.leaf:
            for k in range(i, j):
                self.children[k]._decref()
        del self.children[i:j]

    def __del__(self):
        """In C, this would be a tp_clear function instead of a __del__.

        Because of the way Python's garbage collector handles __del__
        methods, we can end up with uncollectable BList objects if the
        user creates circular references.  In C with a tp_clear
        function, this wouldn't be a problem.
        """
        if self.refcount:
            global leaked_reference
            leaked_reference = True
        try:
            self.refcount = 1          # Make invariance-checker happy
            self.__forget_children()
            self.refcount = 0
        except:
            import traceback
            traceback.print_exc()
            raise

    @modifies_self
    def __forget_child(self, i):
        "Removes links to one child"
        self.__forget_children(i, i+1)

    @modifies_self
    def __prepare_write(self, pt):
        """We are about to modify the child at index pt.  Prepare it.

        This function returns the child object.  If the caller has
        other references to the child, they must be discarded as they
        may no longer be valid.

        If the child's .refcount is 1, we simply return the
        child object.

        If the child's .refcount is greater than 1, we:

        - copy the child object
        - decrement the child's .refcount
        - replace self.children[pt] with the copy
        - return the copy
        """

        if pt < 0:
            pt = len(self.children) + pt
        if not self.leaf and self.children[pt].refcount > 1:
            new_copy = _BList()
            new_copy.__become(self.children[pt])
            self.children[pt]._decref()
            self.children[pt] = new_copy
        return self.children[pt]

    @staticmethod
    def __new_sibling(children, leaf):
        """Non-default constructor.  Create a node with specific children.

        We steal the reference counters from the caller.
        """

        self = _BList()
        self.children = children
        self.leaf = leaf
        self._adjust_n()
        return self

    ####################################################################
    # Functions for manipulating the tree

    @modifies_self
    def __borrow_right(self, k):
        "Child k has underflowed.  Borrow from k+1"
        p = self.children[k]
        right = self.__prepare_write(k+1)
        total = len(p.children) + len(right.children)
        split = total//2

        assert split >= half
        assert total-split >= half

        migrate = split - len(p.children)

        p.children.extend(right.children[:migrate])
        del right.children[:migrate]
        right._adjust_n()
        p._adjust_n()

    @modifies_self
    def __borrow_left(self, k):
        "Child k has underflowed.  Borrow from k-1"
        p = self.children[k]
        left = self.__prepare_write(k-1)
        total = len(p.children) + len(left.children)
        split = total//2

        assert split >= half
        assert total-split >= half

        migrate = split - len(p.children)

        p.children[:0] = left.children[-migrate:]
        del left.children[-migrate:]
        left._adjust_n()
        p._adjust_n()

    @modifies_self
    def __merge_right(self, k):
        "Child k has underflowed.  Merge with k+1"
        p = self.children[k]
        for p2 in self.children[k+1].children:
            if not self.children[k+1].leaf:
                p2._incref()
            p.children.append(p2)
        self.__forget_child(k+1)
        p._adjust_n()

    @modifies_self
    def __merge_left(self, k):
        "Child k has underflowed.  Merge with k-1"
        p = self.children[k]
        if not self.children[k-1].leaf:
            for p2 in self.children[k-1].children:
                p2._incref()
        p.children[:0] = self.children[k-1].children
        self.__forget_child(k-1)
        p._adjust_n()

    @staticmethod
    def __concat(left_subtree, right_subtree, height_diff):
        """Concatenate two trees of potentially different heights.

        The parameters are the two trees, and the difference in their
        heights expressed as left_height - right_height.

        Returns a tuple of the new, combined tree, and an integer.
        The integer expresses the height difference between the new
        tree and the taller of the left and right subtrees.  It will
        be 0 if there was no change, and 1 if the new tree is taller
        by 1.
        """

        assert left_subtree.refcount == 1
        assert right_subtree.refcount == 1

        adj = 0

        if height_diff == 0:
            root = _BList()
            root.children = [left_subtree, right_subtree]
            root.leaf = False
            collapse = root.__underflow(0)
            if not collapse:
                collapse = root.__underflow(1)
            if not collapse:
                adj = 1
            overflow = None
        elif height_diff > 0: # Left is larger
            root = left_subtree
            overflow = root._insert_subtree(-1, right_subtree,
                                            height_diff - 1)
        else: # Right is larger
            root = right_subtree
            overflow = root._insert_subtree(0, left_subtree,
                                            -height_diff - 1)
        adj += -root.__overflow_root(overflow)

        return root, adj

    @staticmethod
    def __concat_subtrees(left_subtree, left_depth, right_subtree,right_depth):
        """Concatenate two subtrees of potentially different heights.

        Returns a tuple of the new, combined subtree and its depth.

        Depths are the depth in the parent, not their height.
        """

        root, adj = BList.__concat(left_subtree, right_subtree,
                                   -(left_depth - right_depth))
        return root, max(left_depth, right_depth) - adj

    @staticmethod
    def __concat_roots(left_root, left_height, right_root, right_height):
        """Concatenate two roots of potentially different heights.

        Returns a tuple of the new, combined root and its height.

        Heights are the height from the root to its leaf nodes.
        """

        root, adj = BList.__concat(left_root, right_root,
                                   left_height - right_height)
        return root, max(left_height, right_height) + adj

    @may_collapse
    @modifies_self
    def __collapse(self):
        "Collapse the tree, if possible"
        if len(self.children) != 1 or self.leaf:
            self._adjust_n()
            return 0

        p = self.children[0]
        self.__become(p)
        return 1

    @may_collapse
    @modifies_self
    def __underflow(self, k):
        """Check if children k-1, k, or k+1 have underflowed.

        If so, move things around until self is the root of a valid
        subtree again, possibly requiring collapsing the tree.

        Always calls self._adjust_n() (often via self.__collapse()).
        """

        if self.leaf:
            self._adjust_n()
            return 0

        if k < len(self.children):
            p = self.__prepare_write(k)
            short = half - len(p.children)

            while short > 0:
                if k+1 < len(self.children) \
                   and len(self.children[k+1].children) - short >= half:
                    self.__borrow_right(k)
                elif k > 0 and len(self.children[k-1].children) - short >=half:
                    self.__borrow_left(k)
                elif k+1 < len(self.children):
                    self.__merge_right(k)
                elif k > 0:
                    self.__merge_left(k)
                    k = k - 1
                else:
                    # No siblings for p
                    return self.__collapse()

                p = self.__prepare_write(k)
                short = half - len(p.children)

        if k > 0 and len(self.children[k-1].children) < half:
            collapse = self.__underflow(k-1)
            if collapse: return collapse
        if k+1 < len(self.children) \
               and len(self.children[k+1].children) <half:
            collapse = self.__underflow(k+1)
            if collapse: return collapse

        return self.__collapse()

    @modifies_self
    def __overflow_root(self, overflow):
        "Handle the case where a user-visible node overflowed"
        self._check_invariants()
        if not overflow: return 0
        child = _BList(self)
        self.__forget_children()
        self.children[:] = [child, overflow]
        self.leaf = False
        self._adjust_n()
        self._check_invariants()
        return -1

    @may_overflow
    @modifies_self
    def __insert_here(self, k, item):
        """Insert 'item', which may be a subtree, at index k.

        Since the subtree may have fewer than half elements, we may
        need to merge it after insertion.

        This function may cause self to overflow.  If it does, it will
        take the upper half of its children and put them in a new
        subtree and return the subtree.  The caller is responsible for
        inserting this new subtree just to the right of self.

        Otherwise, it returns None.

        """

        if k < 0:
            k += len(self.children)

        if len(self.children) < limit:
            self.children.insert(k, item)
            collapse = self.__underflow(k)
            assert not collapse
            self._adjust_n()
            return None

        sibling = BList.__new_sibling(self.children[half:], self.leaf)
        del self.children[half:]

        if k < half:
            self.children.insert(k, item)
            collapse = self.__underflow(k)
            assert not collapse
        else:
            sibling.children.insert(k - half, item)
            collapse = sibling.__underflow(k-half)
            assert not collapse
            sibling._adjust_n()
        self._adjust_n()
        return sibling

    @may_overflow
    @modifies_self
    def _insert_subtree(self, side, subtree, depth):
        """Recurse depth layers, then insert subtree on the left or right

        This function may cause an overflow.

        depth == 0 means insert the subtree as a child of self.
        depth == 1 means insert the subtree as a grandchild, etc.

        """
        assert side == 0 or side == -1

        self._check_invariants()
        subtree._check_invariants()

        self.n += subtree.n

        if depth:
            p = self.__prepare_write(side)
            overflow = p._insert_subtree(side, subtree, depth-1)
            if not overflow: return None
            subtree = overflow

        if side < 0:
            side = len(self.children)

        sibling = self.__insert_here(side, subtree)

        return sibling

    @modifies_self
    def __reinsert_subtree(self, k, depth):
        'Child at position k is too short by "depth".  Fix it'

        assert self.children[k].refcount == 1, self.children[k].refcount
        subtree = self.children.pop(k)
        if len(self.children) > k:
            # Merge right
            p = self.__prepare_write(k)
            overflow = p._insert_subtree(0, subtree, depth-1)
            if overflow:
                self.children.insert(k+1, overflow)
        else:
            # Merge left
            p = self.__prepare_write(k-1)
            overflow = p._insert_subtree(-1, subtree, depth-1)
            if overflow:
                self.children.insert(k, overflow)
        return self.__underflow(k)

    ####################################################################
    # The main insert and deletion operations

    @may_overflow
    @modifies_self
    def _insert(self, i, item):
        """Recursive to find position i, and insert item just there.

        This function may cause an overflow.

        """
        if self.leaf:
            return self.__insert_here(i, item)

        p, k, so_far = self._locate(i)
        del p
        self.n += 1
        p = self.__prepare_write(k)
        overflow = p._insert(i - so_far, item)
        del p
        if not overflow: return
        return self.__insert_here(k+1, overflow)

    @user_callable
    @modifies_self
    def __iadd__(self, other):
        # Make not-user-visible roots for the subtrees
        right = _BList(other)
        left = _BList(self)

        left_height = left._get_height()
        right_height = right._get_height()

        root = BList.__concat_subtrees(left, -left_height,
                                       right, -right_height)[0]
        self.__become(root)
        root._decref()
        return self

    @parent_callable
    @may_collapse
    @modifies_self
    def _delslice(self, i, j):
        """Recursive version of __delslice__

        This may cause self to collapse.  It returns None if it did
        not.  If a collapse occured, it returns a positive integer
        indicating how much shorter this subtree is compared to when
        _delslice() was entered.

        Additionally, this function may cause an underflow.

        """

        if i == 0 and j >= self.n:
            # Delete everything.
            self.__forget_children()
            self.n = 0
            return 0

        if self.leaf:
            del self.children[i:j]
            self.n = len(self.children)
            return 0

        p, k, so_far = self._locate(i)
        p2, k2, so_far2 = self._locate(j-1)
        del p
        del p2

        if k == k2:
            # All of the deleted elements are contained under a single
            # child of this node.  Recurse and check for a short
            # subtree and/or underflow

            assert so_far == so_far2
            p = self.__prepare_write(k)
            depth = p._delslice(i - so_far, j - so_far)
            if not depth:
                return self.__underflow(k)
            return self.__reinsert_subtree(k, depth)

        # Deleted elements are in a range of child elements.  There
        # will be:
        # - a left child (k) where we delete some (or all) of its children
        # - a right child (k2) where we delete some (or all) of it children
        # - children in between who are deleted entirely

        # Call _delslice recursively on the left and right
        p = self.__prepare_write(k)
        collapse_left = p._delslice(i - so_far, j - so_far)
        del p
        p2 = self.__prepare_write(k2)
        collapse_right = p2._delslice(max(0, i - so_far2), j - so_far2)
        del p2

        deleted_k = False
        deleted_k2 = False

        # Delete [k+1:k2]
        self.__forget_children(k+1, k2)
        k2 = k+1

        # Delete k1 and k2 if they are empty
        if not self.children[k2].n:
            self.children[k2]._decref()
            del self.children[k2]
            deleted_k2 = True
        if not self.children[k].n:
            self.children[k]._decref()
            del self.children[k]
            deleted_k = True

        if deleted_k and deleted_k2: # No messy subtrees.  Good.
            return self.__collapse()

        # The left and right may have collapsed and/or be in an
        # underflow state.  Clean them up.  Work on fixing collapsed
        # trees first, then worry about underflows.

        if not deleted_k and not deleted_k2 \
               and collapse_left and collapse_right:
            # Both exist and collapsed.  Merge them into one subtree.
            left = self.children.pop(k)
            right = self.children.pop(k)
            subtree, depth = BList.__concat_subtrees(left, collapse_left,
                                                     right, collapse_right)
            del left
            del right
            self.children.insert(k, subtree)
            
        elif deleted_k:
            # Only the right potentially collapsed, point there.
            depth = collapse_right
            # k already points to the old k2, since k was deleted
        elif not deleted_k2 and not collapse_left:
            # Only the right potentially collapsed, point there.
            k = k + 1
            depth = collapse_right
        else:
            depth = collapse_left

        # At this point, we have a potentially short subtree at k,
        # with depth "depth".

        if not depth or len(self.children) == 1:
            # Doesn't need merging, or no siblings to merge with
            return depth + self.__underflow(k)

        # We definitely have a short subtree at k, and we have other children
        return self.__reinsert_subtree(k, depth)

    @modifies_self
    def __init_from_seq(self, seq):
        # Try the common case of a sequence <= limit in length
        iterator = iter(seq)
        for i in range(limit):
            try:
                x = iterator.next()
            except StopIteration:
                self.n = len(self.children)
                self._check_invariants()
                return
            except AttributeError:
                raise TypeError('instance has no next() method')
            self.children.append(x)
        self.n = limit
        assert limit == len(self.children)
        self._check_invariants()

        # No such luck, build bottom-up instead.
        # The sequence data so far goes in a leaf node.
        cur = _BList()
        self._check_invariants()
        cur._check_invariants()
        cur.__become(self)
        cur._check_invariants()
        self.__forget_children()
        cur._check_invariants()

        forest = Forest()
        forest.append_leaf(cur)
        cur = _BList()

        while 1:
            try:
                x = iterator.next()
            except StopIteration:
                break
            if len(cur.children) == limit:
                cur.n = limit
                cur._check_invariants()
                forest.append_leaf(cur)
                cur = _BList()
            cur.children.append(x)

        if cur.children:
            forest.append_leaf(cur)
            cur.n = len(cur.children)
        else:
            cur._decref()

        final = forest.finish()
        self.__become(final)
        final._decref()

    ########################################################################
    # Below here are other user-callable functions built using the above
    # primitives and user functions.

    @parent_callable
    def _str(self, f):
        """Recursive version of __str__

        Not technically user-callable, but nice to keep near the other
        string functions.
        """

        if self.leaf:
            return ', '.join(f(x) for x in self.children)
        else:
            return ', '.join(x._str(f) for x in self.children)

    @user_callable
    def __str__(self):
        "User-visible function"
        if Py_ReprEnter(self):
            return '[...]'
        #rv = 'BList(%s)' % self._str()
        rv = '[%s]' % self._str(str)
        Py_ReprLeave(self)
        return rv

    @user_callable
    def __repr__(self):
        "User-visible function"
        if Py_ReprEnter(self):
            return '[...]'
        #rv = 'BList(%s)' % self._str()
        rv = '[%s]' % self._str(repr)
        Py_ReprLeave(self)
        return rv

    def debug(self, indent=''):
        import gc
        gc.collect()
        "Return a string that shows the internal structure of the BList"
        indent = indent + ' '
        if not self.leaf:
            rv = 'blist(leaf=%s, n=%s, r=%s, %s)' % (
                str(self.leaf), str(self.n), str(self.refcount),
                '\n%s' % indent +
                ('\n%s' % indent).join([x.debug(indent+'  ')
                                        for x in self.children]))
        else:
            rv = 'blist(leaf=%s, n=%s, r=%s, %s)' % (
                str(self.leaf), str(self.n), str(self.refcount),
                str(self.children))
        return rv

    @user_callable
    @allow_negative1
    def __getitem__(self, i):
        "User-visible function"
        if isinstance(i, slice):
            start, stop, step = i.indices(self.n)
            return BList(self[j] for j in xrange(start, stop, step))

        if type(i) != types.IntType and type(i) != types.LongType:
            raise TypeError('list indices must be integers')

        if i >= self.n or i < 0:
            raise IndexError

        if self.leaf:
            return self.children[i]

        p, k, so_far = self._locate(i)
        assert i >= so_far
        return p.__getitem__(i - so_far)

    @user_callable
    @modifies_self
    @allow_negative1
    def __setitem__(self, i, y):
        "User-visible function"

        if isinstance(i, slice):
            start, stop, step = i.indices(self.n)
            if step == 1:
                # More efficient
                self[start:stop] = y
                return
            y = _BList(y)
            raw_length = (stop - start)
            length = raw_length//step
            if raw_length % step:
                length += 1
            if length != len(y):
                leny = len(y)
                y._decref()
                raise ValueError('attempt to assign sequence of size %d '
                                 'to extended slice of size %d'
                                 % (leny, length))
            k = 0
            for j in xrange(start, stop, step):
                self[j] = y[k]
                k += 1
            y._decref()
            return

        if i >= self.n or i < 0:
            raise IndexError

        if self.leaf:
            self.children[i] = y
            return

        p, k, so_far = self._locate(i)
        p = self.__prepare_write(k)
        p.__setitem__(i-so_far, y)

    @user_callable
    def __len__(self):
        "User-visible function"
        return self.n

    @user_callable
    def __iter__(self):
        "User-visible function"
        return self._iter(0, None)

    def _iter(self, i, j):
        "Make an efficient iterator between elements i and j"
        if self.leaf:
            return ShortBListIterator(self, i, j)
        return BListIterator(self, i, j)

    @user_callable
    def __cmp__(self, other):
        if not isinstance(other, BList) and not isinstance(other, list):
            return cmp(id(type(self)), id(type(other)))

        iter1 = iter(self)
        iter2 = iter(other)
        x_failed = False
        y_failed = False
        while 1:
            try:
                x = iter1.next()
            except StopIteration:
                x_failed = True
            try:
                y = iter2.next()
            except StopIteration:
                y_failed = True
            if x_failed or y_failed: break

            c = cmp(x, y)
            if c: return c

        if x_failed and y_failed: return 0
        if x_failed: return -1
        return 1

    @user_callable
    def __contains__(self, item):
        for x in self:
            if x == item: return True
        return False

    @user_callable
    @modifies_self
    def __setslice__(self, i, j, other):
        # Python automatically adds len(self) to these values if they
        # are negative.  They'll get incremented a second time below
        # when we use them as slice arguments.  Subtract len(self)
        # from them to keep them at the same net value.
        #
        # If they went positive the first time, that's OK.  Python
        # won't change them any further.

        if i < 0:
            i -= self.n
        if j < 0:
            j -= self.n

        # Make a not-user-visible root for the other subtree
        other = _BList(other)

        # Efficiently handle the common case of small lists
        if self.leaf and other.leaf and self.n + other.n <= limit:
            self.children[i:j] = other.children
            other._decref()
            self._adjust_n()
            return

        left = self
        right = _BList(self)
        del left[i:]
        del right[:j]
        left += other
        left += right

        other._decref()
        right._decref()

    @user_callable
    @modifies_self
    def extend(self, other):
        return self.__iadd__(other)

    @user_callable
    @modifies_self
    def pop(self, i=-1):
        try:
            i = int(i)
        except ValueError:
            raise TypeError('an integer is required')
        rv = self[i]
        del self[i]
        return rv

    @user_callable
    def index(self, item, i=0, j=None):
        i, j, _ = slice(i, j).indices(self.n)
        for k, x in enumerate(self._iter(i, j)):
            if x == item:
                return k + i
        raise ValueError('list.index(x): x not in list')

    @user_callable
    @modifies_self
    def remove(self, item):
        for i, x in enumerate(self):
            if x == item:
                del self[i]
                return
        raise ValueError('list.index(x): x not in list')

    @user_callable
    def count(self, item):
        rv = 0
        for x in self:
            if x == item:
                rv += 1
        return rv

    @user_callable
    @modifies_self
    def reverse(self):
        self.children.reverse()
        if self.leaf: return
        for i in range(len(self.children)):
            p = self.__prepare_write(i)
            p.reverse()

    @user_callable
    def __mul__(self, n):
        if n <= 0:
            return BList()

        power = BList(self)
        rv = BList()

        if n & 1:
            rv += self
        mask = 2

        while mask <= n:
            power += power
            if mask & n:
                rv += power
            mask <<= 1
        return rv

    __rmul__ = __mul__

    @user_callable
    @modifies_self
    def __imul__(self, n):
        self.__become(self * n)
        return self

    @parent_callable
    @modifies_self
    def _merge(self, other, cmp=None, key=None, reverse=False):
        """Merge two sorted BLists into one sorted BList, part of MergeSort

        This function consumes the two input BLists along the way,
        making the MergeSort nearly in-place.  This function gains ownership
        of the self and other objects and must .decref() them if appropriate.

        It returns one sorted BList.

        It operates by maintaining two forests (lists of BList
        objects), one for each of the two inputs lists.  When it needs
        a new leaf node, it looks at the first element of the forest
        and checks to see if it's a leaf.  If so, it grabs that.  If
        not a leaf, it takes that node, removes the root, and prepends
        the children to the forest.  Then, it checks again for a leaf.
        It repeats this process until it is able to acquire a leaf.
        This process avoids the cost of doing O(log n) work O(n) times
        (for a total O(n log n) cost per merge).  It takes O(log n)
        extra memory and O(n) steps.

        We also maintain a forest for the output.  Whenever we fill an
        output leaf node, we append it to the output forest.  We keep
        track of the total number of leaf nodes added to the forest,
        and use that to analytically determine if we have "limit" nodes at the
        end of the forest all of the same height.  When we do, we remove them
        from the forest, place them under a new node, and put the new node on
        the end of the forest.  This guarantees that the output forest
        takes only O(log n) extra memory.  When we're done with the input, we
        merge the forest into one final BList.

        Whenever we finish with an input leaf node, we add it to a
        recyclable list, which we use as a source for nodes for the
        output.  Since the output will use only O(1) more nodes than the
        combined input, this part is effectively in-place.

        Overall, this function uses O(log n) extra memory and takes O(n) time.
        """
        
        other._check_invariants();
        if not cmp:
            cmp = builtin_cmp
    
        recyclable = []
    
        def do_cmp(a, b):
            "Utility function for performing a comparison"
            
            if key:
                a = a[key]
                b = b[key]
            x = cmp(a, b)
            if reverse:
                x = -x
            return x
    
        def recycle(node):
            "We've consumed a node, set it aside for re-use"
            del node.children[:]
            node.n = 0
            node.leaf = True
            recyclable.append(node)
            assert node.refcount == 1
            assert node.__get_reference_count() == 0
    
        def get_node(leaf):
            "Get a node, either from the recycled list or through allocation"
            if recyclable:
                node = recyclable.pop(-1)
            else:
                node = _BList()
            node.leaf = leaf
            return node
    
        def get_leaf(forest):
            "Get a new leaf node to process from one of the input forests"
            node = forest.pop(-1)
            assert not node.__get_reference_count()
            while not node.leaf:
                forest.extend(reversed(node.children))
                recycle(node)
                node = forest.pop(-1)
            assert node.__get_reference_count() == 0
            return node

        try:
            if do_cmp(self[-1], other[0]) <= 0: # Speed up a common case
                self += other
                other._decref()
                return self

            # Input forests
            forest1 = [self]
            forest2 = [other]

            # Output forests
            forest_out = Forest()

            # Input leaf nodes we are currently processing
            leaf1 = get_leaf(forest1)
            leaf2 = get_leaf(forest2)

            # Index into leaf1 and leaf2, respectively
            i = 0 
            j = 0

            # Current output leaf node we are building
            output = get_node(leaf=True)
                
            while ((forest1 or i < len(leaf1.children))
                    and (forest2 or j < len(leaf2.children))):

                # Check if we need to get a new input leaf node
                if i == len(leaf1.children):
                    recycle(leaf1)
                    leaf1 = get_leaf(forest1)
                    i = 0
                if j == len(leaf2.children):
                    recycle(leaf2)
                    leaf2 = get_leaf(forest2)
                    j = 0

                # Check if we have filled up an output leaf node
                if output.n == limit:
                    forest_out.append_leaf(output)
                    output = get_node(leaf=True)

                # Figure out which input leaf has the lower element
                if do_cmp(leaf1.children[i], leaf2.children[j]) <= 0:
                    output.children.append(leaf1.children[i])
                    i += 1
                else:
                    output.children.append(leaf2.children[j])
                    j += 1

                output.n += 1

            # At this point, we have completely consumed at least one
            # of the lists

            # Append our partially-complete output leaf node to the forest
            forest_out.append_leaf(output)

            # Append a partially-consumed input leaf node, if one exists
            if i < len(leaf1.children):
                del leaf1.children[:i]
                forest_out.append_leaf(leaf1)
            else:
                recycle(leaf1)
            if j < len(leaf2.children):
                del leaf2.children[:j]
                forest_out.append_leaf(leaf2)
            else:
                recycle(leaf2)
    
            # Append the rest of whichever input forest still has
            # nodes.  This could be sped up by merging trees instead
            # of doing it leaf-by-leaf.
            while forest1:
                forest_out.append_leaf(get_leaf(forest1))
            while forest2:
                forest_out.append_leaf(get_leaf(forest2))

            out_tree = forest_out.finish()
    
        finally:
            # Fix reference counters, in case the user-compare function
            # threw an exception.
            for c in recyclable:
                c._decref()

        return out_tree

    @parent_callable
    @modifies_self
    def _sort(self, *args, **kw):
        if self.leaf:
            self.children.sort(*args, **kw)
            return
        for i in range(len(self.children)):
            self.__prepare_write(i)
            self.children[i]._sort(*args, **kw)
        while len(self.children) != 1:
            children = []
            for i in range(0, len(self.children)-1, 2):
                #print 'Merge:', self.children[i], self.children[i+1]
                a = self.children[i]
                b = self.children[i+1]
                self.children[i] = None     # Keep reference-checker happy
                self.children[i+1] = None
                self.children[i] = a._merge(b, *args, **kw)
                #print '->', self.children[i].debug()
                #assert list(self.children[i]) == sorted(self.children[i], *args, **kw)
                #self.children[i+1]._decref()
                children.append(self.children[i])
            self.children[:] = children
        self.__become(self.children[0])
        self._check_invariants_r()

    @user_callable
    @modifies_self
    def sort(self, *args, **kw):
        if self.leaf: # Special case to speed up common case
            self.children.sort(*args, **kw)
            return
        no_list = BList()
        real_self = BList(self)
        self.__become(no_list)
        try:
            real_self._sort(*args, **kw)
            self._check_invariants_r()
            if self.n:
                raise ValueError('list modified during sort')
        finally:
            self._check_invariants_r()
            real_self._check_invariants_r()
            self.__become(real_self)
            self._check_invariants_r()
                    
    @user_callable
    def __add__(self, other):
        if not isinstance(other, BList) and not isinstance(other, list):
            raise TypeError('can only concatenate list (not "%s") to list'
                            % str(type(other)))
        rv = BList(self)
        rv += other
        return rv

    @user_callable
    def __radd__(self, other):
        if not isinstance(other, BList) and not isinstance(other, list):
            raise TypeError('can only concatenate list (not "%s") to list'
                            % str(type(other)))
        rv = BList(other)
        rv += self
        return rv

    @user_callable
    @modifies_self
    def append(self, item):
        "User-visible function"
        self.insert(len(self), item)

    @user_callable
    @modifies_self
    @allow_negative1
    def insert(self, i, item):
        "User-visible function"
        if i > self.n:
            i = self.n
        overflow = self._insert(i, item)
        self.__overflow_root(overflow)

    @user_callable
    @modifies_self
    def __delslice__(self, i, j):
        "User-visible function"
        if i >= j:
            return
        self._delslice(i, j)

    @user_callable
    @modifies_self
    @allow_negative1
    def __delitem__(self, i):
        "User-visible function"

        if isinstance(i, slice):
            start, stop, step = i.indices(self.n)
            if step == 1:
                # More efficient
                self.__delslice__(start, stop)
                return
            j = start
            if step > 0:
                step -= 1 # We delete an item at each step
                while j < len(self) and j < stop:
                    del self[j]
                    j += step
            else:
                for j in range(start, stop, step):
                    del self[j]
            return

        if i >= self.n or i < 0:
            raise IndexError

        self.__delslice__(i, i+1)

    @user_callable
    def __getslice__(self, i, j):
        "User-visible function"

        # If the indices were negative, Python has already added len(self) to
        # them.  If they're still negative, treat them as 0.
        if i < 0: i = 0
        if j < 0: j = 0

        if j <= i:
            return BList()

        if i >= self.n:
            return BList()

        if self.leaf:
            return BList(self.children[i:j])

        rv = BList(self)
        del rv[j:]
        del rv[:i]

        return rv

    def __copy__(self):
        return BList(self)

########################################################################
# Forest class; an internal utility class for building BLists bottom-up

class Forest:
    def __init__(self):
        self.num_leafs = 0
        self.forest = []

    def append_leaf(self, leaf):
        "Append a leaf to the output forest, possible combining nodes"
        
        if not leaf.children:     # Don't add empty leaf nodes
            leaf._decref()
            return
        self.forest.append(leaf)
        leaf._adjust_n()
    
        # Every "limit" leaf nodes, combine the last "limit" nodes
        # This takes "limit" leaf nodes and replaces them with one node
        # that has the leaf nodes as children.
        
        # Every "limit**2" leaf nodes, take the last "limit" nodes
        # (which have height 2) and replace them with one node
        # (with height 3).
    
        # Every "limit**i" leaf nodes, take the last "limit" nodes
        # (which have height i) and replace them with one node
        # (with height i+1).
    
        i = 1
        self.num_leafs += 1
        while self.num_leafs % limit**i == 0:
            parent = _BList()
            parent.leaf = False
            assert len(self.forest) >= limit, \
                   (len(self.forest), limit, i, self.num_leafs)
            parent.children[:] = self.forest[-limit:]
            del self.forest[-limit:]
    
            # If the right-hand node has too few children,
            # borrow from a neighbor
            x = parent._BList__underflow(len(parent.children)-1)
            assert not x
    
            self.forest.append(parent)
            i += 1
            parent._check_invariants_r()

    def finish(self):
        "Combine the forest into a final BList"
    
        out_tree = None    # The final BList we are building
        out_height = 0     # It's height
        group_height = 1   # The height of the next group from the forest
        while self.forest:
            n = self.num_leafs % limit  # Numbers of same-height nodes
            self.num_leafs /= limit  
            group_height += 1
    
            if not n:
                # No nodes at this height
                continue

            # Merge nodes of the same height into 1 node, and
            # merge it into our output BList.
            group = _BList()
            group.leaf = False
            group.children[:] = self.forest[-n:]
            del self.forest[-n:]
            adj = group._BList__underflow(len(group.children)-1)
            if not out_tree:
                out_tree = group
                out_height = group_height - adj
            else:
                out_tree, out_height = BList._BList__concat_roots(group,
                                                            group_height - adj,
                                                            out_tree,
                                                            out_height)
        out_tree._check_invariants_r()
        return out_tree


########################################################################
# Iterator classes.  BList._iter() choses which one to use.

class ShortBListIterator:
    "A low-overhead iterator for short lists"

    def __init__(self, lst, start=0, stop=None):
        if stop is None:
            stop = len(lst)
        self.cur = start
        self.stop = stop
        self.lst = lst

    def next(self):
        if self.cur >= self.stop or self.cur >= self.lst.n:
            self.stop = 0  # Ensure the iterator cannot be restarted
            raise StopIteration

        rv = BList.__getitem__(self.lst, self.cur)
        self.cur += 1
        return rv

    def __iter__(self):
        return self

class BListIterator:
    """A high-overhead iterator that is more asymptotically efficient.

    Maintain a stack to traverse the tree.  The first step is to copy
    the list so we don't have to worry about user's modifying the list
    and wreaking havoc with our references.  Copying the list is O(1),
    but not worthwhile for lists that only contain a single leaf node.
    """

    def __init__(self, lst, start=0, stop=None):
        self.stack = []
        lst = BList(lst)  # Protect against users modifying the list
        if stop is None:
            stop = len(lst)
        if stop < 0: stop = 0
        if start < 0: start = 0
        self.remaining = stop - start
        while not lst.leaf:
            p, k, so_far = lst._locate(start)
            self.stack.append([lst, k+1])
            lst = lst.children[0]
            start -= so_far
        self.stack.append([lst, start])

    def next(self):
        if not self.remaining:
            raise StopIteration
        self.remaining -= 1

        p, i = self.stack[-1]
        if i < len(p.children):
            self.stack[-1][1] += 1
            return p.children[i]

        while 1:
            if not self.stack: raise StopIteration
            p, i = self.stack.pop()
            if i < len(p.children):
                break

        self.stack.append([p, i+1])

        while not p.leaf:
            p = p.children[i]
            i = 0
            self.stack.append([p, i+1])

        return p.children[i]

    def __iter__(self):
        return self

    def __copy__(self):
        rv = BListIterator.__new__()
        rv.stack = copy.copy(self.stack)
        rv.remaining = self.remaining
        return rv

########################################################################
# Test code

def main():
    n = 512

    data = range(n)
    import random
    random.shuffle(data)
    x = BList(data)
    x.sort()

    assert list(x) == sorted(data), x

    lst = BList()
    t = tuple(range(n))
    for i in range(n):
        lst.append(i)
        if tuple(lst) != t[:i+1]:
            print i, tuple(lst), t[:i+1]
            print lst.debug()
            break
    
    x = lst[4:258]
    assert tuple(x) == tuple(t[4:258])
    x.append(-1)
    assert tuple(x) == tuple(t[4:258] + (-1,))
    assert tuple(lst) == t
    
    lst[200] = 6
    assert tuple(x) == tuple(t[4:258] + (-1,))
    assert tuple(lst) == tuple(t[0:200] + (6,) + t[201:])
    
    del lst[200]
    #print lst.debug()
    assert tuple(lst) == tuple(t[0:200] + t[201:])
    
    lst2 = BList(range(limit+1))
    assert tuple(lst2) == tuple(range(limit+1))
    del lst2[1]
    del lst2[-1]
    assert tuple(lst2) == (0,) + tuple(range(2,limit))
    assert lst2.leaf
    assert len(lst2.children) == limit-1

    lst = BList(range(n))
    lst.insert(200, 0)
    assert tuple(lst) == (t[0:200] + (0,) + t[200:])
    del lst[200:]
    assert tuple(lst) == tuple(range(200))

    lst = BList(range(3))
    lst*3
    assert lst*3 == range(3)*3

    a = BList('spam')
    a.extend('eggs')
    assert a == list('spameggs')

    x = BList([0])
    for i in range(290) + [1000, 10000, 100000, 1000000, 10000000]:
        if len(x*i) != i:
            print 'mul failure', i
            print (x*i).debug()
            break

    little_list = BList([0])
    big_list = little_list * 2**512

blist = BList
    
if __name__ == '__main__':
    main()
