Implementation Details
======================

BLists are based on B+Trees, which are a dictionary data structure
where each element is a (key, value) pair and the keys are kept in
sorted order.  B+Trees internally use a tree representation.  All data
is stored in the leaf nodes of the tree, and all leaf nodes are at the
same level.  Unlike binary trees, each node has a large number of
children, stored as an array of references within the node.  The
B+Tree operations ensure that each node always has between "limit/2"
and "limit" children (except the root which may have between 0 and
"limit" children).  When a B+Tree has fewer than "limit/2" elements,
they will all be contained in a single node (the root).

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
then repair the child, either by stealing elements from one of the
child's sibling or merging the child with one of its sibling.  If the
parent performs a merge, the parent itself may underflow.

If a node has only one element, the tree collapses.  The node replaces
its one child with its grandchildren.  When removing a single element,
this can only happen at the root.

Removing a range
----------------

The __delslice__ method to remove a range of elements is the most
complex operation in the BList implementation.  The first step is to
locate the common parent of all the elements to be removed.  The
parent deletes any children who will be completely deleted (i.e., they
are entirely within the range to be deleted).  The parent also has to
deal with two children who may be partially deleted: they contain the
left and right boundaries of the deletion range.

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
subtree.  It just creates a new node with new references to the
child's children.  In other words, the child and the copy are now
joint parents of their children.

This assumes that no other code will gain references to internal BList
nodes.  The internal nodes are never exposed to the user, so this is a
reasonably safe assumption.  In the worst case, if the user manages to
gain a reference to an internal BList node (such as through the gc
module), it will just prevent the BList code from modifying that node.
It will create a copy instead.  User-visible nodes (i.e., the root of
a tree) have no parents and are never shared children.

Why is this copy-on-write operation so useful?

Consider the common idiom of performing an operation on a slice of a
list.  Normally, this requires making a copy of that region of the
list, which is expensive if the region is large.  With copy-on-write,
__getslice__ takes logarithmic time.

Structure
---------

Each BList node has the following member variables:

n: 
    the total number of user data elements below the node, equal to
    len(children) for leaf nodes

num_children: 
    the number of children immediately below the node


leaf:         
    true if this node is a leaf node (has user data as children),
    false if this node is an interior node (has nodes as children)

children:     
    an array of references to the node's children

Global Constants
----------------

LIMIT:
    the maximum size of .children, must be even and >= 8

HALF:
    LIMIT//2, the minimum size of .children for a valid node, other
    than the root

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
- If a user-visible function does not modify the BList, the BList's
  internal structure must not change.  This is important for
  supporting iterators.
- Functions exposed to the user must ensure these invariants are true
  when they return.
- When a recursive function returns, the invariants must be true as if
  the child were a root node.
- Completely private functions may temporarily violate these invariants.

Reference Counters
------------------

In CPython, when Py_DECREF() decrements a reference counter to zero,
completely arbitrary code may be executed by way of the garbage
collector and __del__.  For that reason, it's critical that any data
structures are in a coherent state when Py_DECREF() is called.

Toward that end, the BList implementation contains the helper
functions, decref_later() and xdecref_later().  If an objects
reference counter is greater than 1, decref_later() will decrement
right away.  Otherwise, it will append the object to a global list to
be decremented just before control returns to the user.
decref_later() must be used instead of Py_DECREF() any time a
reference counter might be decreased to 0 within a BList function.

decref_later() appends the reference to a global list.  All of the
references in the list are decremented when decref_flush() is called.
decref_flush() is recursion-safe, so everything will work out properly
if a __del__ method fired by decref_flush() calls other BList
routines.

decref_flush() must all be called by user-callable functions.  It must
not be called any function that might be called by other BList
functions, as the caller may not expect the list to mutate in
arbitrary ways via __del__.

When we can prove that a reference counter is already greater than 1,
use SAFE_DECREF() or SAFE_XDECREF().  When Py_DEBUG is defined, these
macros will verify that the reference counter is greater than 1.   

Debugging
---------

The BList implementation contains extensive debugging routines to
verify the invariants, which only operate if Py_DEBUG is defined.
Nearly ever function begins with a call like this:

    invariants(self, flags);

