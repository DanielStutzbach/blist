#!/usr/bin/python

"""

Rules:

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
  BList objects that refer to the object (including itself).
- User-nodes may never be referred to by another BList object.
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
  called "positions".
- A node's children are numbered 0 to len(self.pointers)-1.  These are
  called "indexes" and should not be confused with positions.

Invariants:

- User-nodes always have a refcount of at least 1
- User-callable methods may not cause the reference counter to decrement
- When a BList member function returns, the BList is the root of a valid
  subtree.  Exception: member functions that will only be called
  via self do not need to adhere to this requirement.
- When a BList member function returns, the .n member variable must
  contain the total number of leaf elements of all its descendents.
- If a function may cause a BList to overflow, the function has the
  following return types:
  - None, if the BList did not overflow
  - A valid BList subtree containing a new sibling for the BList that
    was called
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
  - None, if the BList did not collapse, or if it is now empty (self.n == 0)
  - A positive integer indicating how many layers have collapsed (i.e., how
    much shorter the subtree is compared to before the function call).

Observations:

- If a parent calls a child's method that may cause a BList to
  underflow, the parent must detect the underflow and merge the child
  before returning.

Comparision of cost of operations with list().  N is the size of "self",
M is the size of the argument.  For slice operations, M is the length
of the slice.

   Operation            list               BList
---------------     ------------  -----------------------
init from seq       O(M)          O(M)
copy                O(M)          O(1)
append              O(1)          O(log N)
insert              O(N)          O(log N)
__mul__             O(N*M)        O(log M)
__delitem__         O(N)          O(log N)
__len__             O(1)          O(1)
iteration           O(N)          O(N)
__getslice__        O(M)          O(log N)
__delslice__        O(N)          O(log N + M)
__setslice__        O(N+M)        O(log N + log M)        [1]
extend              O(M)          O(abs(log N - log M))   [2]
__sort__            O(N*log N)    O(N*log N)
index               O(M)          O(M*log N)              [2]
remove              O(N)          O(N*log N)              [3]
count               O(N)          O(N)
extended slicing    O(M)          O(M*log N)
__cmp__             O(min(N,M))   O(min(N,M))

[1]: Plus O(M) if the items being added are not in a BList
[2]: Could be made O(M + log N) with a little work
[3]: Could be made O(N) with a little work

For BLists smaller than "limit" elements, each operation essentially
reduces to the equivalent list operation, so there is little-to-no
overhead for the common case of small lists.

Known Bugs:
 - Copy-on-write is thread un-friendly in the Python version.  In C,
   we automatically have the Python interpreter lock so there's less
   to worry about, but we'd need to careful whenever we call back out
   to userspace (i.e., in .sort() or .index())

 - The iterator doesn't check to see if the list has been modified
   underneath.

Suspected Bugs:
 - Copy-on-write and reference counting has not been thoroughly tested.

"""

import copy, types

limit = 8 # Low, for testing purposes
half = limit//2
assert limit % 2 == 0
assert limit >= 8

# Simulate utility functions from the Python C API

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

def modifies_self(f):
    "Decorator for member functions which require read-write access to self"
    def g(self, *args, **kw):
        assert self.refcount == 1
        rv = f(self, *args, **kw)
        assert self.refcount == 1
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
        assert self.refcount >= 1
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

class BListIterator:
    def __init__(self, lst):
        self.stack = []
        while not lst.leaf:
            self.stack.append((lst, 1))
            lst = lst.pointers[0]
        self.stack.append((lst, 0))

    def next(self):
        while 1:
            if not self.stack: raise StopIteration
            p, i = self.stack.pop()
            if i < len(p.pointers):
                break

        self.stack.append((p, i+1))

        while not p.leaf:
            p = p.pointers[i]
            i = 0
            self.stack.append((p, i+1))

        return p.pointers[i]

