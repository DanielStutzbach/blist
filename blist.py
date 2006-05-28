#!/usr/bin/python

"""

Definitions:

- Each node has no more than "limit" elements (also called children).
- Every node is either a "leaf" or an "interior node".
- Elements of a leaf node are user-visible Python objects.  These are
  called "leaf elements".
- An interior node's leaf elements are the leaf elements of all its
  descendants.
- Elements of interior nodes are other BList objects that are not visible
  to the user.
- A "user-node" is a BList object that is visible to the user.
- A "subtree" is a BList object and all of its descendents.  The object
  is called the "root" of the subtree.
- BList objects never maintain references to their parents or siblings,
  only to their children.
- Users call methods of the user-node, which may call methods of its
  children, who may call their children recursively.
- A "valid node" is one which has between limit/2 and limit elements.
- A "valid subtree" is a balanced subtree whose descendents are all valid
  nodes.  The root of the subtree must have no more than limit
  elements, but may have as few as 0 elements.
- Each BList object has a variable .refcount which is the number of
  BList objects that refer to the object, plus 1 if the object is a user-node.
- User-nodes may never be referred to by another BList object (ergo
  user-nodes always have a .refcount of 1).
- A public function is a member function that may be called by either users
  or the object's parent.  They either do not begin with underscores, or
  they begin and end with __.
- A internal function is one that may only be called by the object
  itself.  Even other BList objects should not call the function, as it may
  violate the BList invariants.  They begin with __ and do not end with
  underscores.
- A recursive function is a member function that may be called by an object's
  parent.  They begin with a single underscore, and must maintain the BList
  invariants.
- A node's leaf elements are numbered from 0 to self.n-1.  These are
  called"positions".  For interior nodes, this numbering covers the leaf
  elements of all the node's descendents.
- A node's children are numbered 0 to len(self.children)-1.  These are
  called "indexes" and should not be confused with positions.

Invariants:

- When a BList member function returns, the BList is the root of a valid
  subtree.  Exception: member functions that will only be called
  via self do not need to adhere to this requirement.
- When a BList member function returns, the .n member variable must
  contain the total number of leaf elements of all its descendents.
- If a function may cause a BList to overflow, the function has the
  following return types:
  - None, if the BList did not overflow
  - A valid BList subtree containing a new right-hand sibling for the
    BList that was called.
- BList objects may modify their children if the child's .refcount is
  1.  If the .refcount is's greater than 1, the child is shared by another
  parent. The object must copy the child, decrement the child's reference
  counter, and modify the copy instead.
- If an interior node has only one child, before returning it must
  collapse the tree so it takes on the properties of its child.  This
  may mean the interior node becomes a leaf.
- An interior node may return with no children.  The parent must then
  remove the interior node from its children.
- If a function may cause an interior node to collapse, it must have
  the following return types:
  - 0, if the BList did not collapse, or if it is now empty (self.n == 0)
  - A positive integer indicating how many layers have collapsed (i.e., how
    much shorter the subtree is compared to before the function call).
- If a user-visible function does not modify the BList, the BList's
  internal structure must not change.  This is important for
  supporting iterators.

Observations:

- User-nodes always have a refcount of at least 1
- User-callable methods may not cause the reference counter to decrement.
- If a parent calls a child's method that may cause a BList to
  underflow, the parent must detect the underflow and merge the child
  before returning.

Pieces not implemented here that will be needed in a C version:

- __deepcopy__
- support for pickling
- container-type support for the garbage collector

Comparision of cost of operations with list():

n is the size of "self", k is the size of the argument.  For
slice operations, k is the length of the slice.

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
__sort__            O(n*log n)    O(n*log n)
index               O(k)          O(log n + k)
remove              O(n)          O(n)
count               O(n)          O(n)
extended slicing    O(k)          O(k*log n)
__cmp__             O(min(n,k))   O(min(n,k))

[1]: Plus O(k) if the items being added are not in a BList

For BLists smaller than "limit" elements, each operation essentially
reduces to the equivalent list operation, so there is little-to-no
overhead for the common case of small lists.

Suspected Bugs:
 - None currently, but needs more testing
 - Passes test_list.py :-)

User-visible Differences from list:
 - If you modify the list in the middle of an iteration and continue
   to iterate, the behavior is different.  BList iteration could be
   implemented the same way as in list, but then iteration would have
   O(n * log n) cost instead of O(N).  I'm okay with the way it is.

Miscellaneous:
 - All of the reference counter stuff is redundent with the reference
   counting done internally on Python objects.  In C we can just peak
   at the reference counter.

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

########################################################################
# Simulate utility functions from the Python C API.  These functions
# help use detect the case where we have a self-referential list and a
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

########################################################################
# Decorators are for error-checking and code clarity.  They verify
# (most of) the invariances given above.  They're replaced with no_op()
# if debugging_level == NO_DEBUG.

def modifies_self(f):
    "Decorator for member functions which require write access to self"
    def g(self, *args, **kw):
        assert self.refcount == 1 or self.refcount is None
        rv = f(self, *args, **kw)
        assert self.refcount == 1 or self.refcount is None
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
# Utility functions and decorators for dealing for fixing up index
# parameters.

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
                assert len(self.children) > 1 or len(self.children) == 0
                for child in self.children:
                    assert isinstance(child, BList)
                    assert half <= len(child.children) <= limit
            assert self.refcount >= 1 or self.refcount is None
        except:
            print self.debug()
            raise

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

    def __check_reference_count(self):
        # Check that we're counting references properly
        if self.refcount == None: return  # User object
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

        # The caller may be about to increment the reference counter, so
        # total == self.refcount or total+1 == self.refcount are OK
        assert total == self.refcount or total+1 == self.refcount,\
               (total, self.refcount)

    def _decref(self):
        assert self.refcount is not None
        assert self.refcount > 0
        if self.refcount == 1:
            # We're going to be garbage collected.  Remove all references
            # to other objects.

            # This step is technically unnecessary.  The garbage
            # collector will find the descendents either way.
            # However, if we're sharing the children, this releases
            # them so the other tree can write to them without having
            # to make a copy.  On the other hand, if we're not
            # sharing, then we're wasting our time here.
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
        return 1 + self.children[0]._get_height()

    @modifies_self
    def __forget_children(self, i=0, j=None):
        "Remove links to some of our children, decrementing their refcounts"
        if j is None: j = len(self.children)
        if not self.leaf:
            for k in range(i, j):
                self.children[k]._decref()
        del self.children[i:j]

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
    # Functions for dealing with underflows and overflows

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
    def __merge_trees(left_subtree, left_depth, right_subtree, right_depth):
        """Merge two subtrees of potentially different heights.

        Returns a tuple of the new, combined subtree and its depth.

        Depths are the depth in the parent, not their height.
        """

        assert left_subtree.refcount == 1
        assert right_subtree.refcount == 1

        if left_depth == right_depth:
            root = _BList()
            root.children = [left_subtree, right_subtree]
            root.leaf = False
            collapse = root.__underflow(0)
            if not collapse:
                collapse = root.__underflow(1)
            if collapse: adj = 0
            else: adj = -1
            overflow = None
        elif left_depth < right_depth: # Left is larger
            root = left_subtree
            overflow = root._insert_subtree(-1, right_subtree,
                                            right_depth - left_depth - 1)
        else: # Right is larger
            root = right_subtree
            overflow = root._insert_subtree(0, left_subtree,
                                            left_depth - right_depth - 1)
        adj = root.__overflow_root(overflow)
        return root, max(left_depth, right_depth) + adj

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

        Always calls ._adjust_n() (possible via .__collapse).

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
            k = len(self.children) + k
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
            return self.__insert_here(side, overflow)

        if side < 0:
            side = len(self.children)

        sibling = self.__insert_here(side, subtree)

        return sibling

    @modifies_self
    def __reinsert_subtree(self, k, depth):
        'Child at position k is too short by "depth".  Fix it'
        assert self.children[k].refcount == 1
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
        self._adjust_n()

    def __del__(self):
        try:
            #assert self.refcount is None or self.refcount == 0
            self.refcount = 1
            self.__forget_children()
            self.refcount = 0
        except:
            import traceback
            traceback.print_exc()
            raise

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
        # Make a not-user-visible roots for the subtrees
        right = _BList(other)
        left = _BList(self)

        left_height = left._get_height()
        right_height = right._get_height()

        root = BList.__merge_trees(left, -left_height, right, -right_height)[0]
        self.__become(root)
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
            self.__reinsert_subtree(k, depth)
            return 0

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
            subtree, depth = BList.__merge_trees(left, collapse_left,
                                                 right, collapse_right)
            del left
            del right
            self.children.insert(k, subtree)
        elif deleted_k or (not deleted_k2 and not collapse_left):
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
        self.__reinsert_subtree(k, depth)
        return self.__underflow(k)

    def __init_from_seq(self, seq):
        # Try the common case of a sequence < limit in length
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
        self.n = len(self.children)

        # No such luck, build bottom-up instead.
        # The sequence data so far goes in a leaf node.
        cur = _BList()
        cur.__become(self)
        self.__forget_children()
        cur._check_invariants()

        # Build the base of the tree
        lists = []
        while 1:
            try:
                x = iterator.next()
            except StopIteration:
                break
            if len(cur.children) == limit:
                cur.n = limit
                cur._check_invariants()
                lists.append(cur)
                cur = _BList()
            cur.children.append(x)

        if cur.children:
            lists.append(cur)
            cur.n = len(cur.children)

        # Iteratively build the next higher layer of the tree, until we have
        # a single root
        while len(lists) > 1:
            cur = lists[-1]

            if len(cur.children) < half:
                left = lists[-2]
                cur.children[:0] = left.children[half:]
                del left.children[half:]
                left._adjust_n()
                cur._adjust_n()

            cur = _BList()
            cur.leaf = False
            new_lists = []
            for i in range(len(lists)):
                if i and not i % limit:
                    new_lists.append(cur)
                    cur._check_invariants()
                    cur = _BList()
                    cur.leaf = False
                cur.children.append(lists[i])
                cur.n += lists[i].n
            if cur.children:
                new_lists.append(cur)
            lists = new_lists

        self.children[:] = lists
        self._adjust_n()
        self.leaf = False
        self.__collapse()
        self._check_invariants()

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
            rv = 'BList(leaf=%s, n=%s, r=%s, %s)' % (
                str(self.leaf), str(self.n), str(self.refcount),
                '\n%s' % indent +
                ('\n%s' % indent).join([x.debug(indent+'  ')
                                        for x in self.children]))
        else:
            rv = 'BList(leaf=%s, n=%s, r=%s, %s)' % (
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
                raise ValueError('attempt to assign sequence of size %d '
                                 'to extended slice of size %d'
                                 % (len(y), length))
            k = 0
            for j in xrange(start, stop, step):
                self[j] = y[k]
                k += 1
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
            self._adjust_n()
            return

        left = self
        right = _BList(self)
        del left[i:]
        del right[:j]
        left += other
        left += right

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
        mask = 1
        powers = [self]
        rv = BList()
        while mask <= n:
            if mask & n:
                rv += powers[-1]
            powers.append(powers[-1] + powers[-1])
            mask <<= 1
        return rv

    __rmul__ = __mul__

    @user_callable
    @modifies_self
    def __imul__(self, n):
        self.__become(self * n)
        return self

    @user_callable
    @modifies_self
    def sort(self, *args, **kw):
        # This implementation uses 3*N memory.  Yuck.
        # It would likely be more efficient to do a proper mergesort,
        # though that requires a lot more code.
        tmp = list(self)
        saved = _BList(self)
        no_list = _BList()
        self.__become(no_list)
        try:
            tmp.sort(*args, **kw)
            if self.n:
                raise ValueError('list modified during sort')

            tmp2 = _BList(tmp)
            self.__become(tmp2)
        except:
            self.__become(saved)
            raise

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
    and wreaking havoc with our pointers.  Copying the list is O(1),
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
    lst = BList()
    n = 512
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

if __name__ == '__main__':
    main()