where "flags" specifies that invariants that the function promises to
maintain.

When such a function returns, it must hand its return type and value
to the verification routines.  For example, a function that returns an
integer would return as follows:

    return _int(some_value);

The available flags are as follows:

VALID_RW: 
    This is a read-write function that may modify the BList.  "self"
    must be a root node or have exactly one parent, both when the
    function starts and when it returns.

VALID_PARENT:
    This is a function meant to be called by a parent on a child.  
    "self" must maintain all the invariants, both when the function
    stars and it when it returns.

VALID_ROOT:
    "self" must be a root node.  The function must maintain all
    invariants.

    Implies VALID_PARENT.

VALID_USER:
    This is a user-called function.  "self" is a root node.  "self"
    must maintain all the invariants, both when the function starts and
    it when it returns.

    Implies VALID_ROOT.

VALID_OVERFLOW:
    The function may cause "self" to overflow.  If so, the function
    creates a new right-hand sibling for "self" and returns it.  If
    not, the return value is NULL.

VALID_COLLAPSE:
    The function may cause "self" to underflow.  If so, the function
    collapses the tree and returns a positive integer indicating the
    change in the height of the tree.  If not, the function returns 0.

VALID_DECREF:
    The function may call decref_flush().  It must not be called by
    any other BList functions.

    Requires VALID_USER.

Whenever BList code calls a function that might execute arbitrary
code, the call must be surrounded by the macros DANGER_BEGIN and
DANGER END, like this:

    DANGER_BEGIN;
    cmp = PyObject_RichCompareBool(item, w->ob_item[i], Py_EQ);
    DANGER_END;

The macros serve as useful visual aid to the programmer to remember
that the code may modify the list in unexpected ways.  Additionally,
in debug mode they maintain internal state used by the validation
code.

Root Node Extensions
--------------------

The data structure for BList root nodes contains additional fields to
speed up certain operations.  Specifically, the root node contains an
index of the tree's leaf nodes, to speed up __getitem__ and
__setitem__ operations (making them O(1) worst-case amortized time
rather than O(log n)).

The index is broken into INDEX_FACTOR elements, where INDEX_FACTOR <=
HALF.  

index_list:
    An array of pointers to leaf nodes.  index_list[i/INDEX_FACTOR]
    points to the leaf that contains position i, unless it is dirty.

offset_list:
    An array of integers, corresponding to the entries in the
    index_list.  offset_list[j] provides the position of the *first*
    child of index_list[j].

setclean_list:
    An array of bits, each bit corresponding to one entry in
    index_list.  Each bit indicates whether an indexed leaf is ready
    for a __setitem__ operation.  A leaf is ready iff the leaf and all
    of its ancestors are owned exclusively by one BList root (i.e.,
    they have a reference count of 1).

index_length:
    The size of the memory pointed to by index_list and offset_list.

dirty:
    An array of integers representing a binary tree, indicating which
    parts of the index_list are valid and which are dirty.  For some
    even integer, i, dirty[i] and dirty[i+1] are integers pointing to
    the children of node i.  A negative values (CLEAN or DIRTY)
    indicates that there is no child.

    The binary tree corresponds with the index_list, as if the length
    of the index_list were rounded up to the nearest power of two.
    For example, if the root of the tree is DIRTY then the whole
    index_list is dirty.  If the index_list has a length of 8 and the
    root points to CLEAN on the left and DIRTY on the right, then the
    first 4 indexes are clean and the second 4 are dirty.

dirty_length:
    The size of the memory pointed to by dirty.

dirty_root:
    An integer pointing to the root node of dirty, or a negative value
    (CLEAN_RW, CLEAN, or DIRTY).

free_root:
    Another integer pointer into dirty.  free_root points to an entry
    that is not currently in use to indicate clean/dirty status.
    Instead, the entry forms a binary tree of other entries that are
    not currently in use.  The free list allows entries for the dirty
    tree to be allocated quickly without malloc/free.

last_n:
    The length of the BList object when the index was last set to all
    dirty.  last_n is used only for debugging purposes.