class BList:
    def _check_invariants(self):
        try:
            if self.leaf:
                assert self.n == len(self.pointers)
            else:
                assert self.n == sum(child.n for child in self.pointers)
                assert len(self.pointers) > 1 or len(self.pointers) == 0
                for child in self.pointers:
                    assert isinstance(child, BList)
                    assert half <= len(child.pointers) <= limit
            assert self.refcount >= 1
        except:
            print self.debug()
            raise

    def __init__(self, seq=[]):
        self.leaf = True

        # Points to children
        self.pointers = []

        # Number of leaf elements that are descendents of this node
        self.n = 0

        self.refcount = 1

        # We can copy other BLists in O(1) time :-)
        if isinstance(seq, BList):
            self.__become(seq)
            self._check_invariants()
            return

        # Try the common case of a sequence < limit in length
        iterator = iter(seq)
        for i in range(limit):
            try:
                x = iterator.next()
            except StopIteration:
                self.n = len(self.pointers)
                self._check_invariants()
                return
            except AttributeError:
                raise TypeError('instance has no next() method')
            self.pointers.append(x)
        self.n = len(self.pointers)

        # No such luck, build bottom-up instead.
        # The sequence data so far goes in a leaf node.
        cur = BList()
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
            if len(cur.pointers) == limit:
                cur.n = limit
                cur._check_invariants()
                lists.append(cur)
                cur = BList()
            cur.pointers.append(x)

        if cur.pointers:
            lists.append(cur)
            cur.n = len(cur.pointers)

        # Iteratively build the next higher layer of the tree, until we have
        # a single root
        while len(lists) > 1:
            cur = lists[-1]

            if len(cur.pointers) < half:
                left = lists[-2]
                cur.pointers[:0] = left.pointers[half:]
                del left.pointers[half:]
                left._adjust_n()
                cur._adjust_n()

            cur = BList()
            cur.leaf = False
            new_lists = []
            for i in range(len(lists)):
                if i and not i % limit:
                    new_lists.append(cur)
                    cur._check_invariants()
                    cur = BList()
                    cur.leaf = False
                cur.pointers.append(lists[i])
                cur.n += lists[i].n
            if cur.pointers:
                new_lists.append(cur)
            lists = new_lists

        self.pointers = lists
        self._adjust_n()
        self.leaf = False
        self.__collapse()
        self._check_invariants()

    def __copy__(self):
        return BList(self)

    @staticmethod
    def __new_sibling(children, leaf):
        """Non-default constructor.  Create a node with specific children.

        We steal the reference counters from the caller.
        """

        self = BList()
        self.pointers = children
        self.leaf = leaf
        self._adjust_n()
        return self

    @user_callable
    def __getslice__(self, i, j):
        "User-visible function"

        # If the indices were negative, Python has already added len(self) to
        # them.  If they're still negative, treat them as 0.
        if i < 0:
            i = 0
        if j < 0:
            j = 0

        if j <= i:
            return BList()

        if i >= self.n:
            return BList()

        if self.leaf:
            return BList(self.pointers[i:j])

        # XXX: Bug.  The right and left sub-slices may not be valid
        # trees, and may even be collapsed.  We need to splice them
        # back in at the right spot.  This is kind of the opposite of
        # __delslice__ where we are deleted the middle.  Here, we
        # remove the outside fro the returned value.

        #rv = BList()
        #rv.leaf = False
        #p, k, so_far = self.__locate(i)
        #rv.pointers.append(p.__getslice__(i-so_far, j-so_far))
        #
        #so_far += p.n
        #if j > so_far:
        #    for k in range(k+1, len(self.pointers)):
        #        p = self.pointers[k]
        #        if j < so_far + p.n:
        #            x = p.__getslice__(0, j-so_far)
        #            if x:
        #                rv.pointers.append(x)
        #            break
        #        so_far += p.n
        #        p._incref()
        #        rv.pointers.append(p)
        #
        #rv.__collapse()
        #rv._adjust_n()

        rv = BList(self)
        del rv[j:]
        del rv[:i]

        return rv

    @modifies_self
    def __borrow_right(self, k):
        "Child k has underflowed.  Borrow from k+1"
        p = self.pointers[k]
        right = self.__prepare_write(k+1)
        total = len(p.pointers) + len(right.pointers)
        split = total//2

        assert split >= half
        assert total-split >= half

        migrate = split - len(p.pointers)

        p.pointers.extend(right.pointers[:migrate])
        del right.pointers[:migrate]
        right._adjust_n()
        p._adjust_n()

    @modifies_self
    def __borrow_left(self, k):
        "Child k has underflowed.  Borrow from k-1"
        p = self.pointers[k]
        left = self.__prepare_write(k-1)
        total = len(p.pointers) + len(left.pointers)
        split = total//2

        assert split >= half
        assert total-split >= half

        migrate = split - len(p.pointers)

        p.pointers[:0] = left.pointers[-migrate:]
        del left.pointers[-migrate:]
        left._adjust_n()
        p._adjust_n()

    @modifies_self
    def __merge_right(self, k):
        "Child k has underflowed.  Merge with k+1"
        p = self.pointers[k]
        for p2 in self.pointers[k+1].pointers:
            if not self.pointers[k+1].leaf:
                p2._incref()
            p.pointers.append(p2)
        self.pointers[k+1]._decref()
        del self.pointers[k+1]
        p._adjust_n()

    @modifies_self
    def __merge_left(self, k):
        "Child k has underflowed.  Merge with k-1"
        p = self.pointers[k]
        if not self.pointers[k-1].leaf:
            for p2 in self.pointers[k-1].pointers:
                p2._incref()
        p.pointers[:0] = self.pointers[k-1].pointers
        self.pointers[k-1]._decref()
        del self.pointers[k-1]
        p._adjust_n()

    @may_collapse
    @modifies_self
    def __collapse(self):
        "Collapse the tree, if possible"
        if len(self.pointers) != 1 or self.leaf:
            self._adjust_n()
            return 0

        p = self.pointers[0]
        self.__become(p)
        return 1

    @may_collapse
    @modifies_self
    def __underflow(self, k):
        """Check if children k-1, k, or k+1 have underflowed.

        If so, move things around until self is the root of a valid
        subtree again, with some caveats.  If k has underflowed, it
        will definitely be repaired.  Otherwise, the function checks
        k-1.  If k-1 is also okay, then k+1 is checked.

        Always call ._adjust_n() (possible via .__collapse) to restore
        a sane state.

        This function may collapse the tree.

        """

        if self.leaf:
            self._adjust_n()
            return 0
        p = self.__prepare_write(k)
        short = half - len(p.pointers)

        while short > 0:
            if k+1 < len(self.pointers) \
               and len(self.pointers[k+1].pointers) - short >= half:
                self.__borrow_right(k)
            elif k > 0 and len(self.pointers[k-1].pointers) - short >= half:
                self.__borrow_left(k)
            elif k+1 < len(self.pointers):
                self.__merge_right(k)
            elif k > 0:
                self.__merge_left(k)
                k = k - 1
            else:
                # No siblings for p, this must be the root
                break

            p = self.__prepare_write(k)
            short = half - len(p.pointers)

        if k > 0 and len(self.pointers[k-1].pointers) < half:
            collapse = self.__underflow(k-1)
            if collapse: return collapse
        if k+1 < len(self.pointers) \
               and len(self.pointers[k+1].pointers) <half:
            collapse = self.__underflow(k+1)
            if collapse: return collapse

        return self.__collapse()

    def _decref(self):
        assert self.refcount > 0
        if self.refcount == 1:
            # We're going to be garbage collected.  Remove all references
            # to other objects.
            self.__forget_children()
        self.refcount -= 1

    def _incref(self):
        self.refcount += 1

    @modifies_self
    def __forget_children(self, i=0, j=None):
        if j is None: j = len(self.pointers)
        if not self.leaf:
            for k in range(i, j):
                self.pointers[k]._decref()
        del self.pointers[i:j]

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

        #self.n -= 1
        #if self.leaf:
        #    # Parent is responsible for cleaning up under-flows
        #    del self.pointers[i]
        #    return
        #
        #p, k, so_far = self.__locate(i)
        #p = self.__prepare_write(k)
        #p.__delitem__(i-so_far)
        #
        #if len(p.pointers) < half:
        #    # This may collapse the subtree, but that's okay since we are
        #    # the user-node.
        #    # XXX.  Bug!  Recursive, so not necessarily the user-node
        #    self.__underflow(k)

    @user_callable
    @modifies_self
    def __delslice__(self, i, j):
        "User-visible function"
        if i >= j:
            return
        self._delslice(i, j)

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
            # Delete everything.  No need to recurse, let the GC deal with it
            self.__forget_children()
            self.n = 0
            return 0

        if self.leaf:
            del self.pointers[i:j]
            self.n = len(self.pointers)
            return 0

        p, k, so_far = self.__locate(i)
        p2, k2, so_far2 = self.__locate(j-1)

        if k == k2:
            # All of the deleted elements are contained under a single
            # child of this node.  Recurse and check for a short
            # subtree and/or underflow

            assert p == p2 and so_far == so_far2
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
        p2 = self.__prepare_write(k2)
        collapse_right = p2._delslice(max(0, i - so_far2), j - so_far2)

        deleted_k = False
        deleted_k2 = False

        # Delete [k+1:k2]
        self.__forget_children(k+1, k2)
        k2 = k+1

        # Delete k1 and k2 if they are empty
        if not self.pointers[k2].n:
            self.pointers[k2]._decref()
            del self.pointers[k2]
            deleted_k2 = True
            collapse_right = 0
        if not self.pointers[k].n:
            self.pointers[k]._decref()
            del self.pointers[k]
            deleted_k = True
            collapse_left = 0

        # No children left
        if not len(self.pointers):
            self.n = 0
            assert deleted_k and deleted_k2
            return 0

        if deleted_k and deleted_k2:
            # It was a clean delete.  No messy subtrees.
            return self.__collapse()

        # The left and right may have collapsed and/or be in an
        # underflow state.  Clean them up.  Work on fixing collapsed
        # trees first, then worry about underflows.

        if not deleted_k and not deleted_k2 \
               and collapse_left and collapse_right:
            # Both exist and collapsed.  Merge them into one subtree.
            left = self.pointers.pop(k)
            right = self.pointers.pop(k)
            subtree, depth = BList.__merge_trees(left, collapse_left,
                                                 right, collapse_right)
            self.pointers.insert(k, subtree)
        elif deleted_k or (not deleted_k2 and not collapse_left):
            # Only the right potentially collapsed, point there
            # Remember to check the left for an underflow later
            k = k + 1
            depth = collapse_right
        else:
            depth = collapse_left

        # At this point, we have a potentially short subtree at k,
        # with depth "depth".

        if not depth or len(self.pointers) == 1:
            # Doesn't need merging, or no siblings to merge with
            return depth + self.__underflow(k)

        # We definitely have a short subtree at k, and we have other children
        self.__reinsert_subtree(k, depth)

        # Okay, we've taken care of collapsed subtrees, now check for
        # underflows
        if not self.leaf:
            for m in range(max(0, k-1), min(len(self.pointers), k+2)):
                if len(self.pointers[m].pointers) < half:
                    return self.__underflow(m)

        # Possibly collapse self
        return self.__collapse()

    @modifies_self
    def __reinsert_subtree(self, k, depth):
        assert self.pointers[k].refcount == 1
        subtree = self.pointers.pop(k)
        if len(self.pointers) > k:
            # Merge right
            p = self.__prepare_write(k)
            overflow = p._insert_subtree(0, subtree, depth-1)
            if overflow:
                self.pointers.insert(k+1, overflow)
        else:
            # Merge left
            p = self.__prepare_write(k-1)
            overflow = p._insert_subtree(-1, subtree, depth-1)
            if overflow:
                self.pointers.insert(k, overflow)
        self._adjust_n()

    @staticmethod
    def __merge_trees(left_subtree, left_depth, right_subtree, right_depth):
        """Merge two subtrees of potentially different heights.

        Returns a tuple of the new, combined subtree and its depth.

        Depths are the depth in the parent, not their height.
        """

        assert left_subtree.refcount == 1
        assert right_subtree.refcount == 1

        if left_depth == right_depth:
            root = BList()
            root.pointers = [left_subtree, right_subtree]
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

    @parent_callable
    @modifies_self
    def _adjust_n(self):
        "Recompute self.n"
        if self.leaf:
            self.n = len(self.pointers)
        else:
            self.n = sum(x.n for x in self.pointers)

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
            y = BList(y)
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
            self.pointers[i] = y
            return

        p, k, so_far = self.__locate(i)
        p = self.__prepare_write(k)
        p.__setitem__(i-so_far, y)

    def __locate(self, i):
        """We are searching for the child that contains leaf element i.

        Returns a 3-tuple: (the child object, our index of the child,
                            the number of leaf elements before the child)
        """
        if self.leaf:
            return self.pointers[i], i, i

        so_far = 0
        for k in range(len(self.pointers)):
            p = self.pointers[k]
            if i < so_far + p.n:
                return p, k, so_far
            so_far += p.n
        else:
            return self.pointers[-1], len(self.pointers)-1, so_far - p.n
            raise 'Impossible!'

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
            return self.pointers[i]

        p, k, so_far = self.__locate(i)
        assert i >= so_far
        return p.__getitem__(i - so_far)

    @parent_callable
    def _str(self, f):
        "Recursive version of __str__"
        if self.leaf:
            return ', '.join(f(x) for x in self.pointers)
        else:
            return ', '.join(x._str(f) for x in self.pointers)

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
        "Return a string that shows the internal structure of the BList"
        indent = indent + ' '
        if not self.leaf:
            rv = 'BList(leaf=%s, n=%s, %s)' % (
                str(self.leaf), str(self.n), '\n%s' % indent +
                ('\n%s' % indent).join([x.debug(indent+'  ')
                                        for x in self.pointers]))
        else:
            rv = 'BList(leaf=%s, n=%s, %s)' % (
                str(self.leaf), str(self.n), str(self.pointers))
        return rv

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
        - replace self.pointers[pt] with the copy
        - return the copy
        """

        if pt < 0:
            pt = len(self.pointers) + pt
        if not self.leaf and self.pointers[pt].refcount > 1:
            new_copy = BList()
            new_copy.__become(self.pointers[pt])
            self.pointers[pt]._decref()
            self.pointers[pt] = new_copy
        return self.pointers[pt]

    @user_callable
    @modifies_self
    def append(self, item):
        "User-visible function"
        return self.insert(len(self), item)

    @user_callable
    @modifies_self
    @allow_negative1
    def insert(self, i, item):
        "User-visible function"
        if i > self.n:
            i = self.n
        overflow = self._insert(i, item)
        self.__overflow_root(overflow)

    @modifies_self
    def __overflow_root(self, overflow):
        "Handle the case where a user-visible node overflowed"
        if not overflow: return 0
        child = copy.copy(self)
        self.pointers = [child, overflow]
        self.leaf = False
        self._adjust_n()
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
            k = len(self.pointers) + k
        if len(self.pointers) < limit:
            self.pointers.insert(k, item)
            collapse = self.__underflow(k)
            assert not collapse
            self._adjust_n()
            return None

        sibling = BList.__new_sibling(self.pointers[half:], self.leaf)
        del self.pointers[half:]

        if k < half:
            self.pointers.insert(k, item)
            collapse = self.__underflow(k)
            assert not collapse
        else:
            sibling.pointers.insert(k - half, item)
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
            side = len(self.pointers)

        sibling = self.__insert_here(side, subtree)

        return sibling

    @may_overflow
    @modifies_self
    def _insert(self, i, item):
        """Recursive to find position i, and insert item just there.

        This function may cause an overflow.

        """
        if self.leaf:
            return self.__insert_here(i, item)

        self.n += 1
        p, k, so_far = self.__locate(i)
        p = self.__prepare_write(k)
        overflow = p._insert(i - so_far, item)
        if not overflow: return
        return self.__insert_here(k+1, overflow)

    @user_callable
    def __len__(self):
        "User-visible function"
        return self.n

    @user_callable
    def __iter__(self):
        "User-visible function"
        return BListIterator(self)

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
        other = BList(other)

        # Efficiently handle the common case of small lists
        if self.leaf and other.leaf and self.n + other.n <= limit:
            self.pointers[i:j] = other.pointers
            self._adjust_n()
            return

        left = self
        right = BList(self)
        del left[i:]
        del right[:j]
        left += other
        left += right

    @parent_callable
    def _get_height(self):
        if self.leaf:
            return 1
        return 1 + self.pointers[0]._get_height()

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
    def __iadd__(self, other):
        # Make a not-user-visible root for the other subtree
        right = BList(other)
        left = BList(self)

        left_height = left._get_height()
        right_height = right._get_height()

        root = BList.__merge_trees(left, -left_height, right, -right_height)[0]
        self.__become(root)
        return self

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
        for k in range(i, j):
            if k >= len(self): break
            if self[k] == item:
                return k
        raise ValueError('list.index(x): x not in list')

    @user_callable
    @modifies_self
    def remove(self, item):
        for i in range(len(self)):
            if self[i] == item:
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
        self.pointers.reverse()
        if self.leaf: return
        for i in range(len(self.pointers)):
            p = self.__prepare_write(i)
            p.reverse()

    @user_callable
    @modifies_self
    def sort(self, *args, **kw):
        # This implementation uses 3*N memory.  Yuck.
        # It would likely be more efficient to do a proper mergesort,
        # though that requires a lot more code.
        tmp = list(self)
        saved = BList(self)
        no_list = BList()
        self.__become(no_list)
        try:
            tmp.sort(*args, **kw)
            if self.n:
                raise ValueError('list modified during sort')

            tmp2 = BList(tmp)
            self.__become(tmp2)
        except:
            self.__become(saved)
            raise

    @modifies_self
    def __become(self, other):
        if id(self) == id(other):
            self._adjust_n()
            return
        if not other.leaf:
            for child in other.pointers:
                child._incref()
        other._incref()  # Other may be one of our children
        self.__forget_children()
        self.n = other.n
        self.pointers = copy.copy(other.pointers)
        self.leaf = other.leaf
        other._decref()

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
    assert len(lst2.pointers) == limit-1

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
    for i in range(290):
        if len(x*i) != i:
            print 'mul failure', i
            print (x*i).debug()
            break

if __name__ == '__main__':
    main()
