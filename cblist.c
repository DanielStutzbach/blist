#include <python2.5/Python.h>

/* XXX, DECREF can cause callbacks into the object through user
 * __del__ methods.  We are not very good about postponing these until
 * we have finished modifying the list.
 *
 * XXX, prepare_write can cause out-of-memory, which must be handled
 *
 * Need to prevent users form making lists bigger than PY_SSIZE_T_MAX
 *
 * XXX, .reversed()
 *
 * XXX, .sort()
 */

#define LIMIT (8)       // Maximum size, currently low (for testing purposes)
#define HALF  (LIMIT/2) // Minimum size

#if LIMIT & 1
#error LIMIT must be divisible by 2
#endif

#if LIMIT < 8
#error LIMIT must be at least 8
#endif

#define shift_right(self, k, n) \
    (memmove(&(self)->children[(k)+(n)], &(self)->children[(k)], \
             sizeof(PyObject *) * ((self)->num_children - (k))))
#define shift_left(self, k, n) \
    (memmove(&(self)->children[(k)-(n)], &(self)->children[(k)], \
             sizeof(PyObject *) * ((self)->num_children - (k))))
#define copy(self, k, other, k2, n) \
    (memmove(&(self)->children[k], &(other)->children[k2], \
             sizeof(PyObject *) * n))

typedef struct PyBList {
        PyObject_HEAD
        unsigned n;                 // Total number of user-object descendents
        unsigned num_children;      // Number of immediate children
        int leaf;                   // Boolean value
        PyObject *children[LIMIT];  // Immediate children
} PyBList;

typedef struct {
        PyBList *lst;
        int i;
} point_t;

typedef struct {
        PyBList *blist;     /* Set to NULL when iterator exhausted */
        point_t *stack;
        int depth;
        int remaining;
} iter_t;

typedef struct {
        PyObject_HEAD
        iter_t *iter;
} blistiterobject;

/* Forward declarations */
void iter_delete(iter_t *iter);
PyObject *iter_next(iter_t *iter);
iter_t *iter_new2(PyBList *lst, int start, int stop);
#define iter_new(lst) (iter_new2((lst), 0, (lst)->n))
static PyObject *blist_iter(PyObject *);
static void blistiter_dealloc(blistiterobject *);
static int blistiter_traverse(blistiterobject *, visitproc, void *);
PyTypeObject PyBList_Type;
PyTypeObject PyUserBList_Type;
static PyObject *blistiter_next(PyObject *);
static PyObject *blist_iter(PyObject *oseq);
static PyObject *blist_iter2(PyBList *seq, int start, int stop);
static PyObject *blist_repeat(PyBList *self, ssize_t n);
static PyObject *blist_get1(PyBList *self, ssize_t i);
static void blist_forget_children2(PyBList *self, int i, int j);
#define blist_forget_children1(self, i) \
        (blist_forget_children2((self), (i), (self)->num_children))
#define blist_forget_children(self) \
        (blist_forget_children2((self), 0, (self)->num_children))
#define blist_forget_child(self, i) \
        (blist_forget_children2((self), (i), (i)+1))
static void blist_become(PyBList *self, PyBList *other);
static int blist_init_from_seq(PyBList *self, PyObject *b);
static int blist_underflow(PyBList *self, int k);
static PyBList *blist_insert_subtree(PyBList *self, int side,
                                     PyBList *subtree, int depth);
static int blist_overflow_root(PyBList *self, PyObject *overflow);
static PyBList *blist_new(void);
static PyBList *blist_concat_roots(PyBList *left_root, int left_height,
                                   PyBList *right_root, int right_height,
                                   int *pheight);
static PyObject *blist_append(PyBList *self, PyObject *v);
static int blist_ass_slice(PyBList *self, ssize_t ilow, ssize_t ihigh,
                           PyObject *v);


static PyObject *_indexerr = NULL;
void set_index_error(void)
{
        if (_indexerr == NULL) 
                _indexerr = PyString_FromString("list index out of range");
        PyErr_SetObject(PyExc_IndexError, _indexerr);
}

/************************************************************************
 * A forest is an array of BList tree structures.
 */

typedef struct Forest
{
        unsigned num_leafs;
        unsigned num_trees;
        unsigned max_trees;
        PyBList **list;        
} Forest;

static Forest *forest_new(void)
{
        Forest *forest = PyMem_New(Forest, 1);
        if (forest == NULL) return (Forest *) PyErr_NoMemory();
        forest->num_trees = 0;
        forest->num_leafs = 0;
        forest->max_trees = LIMIT; // enough for O(LIMIT**2) items
        forest->list = PyMem_New(PyBList *, forest->max_trees);
        if (forest->list == NULL) {
                PyMem_Free(forest);
                return (Forest *) PyErr_NoMemory();
        }
        return forest;
}

static int forest_append(Forest *forest, PyBList *leaf)
{
        if (!leaf->num_children) {  // Don't bother adding empty leaf nodes
                Py_DECREF(leaf);
                return 0;
        }

        if (forest->num_trees == forest->max_trees) {
                forest->max_trees <<= 1;
                PyBList **list = forest->list;
                PyMem_Resize(list, PyBList *, forest->max_trees);
                if (list == NULL) {
                        PyErr_NoMemory();
                        return -1;
                }
                forest->list = list;
        }

        forest->list[forest->num_trees++] = leaf;
        forest->num_leafs++;

        int power = LIMIT;
        while (forest->num_leafs % power == 0) {
                struct PyBList *parent = blist_new();
                parent->leaf = 0;
                memcpy(parent->children,
                       &forest->list[forest->num_trees - LIMIT],
                       sizeof (PyBList *) * LIMIT);
                parent->num_children = LIMIT;
                forest->num_trees -= LIMIT;
                int x = blist_underflow(parent, LIMIT - 1);
                assert(!x);

                forest->list[forest->num_trees++] = parent;
                power *= LIMIT;
        }
        
        return 0;
}

static void forest_delete(Forest *forest)
{
        int i;
        for (i = 0; i < forest->num_trees; i++)
                Py_DECREF(forest->list[i]);
        PyMem_Free(forest->list);
        PyMem_Free(forest);
}

/* Combine the forest into a final BList */
static PyBList *forest_finish(Forest *forest)
{
        PyBList *out_tree = NULL; // The final BList we are building
        int out_height = 0;       // It's height
        int group_height = 1;     // The height of the next group from forest

        while(forest->num_trees) {
                int n = forest->num_leafs % LIMIT;
                forest->num_leafs /= LIMIT;
                group_height++;

                if (!n) continue;  // No nodes at this height

                /* Merge nodes of the same height into 1 node, and
                 * merge it into our output BList.
                 */
                PyBList *group = blist_new();
                if (group == NULL) {
                        forest_delete(forest);
                        Py_DECREF(group);
                        Py_XDECREF(out_tree);
                        return NULL;
                }
                group->leaf = 0;
                memcpy(group->children,
                       &forest->list[forest->num_trees - n],
                       sizeof (PyBList *) * n);
                group->num_children = n;
                forest->num_trees -= n;
                int adj = blist_underflow(group, n - 1);
                if (out_tree == NULL) {
                        out_tree = group;
                        out_height = group_height - adj;
                } else {
                        out_tree = blist_concat_roots(group, group_height- adj,
                                                      out_tree, out_height,
                                                      &out_height);
                }
        }

        forest_delete(forest);
        
        return out_tree;
}

/************************************************************************
 * Back to BLists proper.
 */

/* Creates a new blist for internal use only */
static PyBList *blist_new(void)
{
        PyBList *self;

        self = PyObject_GC_New(PyBList, &PyBList_Type);
        if (self == NULL)
                return NULL;

        self->leaf = 1; // True
        self->num_children = 0;
        self->n = 0;

        PyObject_GC_Track(self);
        
        return self;
}

/* Creates a blist for user use */
static PyBList *blist_user_new(void)
{
        PyBList *self;

        self = PyObject_GC_New(PyBList, &PyUserBList_Type);
        if (self == NULL)
                return NULL;

        self->leaf = 1; // True
        self->num_children = 0;
        self->n = 0;

        PyObject_GC_Track(self);
        
        return self;
}

static int blist_traverse(PyBList *self, visitproc visit, void *arg)
{
        int i;
        for (i = 0; i < self->num_children; i++)
                Py_VISIT(self->children[i]);
        return 0;
}

static int blist_clear(PyBList *self)
{
        blist_forget_children(self);
        return 0;
}

static PyBList *blist_copy(PyBList *self)
{
        PyBList *copy = blist_new();
        blist_become(copy, self);
        return copy;
}

static int blist_init(PyBList *self, PyObject *args, PyObject *kw)
{
        PyObject *arg = NULL;
        static char *kwlist[] = {"sequence", 0};

        if (!PyArg_ParseTupleAndKeywords(args, kw, "|O:list", kwlist, &arg))
                return -1;

        self->leaf = 1; // True
        self->num_children = 0;
        self->n = 0;

        if (arg == NULL) return 0;

        return blist_init_from_seq(self, arg);
}

#define PyBList_Check(op) ((op)->ob_type == &PyBList_Type || (op)->ob_type == &PyUserBList_Type)

/************************************************************************
 * Useful internal utility functions
 */

static void blist_become(PyBList *self, PyBList *other)
{
        if (self == other)
                return;

        if (!other->leaf) {
                int i;
                for (i = 0; i < other->num_children; i++)
                        Py_INCREF(other->children[i]);
        }

        Py_INCREF(other); // "other" may be one of self's children
        blist_forget_children(self);
        self->n = other->n;
        memcpy(self->children, other->children,
               sizeof(PyObject *) * other->num_children);
        self->num_children = other->num_children;
        self->leaf = other->leaf;
        Py_DECREF(other); 
}

/* We are searching for the child that contains leaf element i.
 *
 * Returns a 3-tuple: (the child object, our index of the child,
 *                     the number of leaf elements before the child)
 */
static void blist_locate(PyBList *self, int i,
                         PyObject **child, int *idx, int *before)
{
        if (self->leaf) {
                *child = self->children[i];
                *idx = *before = i;
                return;
        }

        int so_far = 0;
        int k;
        for (k = 0; k < self->num_children; k++) {
                PyBList *p = (PyBList *) self->children[k];
                if (i < so_far + p->n) {
                        *child = (PyObject *) p;
                        *idx = k;
                        *before = so_far;
                        return;
                }
                so_far += p->n;
        }

        *child = self->children[self->num_children-1];
        *idx = self->num_children-1;
        *before = so_far - ((PyBList *)(*child))->n;
}

/* Find the current height of the tree.
 *
 *      We could keep an extra few bytes in each node rather than
 *      figuring this out dynamically, which would reduce the
 *      asymptotic complexitiy of a few operations.  However, I
 *      suspect it's not worth the extra overhead of updating it all
 *      over the place.
 */
static int blist_get_height(PyBList *self)
{
        if (self->leaf)
                return 1;
        return 1 + blist_get_height((PyBList *)
                                    self->children[self->num_children - 1]);
}

/* Remove links to some of our children, decrementing their refcounts */
static void blist_forget_children2(PyBList *self, int i, int j)
{
        int k;
        if (!self->leaf) {
            for (k = i; k < j; k++)
                    Py_DECREF(self->children[k]);
        }
        int delta = j - i;
        shift_left(self, j, delta);
        self->num_children -= delta;
}

static PyBList *blist_prepare_write(PyBList *self, int pt)
{
        /* We are about to modify the child at index pt.  Prepare it.
         *
         * This function returns the child object.  If the caller has
         * other references to the child, they must be discarded as they
         * may no longer be valid.
         * 
         * If the child's .refcount is 1, we simply return the
         * child object.
         * 
         * If the child's .refcount is greater than 1, we:
         * 
         * - copy the child object
         * - decrement the child's .refcount
         * - replace self.children[pt] with the copy
         * - return the copy
         */

        if (pt < 0)
                pt += self->num_children;
        if (!self->leaf && self->children[pt]->ob_refcnt > 1) {
                PyBList *new_copy = blist_new();
                blist_become(new_copy, (PyBList *) self->children[pt]);
                Py_DECREF(self->children[pt]);
                self->children[pt] = (PyObject *) new_copy;
        }

        return (PyBList *) self->children[pt];
}

static void blist_adjust_n(PyBList *self)
{
        if (self->leaf) {
                self->n = self->num_children;
                return;
        }
        self->n = 0;
        int i;
        for (i = 0; i < self->num_children; i++)
                self->n += ((PyBList *)self->children[i])->n;
}

/* Non-default constructor.  Create a node with specific children.
 *
 * We steal the reference counters from the caller.
 */

static PyBList *blist_new_sibling(PyBList *sibling)
{
        PyBList *self = blist_new();
        assert(sibling->num_children == LIMIT);
        copy(self, 0, sibling, HALF, HALF);
        self->leaf = sibling->leaf;
        self->num_children = HALF;
        sibling->num_children = HALF;
        blist_adjust_n(self);
        return self;
}

/************************************************************************
 * Functions for manipulating the tree
 */

/* Child k has underflowed.  Borrow from k+1 */
static void blist_borrow_right(PyBList *self, int k)
{
        PyBList *p = (PyBList *) self->children[k];
        PyBList *right = blist_prepare_write(self, k+1);
        unsigned total = p->num_children + right->num_children;
        unsigned split = total / 2;

        assert(split >= HALF);
        assert(total-split >= HALF);

        unsigned migrate = split - p->num_children;

        copy(p, p->num_children, right, 0, migrate);
        p->num_children += migrate;
        shift_left(right, migrate, migrate);
        right->num_children -= migrate;
        blist_adjust_n(right);
        blist_adjust_n(p);
}

/* Child k has underflowed.  Borrow from k-1 */
static void blist_borrow_left(PyBList *self, int k)
{
        PyBList *p = (PyBList *) self->children[k];
        PyBList *left = blist_prepare_write(self, k-1);
        unsigned total = p->num_children + left->num_children;
        unsigned split = total / 2;

        assert(split >= HALF);
        assert(total-split >= HALF);

        unsigned migrate = split - p->num_children;

        shift_right(p, 0, migrate);
        copy(p, 0, left, p->num_children - migrate, migrate);
        p->num_children += migrate;
        left->num_children -= migrate;
        blist_adjust_n(left);
        blist_adjust_n(p);
}

/* Child k has underflowed.  Merge with k+1 */
static void blist_merge_right(PyBList *self, int k)
{
        PyBList *p = (PyBList *) self->children[k];
        PyBList *p2 = (PyBList *) self->children[k+1];
        copy(p, p->num_children, p2, 0, p2->num_children);
        int i;
        for (i = 0; i < p2->num_children; i++)
                Py_INCREF(p2->children[i]);
        p->num_children += p2->num_children;
        blist_forget_child(self, k+1);
        blist_adjust_n(p);
}

/* Child k has underflowed.  Merge with k-1 */
static void blist_merge_left(PyBList *self, int k)
{
        PyBList *p = (PyBList *) self->children[k];
        PyBList *p2 = (PyBList *) self->children[k-1];
        shift_right(p, 0, p2->num_children);
        copy(p, 0, p2, 0, p2->num_children);
        int i;
        for (i = 0; i < p2->num_children; i++)
                Py_INCREF(p2->children[i]);
        blist_forget_child(self, k-1);
        blist_adjust_n(p);        
}

/* Concatenate two trees of potentially different heights. */
static PyBList *blist_concat_blist(PyBList *left_subtree, PyBList *right_subtree,
                             int height_diff, int *padj)
{
        /* The parameters are the two trees, and the difference in their
         * heights expressed as left_height - right_height.
         * 
         * Returns a tuple of the new, combined tree, and an integer.
         * The integer expresses the height difference between the new
         * tree and the taller of the left and right subtrees.  It will
         * be 0 if there was no change, and 1 if the new tree is taller
         * by 1.
         */

        assert(left_subtree->ob_refcnt == 1);
        assert(right_subtree->ob_refcnt == 1);

        int adj = 0;
        PyBList *overflow;
        PyBList *root;
        
        if (height_diff == 0) {
                root = blist_new();
                root->children[0] = (PyObject *) left_subtree;
                root->children[1] = (PyObject *) right_subtree;
                root->leaf = 0;
                root->num_children = 2;
                int collapse = blist_underflow(root, 0);
                if (!collapse)
                        collapse = blist_underflow(root, 1);
                if (!collapse)
                        adj = 1;
                overflow = NULL;
        } else if (height_diff > 0) { // Left is larger
                root = left_subtree;
                overflow = blist_insert_subtree(root, -1, right_subtree,
                                                height_diff - 1);
        } else { // Right is larger
                root = right_subtree;
                overflow = blist_insert_subtree(root, 0, left_subtree,
                                                -height_diff - 1);
        }

        adj += -blist_overflow_root(root, (PyObject *) overflow);
        if (padj) *padj = adj;

        return root;
}

/* Concatenate two subtrees of potentially different heights. */
static PyBList *blist_concat_subtrees(PyBList *left_subtree,
                                      int left_depth,
                                      PyBList *right_subtree,
                                      int right_depth,
                                      int *pdepth)
{
        /* Returns a tuple of the new, combined subtree and its depth.
         *
         * Depths are the depth in the parent, not their height.
         */

        int deepest = left_depth > right_depth ?
                left_depth : right_depth;
        PyBList *root = blist_concat_blist(left_subtree, right_subtree,
                                     -(left_depth - right_depth), pdepth);
        if (pdepth) *pdepth = deepest - *pdepth;
        return root;
}
        
/* Concatenate two roots of potentially different heights. */
static PyBList *blist_concat_roots(PyBList *left_root, int left_height,
                                   PyBList *right_root, int right_height,
                                   int *pheight)
{
        /* Returns a tuple of the new, combined root and its height.
         *
         * Heights are the height from the root to its leaf nodes.
         */

        PyBList *root = blist_concat_blist(left_root, right_root,
                                     left_height - right_height, pheight);
        int highest = left_height > right_height ?
                left_height : right_height;

        if (pheight) *pheight = highest + *pheight;
        
        return root;
}

/* Collapse the tree, if possible */
static int blist_collapse(PyBList *self)
{
        if (self->num_children != 1 || self->leaf) {
                blist_adjust_n(self);
                return 0;
        }

        PyBList *p = (PyBList *) self->children[0];
        blist_become(self, p);
        return 1;
}

/* Check if children k-1, k, or k+1 have underflowed.
 *
 * If so, move things around until self is the root of a valid
 * subtree again, possibly requiring collapsing the tree.
 * 
 * Always calls self._adjust_n() (often via self.__collapse()).
 */
static int blist_underflow(PyBList *self, int k)
{
        if (self->leaf) {
                blist_adjust_n(self);
                return 0;
        }

        if (k < self->num_children) {
                PyBList *p = blist_prepare_write(self, k);
                int shrt = HALF - p->num_children;

                while (shrt > 0) {
                        if (k+1 < self->num_children
                            && ((PyBList *)self->children[k+1])->num_children-shrt >= HALF)
                                blist_borrow_right(self, k);
                        else if (k > 0
                                 && (((PyBList *)self->children[k-1])->num_children-shrt
                                     >= HALF))
                                blist_borrow_left(self, k);
                        else if (k+1 < self->num_children)
                                blist_merge_right(self, k);
                        else if (k > 0)
                                blist_merge_left(self, k--);
                        else // No siblings for p
                                return blist_collapse(self);

                        p = blist_prepare_write(self, k);
                        shrt = HALF - p->num_children;
                }
        }

        if (k > 0 && ((PyBList *)self->children[k-1])->num_children < HALF) {
                int collapse = blist_underflow(self, k-1);
                if (collapse) return collapse;
        }
        
        if (k+1 < self->num_children
            && ((PyBList *)self->children[k+1])->num_children < HALF) {
                int collapse = blist_underflow(self, k+1);
                if (collapse) return collapse;
        }

        return blist_collapse(self);
}

/* Handle the case where a user-visible node overflowed */
static int blist_overflow_root(PyBList *self, PyObject *overflow)
{
        if (!overflow) return 0;
        PyBList *child = blist_copy(self);
        blist_forget_children(self);
        self->children[0] = (PyObject *)child;
        self->children[1] = overflow;
        self->num_children = 2;
        self->leaf = 0;
        blist_adjust_n(self);
        return -1;
}

/* Insert 'item', which may be a subtree, at index k. */
PyBList *blist_insert_here(PyBList *self, int k, PyObject *item)
{
        /* Since the subtree may have fewer than half elements, we may
         * need to merge it after insertion.
         * 
         * This function may cause self to overflow.  If it does, it will
         * take the upper half of its children and put them in a new
         * subtree and return the subtree.  The caller is responsible for
         * inserting this new subtree just to the right of self.
         * 
         * Otherwise, it returns None.
         */

        if (k < 0)
                k += self->num_children;

        if (self->num_children < LIMIT) {
                shift_right(self, k, 1);
                self->children[k] = item;
                int collapse = blist_underflow(self, k);
                assert(!collapse);
                blist_adjust_n(self);
                return NULL;
        }

        PyBList *sibling = blist_new_sibling(self);

        if (k < HALF) {
                shift_right(self, k, 1);
                self->children[k] = item;
                int collapse = blist_underflow(self, k);
                assert(!collapse);
        } else {
                shift_right(sibling, k - HALF, 1);
                sibling->children[k - HALF] = item;
                int collapse = blist_underflow(sibling, k);
                assert(!collapse);
                blist_adjust_n(sibling);
        }

        blist_adjust_n(self);
        return sibling;
}

/* Recurse depth layers, then insert subtree on the left or right */
static PyBList *blist_insert_subtree(PyBList *self, int side,
                                     PyBList *subtree, int depth)
{
        /* This function may cause an overflow.
         *    
         * depth == 0 means insert the subtree as a child of self.
         * depth == 1 means insert the subtree as a grandchild, etc.
         */

        assert(side == 0 || side == -1);

        self->n += subtree->n;

        if (depth) {
                PyBList *p = blist_prepare_write(self, side);
                PyBList *overflow = blist_insert_subtree(p, side,
                                                         subtree, depth-1);
                if (!overflow) return NULL;
                subtree = overflow;
        }

        if (side < 0)
                side = self->num_children;

        PyBList *sibling = blist_insert_here(self, side, (PyObject *) subtree);

        return sibling;
}

/* Child at position k is too short by "depth".  Fix it */
int blist_reinsert_subtree(PyBList *self, int k, int depth)
{
        assert(self->children[k]->ob_refcnt == 1);
        PyBList *subtree = (PyBList *) self->children[k];
        shift_left(self, k, 1);
        
        if (self->num_children > k) {
                // Merge right
                PyBList *p = blist_prepare_write(self, k);
                PyBList *overflow = blist_insert_subtree(p, 0,
                                                         subtree, depth-1);
                if (overflow) {
                        shift_right(self, k+1, 1);
                        self->children[k+1] = (PyObject *) overflow;
                }
        } else {
                // Merge left
                PyBList *p = blist_prepare_write(self, k-1);
                PyBList *overflow = blist_insert_subtree(p, -1,
                                                         subtree, depth-1);
                if (overflow) {
                        shift_right(self, k, 1);
                        self->children[k] = (PyObject *) overflow;
                }
        }
        
        return blist_underflow(self, k);
}

/************************************************************************
 * The main insert and deletion operations
 */

/* Recursive to find position i, and insert item just there. */
PyBList *ins1(PyBList *self, int i, PyObject *item)
{
        /* This function may cause an overflow. */

        if (self->leaf)
                return blist_insert_here(self, i, item);

        PyBList *p;
        int k, so_far;
        blist_locate(self, i, (PyObject **) &p, &k, &so_far);

        self->n += 1;
        p = blist_prepare_write(self, k);
        PyBList *overflow = ins1(p, i - so_far, item);

        if (!overflow) return NULL;
        return blist_insert_here(self, k+1, (PyObject *) overflow);
}

static int blist_extend_blist(PyBList *self, PyBList *other)
{
        // Make not-user-visible roots for the subtrees
        PyBList *right = blist_new();
        blist_become(right, other); // XXX ignoring return value
        PyBList *left = blist_copy(self);

        int left_height = blist_get_height(left);
        int right_height = blist_get_height(right);

        PyBList *root = blist_concat_subtrees(left, -left_height,
                                              right, -right_height, NULL);
        blist_become(self, root);
        Py_DECREF(root);
        return 0;
}

static PyObject *blist_extend(PyBList *self, PyObject *other)
{
        int err;
        if (PyBList_Check(other)) {
                err = blist_extend_blist(self, (PyBList *) other);
                goto done;
        }

        PyBList *bother = blist_user_new();
        blist_init_from_seq(bother, other);
        err = blist_extend_blist(self, bother);
        Py_DECREF(bother);
 done:
        if (err < 0)
                return NULL;
        Py_RETURN_NONE;
}

static PyObject *blist_inplace_concat(PyBList *self, PyObject *other)
{
        PyObject *result;

        result = blist_extend(self, other);
        if (result == NULL)
                return result;
        Py_DECREF(result);
        Py_INCREF(self);
        return (PyObject *)self;
}

static PyBList *blist_inplace_repeat(PyBList *self, Py_ssize_t n)
{
        PyBList *tmp = (PyBList *) blist_repeat(self, n);
        blist_become(self, tmp);
        Py_DECREF(tmp);
        return self;
}

static long
blist_nohash(PyObject *self)
{
        PyErr_SetString(PyExc_TypeError, "list objects are unhashable");
        return -1;
}

/* Recursive version of __delslice__ */
int blist_delslice(PyBList *self, int i, int j)
{
        /* This may cause self to collapse.  It returns None if it did
         * not.  If a collapse occured, it returns a positive integer
         * indicating how much shorter this subtree is compared to when
         * _delslice() was entered.
         * 
         * Additionally, this function may cause an underflow.
         */

        if (i == 0 && j >= self->n) {
                // Delete everything.
                blist_forget_children(self);
                self->n = 0;
                return 0;
        }

        if (self->leaf) {
                int delta = j-i;
                shift_left(self, i, delta);
                self->num_children -= delta;
                self->n = self->num_children;
                return 0;
        }

        PyBList *p, *p2;
        int k, k2, so_far, so_far2;

        blist_locate(self, i, (PyObject **) &p, &k, &so_far);
        blist_locate(self, j-1, (PyObject **) &p2, &k2, &so_far2);

        if (k == k2) {
                /* All of the deleted elements are contained under a single
                 * child of this node.  Recurse and check for a short
                 * subtree and/or underflow
                 */

                assert(so_far == so_far2);
                p = blist_prepare_write(self, k);
                int depth = blist_delslice(p, i - so_far, j - so_far);
                if (!depth) 
                        return blist_underflow(self, k);
                return blist_reinsert_subtree(self, k, depth);
        }

        // Deleted elements are in a range of child elements.  There
        // will be:
        // - a left child (k) where we delete some (or all) of its children
        // - a right child (k2) where we delete some (or all) of it children
        // - children in between who are deleted entirely

        // Call _delslice recursively on the left and right
        p = blist_prepare_write(self, k);
        int collapse_left = blist_delslice(p, i - so_far, j - so_far);
        p2 = blist_prepare_write(self, k2);
        int low = i-so_far > 0 ? i-so_far : 0;
        int collapse_right = blist_delslice(p2, low, j - so_far2);

        int deleted_k = 0; // False
        int deleted_k2 = 0; // False

        // Delete [k+1:k2]
        blist_forget_children2(self, k+1, k2);
        k2 = k+1;

        // Delete k1 and k2 if they are empty
        if (!((PyBList *)self->children[k2])->n) {
                Py_DECREF(self->children[k2]);
                shift_left(self, k2+1, 1);
                self->num_children--;
                deleted_k2 = 1; // True
        }
        if (!((PyBList *)self->children[k])->n) {
                Py_DECREF(self->children[k]);
                shift_left(self, k+1, 1);
                deleted_k = 1; // True
        }

        if (deleted_k && deleted_k2) // # No messy subtrees.  Good.
                return blist_collapse(self);

        // The left and right may have collapsed and/or be in an
        // underflow state.  Clean them up.  Work on fixing collapsed
        // trees first, then worry about underflows.

        int depth;
        if (!deleted_k && !deleted_k2 && collapse_left && collapse_right) {
                // Both exist and collapsed.  Merge them into one subtree.
                PyBList *left = (PyBList *) self->children[k];
                PyBList *right = (PyBList *) self->children[k+1];
                shift_left(self, k+1, 1);
                self->num_children--;
                PyBList *subtree = blist_concat_subtrees(left, collapse_left,
                                                         right, collapse_right,
                                                         &depth);
                self->children[k] = (PyObject *) subtree;
        } else if (deleted_k) {
                // Only the right potentially collapsed, point there.
                depth = collapse_right;
                // k already points to the old k2, since k was deleted
        } else if (!deleted_k2 && !collapse_left) {
                // Only the right potentially collapsed, point there.
                k = k + 1;
                depth = collapse_right;
        } else {
                depth = collapse_left;
        }

        // At this point, we have a potentially short subtree at k,
        // with depth "depth".

        if (!depth || self->num_children == 1) {
                // Doesn't need merging, or no siblings to merge with
                return depth + blist_underflow(self, k);
        }

        // We definitely have a short subtree at k, and we have other children
        return blist_reinsert_subtree(self, k, depth);
}

static void blist_delitem(PyBList *self, ssize_t i)
{
        blist_delslice(self, i, i+1);
}

static PyObject *blist_delitem_return(PyBList *self, ssize_t i)
{
        PyObject *rv = blist_get1(self, i);
        blist_delitem(self, i);
        return rv;
}

static int blist_init_from_seq(PyBList *self, PyObject *b)
{
        if (PyBList_Check(b)) {
                // We can copy other BLists in O(1) time :-)
                blist_become(self, (PyBList *) b);
                return 0;
        }
        
        PyObject *it;
        PyObject *(*iternext)(PyObject *);

        it = PyObject_GetIter(b);
        if (it == NULL)
                return -1;
        iternext = *it->ob_type->tp_iternext;

        /* Try common case of len(sequence) <= LIMIT */
        for (self->num_children = 0; self->num_children < LIMIT;
             self->num_children++) {
                PyObject *item = iternext(it);
                if (item == NULL) {
                        if (PyErr_Occurred()) {
                                if (PyErr_ExceptionMatches(PyExc_StopIteration))
                                        PyErr_Clear();
                                else
                                        goto error;
                        }
                        self->n = self->num_children;
                        goto done;
                }

                self->children[self->num_children] = item;
        }

        /* No such luck, build bottom-up instead.  The sequence data
         * so far goes in a leaf node. */

        PyBList *cur = blist_new();
        blist_become(cur, self);
        blist_forget_children(self);

        Forest *forest = forest_new();
        if (forest == NULL) {
                Py_DECREF(cur);
                return -1;
        }
        forest_append(forest, cur);
        cur = blist_new();

        while (1) {
                PyObject *item = iternext(it);
                if (item == NULL) {
                        if (PyErr_Occurred()) {
                                if (PyErr_ExceptionMatches(PyExc_StopIteration))
                                        PyErr_Clear();
                                else 
                                        goto error;
                        }
                        break;
                }

                if (cur->num_children == LIMIT) {
                        cur->n = LIMIT;
                        if (forest_append(forest, cur) < 0) goto error2;
                        cur = blist_new();
                }

                cur->children[cur->num_children++] = item;
        }

        if (cur->num_children) {
                if (forest_append(forest, cur) < 0) goto error2;
                cur->n = cur->num_children;
        } else {
                Py_DECREF(cur);
        }

        PyBList *final = forest_finish(forest);
        blist_become(self, final);
        Py_DECREF(final);
        
 done:
        Py_DECREF(it);
        return 0;

 error2:
        forest_delete(forest);
        Py_DECREF(cur);
 error:
        Py_DECREF(it);
        return -1;
}

/************************************************************************
 * Below here are other user-callable functions built using the above
 * primitives and user functions.
 */

/* Utility function for performing repr() */
static int blist_repr_r(PyBList *self)
{
        int i;
        PyObject *s;
        if (self->leaf) {
                for (i = 0; i < self->num_children; i++) {
                        s = PyObject_Repr(self->children[i]);
                        if (s == NULL)
                                return -1;
                        Py_DECREF(self->children[i]);
                        self->children[i] = s;
                }
        } else {
                for (i = 0; i < self->num_children; i++) {
                        PyBList *child = blist_prepare_write(self, i);
                        int status = blist_repr_r(child);
                        if (status < 0)
                                return status;
                }
        }

        return 0;
}

/* User-visible repr() */
static PyObject *blist_repr(PyBList *self)
{
        /* Basic approach: Clone self in O(1) time, then walk through
         * the clone, changing each element to repr() of the element,
         * in O(n) time.  Finally, enclose it in square brackets and
         * call join.
         */ 
        
        ssize_t i;
        PyBList *pieces = NULL;
        PyObject *result = NULL;
        PyObject *s;

        i = Py_ReprEnter((PyObject *) self);
        if (i) {
                return i > 0 ? PyString_FromString("[...]") : NULL;
        }

        if (self->n == 0) {
                result = PyString_FromString("[]");
                goto Done;
        }

        pieces = blist_copy(self);
        if (pieces == NULL)
                goto Done;

        if (blist_repr_r(pieces) < 0)
                goto Done;

        s = PyString_FromString("[");
        if (s == NULL)
                goto Done;
        ins1(pieces, 0, s);

        s = PyString_FromString("]");
        if (s == NULL)
                goto Done;
        ins1(pieces, self->num_children, s);

        s = PyString_FromString(", ");
        if (s == NULL)
                goto Done;
        printf("calling join\n");
        result = PyUnicode_Join(s, (PyObject *) pieces);
        printf("eek! %p\n", result);
        Py_DECREF(s);
        
 Done:
        Py_XDECREF(pieces);
        Py_ReprLeave((PyObject *) self);
        return result;
}

/* Return a string that shows the internal structure of the BList */
static PyObject *blist_debug(PyBList *self, PyObject *indent)
{
        PyObject *result, *s, *nl_indent;
        PyString_ConcatAndDel(&indent, PyString_FromString("  "));
        Py_INCREF(indent);

        if (!self->leaf) {
                nl_indent = indent;
                Py_INCREF(indent);
                PyString_ConcatAndDel(&nl_indent, PyString_FromString("\n"));
        
                result = PyString_FromFormat("BList(leaf=%d, n=%d, r=%d, ",
                                             self->leaf, self->n,
                                             self->ob_refcnt);
                PyString_Concat(&result, nl_indent);

                int i;
                for (i = 0; i < self->num_children; i++) {
                        s = blist_debug((PyBList *)self->children[i], indent);
                        PyString_Concat(&result, nl_indent);
                        PyString_ConcatAndDel(&result, s);
                }

                PyString_ConcatAndDel(&result, PyString_FromString(")"));
        } else {
                result = PyString_FromFormat("BList(leaf=%d, n=%d, r=%d, ",
                                             self->leaf, self->n,
                                             self->ob_refcnt);
                int i;
                for (i = 0; i < self->num_children; i++) {
                        s = PyObject_Str(self->children[i]);
                        PyString_ConcatAndDel(&result, s);
                }
        }

        return result;
}

static PyObject *blist_get1(PyBList *self, ssize_t i)
{
        if (self->leaf)
                return self->children[i];

        PyBList *p;
        int k, so_far;
        blist_locate(self, i, (PyObject **) &p, &k, &so_far);
        assert(i >= so_far);
        return blist_get1(p, i - so_far);
}

static PyObject *blist_get_item(PyObject *oself, ssize_t i)
{
        PyBList *self = (PyBList *) oself;
        if (!PyBList_Check(oself)) {
                PyErr_BadInternalCall();
                return NULL;
        }

        if (i < 0 || i >= self->n) {
                set_index_error();
                return NULL;
        }
        return blist_get1(self, i);
}

static PyBList *blist_get_slice(PyBList *self, ssize_t start, ssize_t stop)
{
        assert(start >= 0);
        assert(start < self->n);
        assert(stop <= self->n);
        assert(stop >= start);

        if (stop <= start || start >= self->n)
                return blist_user_new();

        PyBList *rv = blist_user_new();

        if (self->leaf) {
                int delta = stop - start;
                copy(rv, 0, self, start, delta);
                rv->num_children = delta;
                rv->n = delta;
                return rv;
        }

        blist_become(rv, self);
        blist_delslice(rv, stop, self->n);
        blist_delslice(rv, 0, start);
        return rv;
}

static PyObject *blist_subscript(PyBList *self, PyObject *item)
{
        if (PyIndex_Check(item)) {
                ssize_t i;
                i = PyNumber_AsSsize_t(item, PyExc_IndexError);
                if (i == -1 && PyErr_Occurred())
                        return NULL;
                if (i < 0)
                        i += self->n;
                return blist_get_item((PyObject *) self, i);
        } else if (PySlice_Check(item)) {
                ssize_t start, stop, step, slicelength, cur, i;
                PyBList* result;
                PyObject* it;

                if (PySlice_GetIndicesEx((PySliceObject*)item, self->n,
                                         &start, &stop,&step,&slicelength)<0) {
                        return NULL;
                }

                if (step == 1)
                        return (PyObject *) blist_get_slice(self, start, stop);

                result = blist_user_new();
                
                if (slicelength <= 0)
                        return (PyObject *) result;

                /* This could be made slightly faster by using forests */
                for (cur = start, i = 0; i < slicelength; cur += step, i++) {
                        it = blist_get1(self, cur);
                        Py_INCREF(it);
                        blist_append(result, it);
                }

                return (PyObject *) result;
        } else {
                PyErr_SetString(PyExc_TypeError,
                                "list indices must be integers");
                return NULL;
        }
}

static PyObject *blist_ass_item_return(PyBList *self, ssize_t i, PyObject *v)
{
        if (self->leaf) {
                Py_INCREF(v);
                PyObject *old_value = self->children[i];
                self->children[i] = v;
                return old_value;
        }

        PyBList *p;
        int k, so_far;
        blist_locate(self, i, (PyObject **) &p, &k, &so_far);
        assert(i >= so_far);
        return blist_ass_item_return(p, i - so_far, v);
}

static int blist_ass_item(PyBList *self, ssize_t i, PyObject *v)
{
        if (i >= self->n || i < 0) {
                set_index_error();
                return -1;
        }

        if (v == NULL)
                return blist_ass_slice(self, i, i+1, v);

        PyObject *old_value = blist_ass_item_return(self, i, v);
        Py_DECREF(old_value);
        return 0;
}

static int blist_ass_subscript(PyBList *self, PyObject *item, PyObject *value)
{
        if (PyIndex_Check(item)) {
                ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);
                if (i == -1 && PyErr_Occurred())
                        return -1;
                if (i < 0)
                        i += self->n;
                return blist_ass_item(self, i, value);
        } else if (PySlice_Check(item)) {
                ssize_t start, stop, step, slicelength;

                if (PySlice_GetIndicesEx((PySliceObject*)item, self->n,
                                         &start, &stop,&step,&slicelength)<0) {
                        return -1;
                }

                if (slicelength <= 0)
                        return 0;

                /* treat L[slice(a,b)] = v _exactly_ like L[a:b] = v */
                if (step == 1)
                        return blist_ass_slice(self, start, stop, value);

                if (value == NULL) {
                        /* Delete back-to-front */
                        PyObject **garbage;
                        int i, cur;

                        if (step > 0) {
                                stop = start + 1;
                                start = stop + step*(slicelength-1)-1;
                                step = -step;
                        }
                        
                        garbage = (PyObject **)
                                PyMem_New(PyObject *, slicelength);
                        if (!garbage) {
                                PyErr_NoMemory();
                                return -1;
                        }
                        
                        for (cur = start, i = 0; cur < stop; cur += step, i++){
                                garbage[i] = blist_get1(self, cur);
                                blist_delitem(self, cur);
                        }
                        
                        for (i = 0; i < slicelength; i++)
                                Py_DECREF(garbage[i]);
                        
                        PyMem_Free(garbage);
                        
                        return 0;
                } else { // assign slice
                        PyObject **garbage, *ins, *seq, **seqitems;
                        ssize_t cur, i;

                        /* protect against a[::-1] = a */
                        if (self == (PyBList *) value)
                                value = (PyObject *)
                                        blist_copy((PyBList *) value);
                        seq = PySequence_Fast(value,
                                "Must assign iterable to extended slice");
                        if (!seq)
                                return -1;

                        if (PySequence_Fast_GET_SIZE(seq) != slicelength) {
                                PyErr_Format(PyExc_ValueError,
                                             "attempt to assign sequence of size %zd to extended slice of size %zd",
                                             PySequence_Fast_GET_SIZE(seq),
                                             slicelength);
                                Py_DECREF(seq);
                                return -1;
                        }

                        if (!slicelength) {
                                Py_DECREF(seq);
                                return 0;
                        }
                        
                        garbage = (PyObject **)
                                PyMem_New(PyObject *, slicelength);
                        if (!garbage) {
                                Py_DECREF(seq);
                                PyErr_NoMemory();
                                return -1;
                        }

                        seqitems = PySequence_Fast_ITEMS(seq);
                        for (cur = start, i = 0; i < slicelength;
                             cur += step, i++) {
                                ins = seqitems[i];
                                Py_INCREF(ins);
                                garbage[i] = blist_ass_item_return(self, cur,
                                                                   ins);
                        }

                        for (i = 0; i < slicelength; i++)
                                Py_DECREF(garbage[i]);

                        PyMem_FREE(garbage);
                        Py_DECREF(seq);

                        return 0;
                }
        } else {
                PyErr_SetString(PyExc_TypeError,
                                "list indices must be integers");
                return -1;
        }
}

static ssize_t blist_length(PyBList *self)
{
        return self->n;
}

static PyObject *blist_richcompare(PyObject *ov, PyObject *ow, int op)
{
        PyBList *v, *w;
        ssize_t cmp;
        iter_t *it1 = NULL, *it2 = NULL;

        if (!PyBList_Check(ov) || !PyBList_Check(ow)) {
                Py_INCREF(Py_NotImplemented);
                return Py_NotImplemented;
        }

        v = (PyBList *) ov;
        w = (PyBList *) ow;

        if (v->n != w->n && (op == Py_EQ || op == Py_NE)) {
                /* Shortcut: if the lengths differe, the lists differ */
                PyObject *res;
                if (op == Py_EQ) {
                false:
                        res = Py_False;
                } else {
                true:
                        res = Py_True;
                }
                if (it1) iter_delete(it1);
                if (it2) iter_delete(it2);
                Py_INCREF(res);
                return res;
        }

        /* Search for the first index where items are different */
        it1 = iter_new(v);
        if (it1 == NULL)
                return NULL;
        it2 = iter_new(w);
        if (it2 == NULL) {
                iter_delete(it1);
                return NULL;
        }

        int v_stopped = 0;
        int w_stopped = 0;

        while (1) {
                PyObject *item1 = iter_next(it1);
                if (item1 == NULL)
                        v_stopped = 1;
                
                PyObject *item2 = iter_next(it2);
                if (item2 == NULL)
                        w_stopped = 1;
                
                if (v_stopped || w_stopped)
                        break;

                cmp = PyObject_RichCompareBool(item1, item2, Py_EQ);
                if (cmp < 0)
                        return NULL;
                if (!cmp) {
                        if (op == Py_EQ) goto false;
                        if (op == Py_NE) goto true;
                        iter_delete(it1);
                        iter_delete(it2);
                        return PyObject_RichCompare(item1, item2, op);
                }
        }

        switch (op) {
        case Py_LT: cmp = v_stopped && !w_stopped; break;
        case Py_LE: cmp = v_stopped; break;
        case Py_EQ: cmp = v_stopped == w_stopped; break;
        case Py_NE: cmp = v_stopped != w_stopped; break;
        case Py_GT: cmp = !v_stopped && w_stopped; break;
        case Py_GE: cmp = w_stopped; break;
        default: goto error; /* cannot happen */
        }

        if (cmp) goto true;
        else goto false;

 error:
        if (it1) iter_delete(it1);
        if (it2) iter_delete(it2);
        return NULL;
}

static int blist_contains(PyBList *self, PyObject *el)
{
        int c;

        iter_t *it = iter_new(self);

        if (it == NULL)
                return -1;

        do {
                PyObject *item = iter_next(it);
                if (item == NULL) {
                        iter_delete(it);
                        return 0;
                }
                c = PyObject_RichCompareBool(el, item, Py_EQ);
        } while (!c);

        iter_delete(it);
        return 1;
}

static int blist_ass_slice(PyBList *self, ssize_t ilow, ssize_t ihigh,
                           PyObject *v)
{
        int net;
        
        if (ilow < 0) ilow = 0;
        else if (ilow > self->n) ilow = self->n;
        if (ihigh < ilow) ihigh = ilow;
        else if (ihigh > self->n) ihigh = self->n;

        PyBList *other = blist_new();
        blist_init_from_seq(other, v); // XXX ignoring return value

        net = other->n - (ihigh - ilow);

        /* Special case small lists */
        if (self->leaf && other->leaf && (self->n + net <= LIMIT))
        {
                if (net >= 0)
                        shift_right(self, ilow, net);
                else
                        shift_left(self, ilow + -net, -net);
                copy(self, ilow, other, 0, other->n);
                Py_DECREF(other);
                other->num_children = 0;
                blist_adjust_n(self);
                return 0;
        }

        PyBList *left = self;
        PyBList *right = blist_copy(self);
        blist_delslice(left, ilow, left->n);
        blist_delslice(right, 0, ihigh);
        blist_extend_blist(left, other); // XXX check return values
        blist_extend_blist(left, right);

        Py_DECREF(other);
        Py_DECREF(right);

        return 0;
}

static PyObject *blist_pop(PyBList *self, PyObject *args)
{
        ssize_t i = -1;
        PyObject *v;

        if (!PyArg_ParseTuple(args, "|n:pop", &i))
                return NULL;

        if (self->n == 0) {
                /* Special-case most common failure cause */
                PyErr_SetString(PyExc_IndexError, "pop from empty list");
                return NULL;
        }
        if (i < 0)
                i += self->n;
        if (i < 0 || i >= self->n) {
                PyErr_SetString(PyExc_IndexError, "pop index out of range");
                return NULL;
        }
        
        v = blist_delitem_return(self, i);

        return v; /* the caller now owns the reference the list had */
}

static PyObject *blist_index(PyBList *self, PyObject *args)
{
        ssize_t i, start=0, stop=self->n;
        PyObject *v;
        int c;
        iter_t *it;
        
        if (!PyArg_ParseTuple(args, "O|O&O&:index", &v,
                              _PyEval_SliceIndex, &start,
                              _PyEval_SliceIndex, &stop))
                return NULL;
        if (start < 0) {
                start += self->n;
                if (start < 0)
                        start = 0;
        }
        if (stop < 0) {
                stop += self->n;
                if (stop < 0)
                        stop = 0;
        }

        it = iter_new2(self, start, stop);
        if (it == NULL)
                return NULL;
        
        for (i = start; i < stop; i++) {
                PyObject *item = iter_next(it);
                if (item == NULL)
                        break;
                
                c = PyObject_RichCompareBool(item, v, Py_EQ);
                if (c > 0) {
                        iter_delete(it);
                        return PyInt_FromSsize_t(i);
                } else if (c < 0) {
                        iter_delete(it);
                        return NULL;
                }
        }

        iter_delete(it);
        PyErr_SetString(PyExc_ValueError, "list.index(x): x not in list");
        return NULL;
}

static PyObject *blist_remove(PyBList *self, PyObject *v)
{
        ssize_t i;

        iter_t *it = iter_new(self);
        if (it == NULL)
                return NULL;
        
        for (i = 0 ;; i++) {
                PyObject *item = iter_next(it);
                if (item == NULL) 
                        break;
                
                int c = PyObject_RichCompareBool(item, v, Py_EQ);
                if (c > 0) {
                        iter_delete(it);
                        return blist_delitem_return(self, i);
                } else if (c < 0) {
                        iter_delete(it);
                        return NULL;
                }
        }

        iter_delete(it);
        PyErr_SetString(PyExc_ValueError, "list.index(x): x not in list");
        return NULL;
}

static PyObject *blist_count(PyBList *self, PyObject *v)
{
        ssize_t count = 0;
        ssize_t i;

        iter_t *it = iter_new(self);
        
        for (i = 0 ;; i++) {
                PyObject *item = iter_next(it);
                if (item == NULL)
                        break;

                int c = PyObject_RichCompareBool(item, v, Py_EQ);
                if (c > 0)
                        count++;
                else if (c < 0) {
                        iter_delete(it);
                        return NULL;
                }
        }

        iter_delete(it);
        return PyInt_FromSsize_t(count);
}

/* Swiped from listobject.c */
/* Reverse a slice of a list in place, from lo up to (exclusive) hi. */
static void
reverse_slice(PyObject **lo, PyObject **hi)
{
        assert(lo && hi);

        --hi;
        while (lo < hi) {
                PyObject *t = *lo;
                *lo = *hi;
                *hi = t;
                ++lo;
                --hi;
        }
}

static PyObject *blist_reverse(PyBList *self)
{
        if (self->n > 1) {
                reverse_slice(self->children,
                              &self->children[self->num_children]);
                if (!self->leaf) {
                        int i;
                        for (i = 0; i < self->num_children; i++) {
                                PyBList *p = blist_prepare_write(self, i);
                                blist_reverse(p);
                        }
                }
        }

        Py_RETURN_NONE;
}

static PyObject *blist_repeat(PyBList *self, ssize_t n)
{
        if (n <= 0)
                return (PyObject *) blist_user_new();

        PyBList *power = blist_user_new();
        PyBList *rv = blist_user_new();

        if (n & 1)
                blist_extend_blist(rv, self);
        int mask = 2;

        while (mask <= n) {
                blist_extend_blist(power, power);
                if (mask & n)
                        blist_extend_blist(rv, power);
                mask <<= 1;
        }

        return (PyObject *) rv;
}

static PyObject *blist_concat(PyBList *self, PyObject *oother)
{
        if (!PyBList_Check(oother)) {
                PyErr_Format(PyExc_TypeError,
                        "can only concatenate list (not \"%.200s\") to list",
                         oother->ob_type->tp_name);
                return NULL;
        }

        PyBList *other = (PyBList *) oother;

        PyBList *rv = blist_copy(self);
        blist_extend_blist(rv, other);
        return (PyObject *) rv;
}

static PyObject *blist_append(PyBList *self, PyObject *v)
{
        if (ins1(self, self->n, v))
                Py_RETURN_NONE;
        return NULL;
}

static PyObject *blist_insert(PyBList *self, PyObject *args)
{
        ssize_t i;
        PyObject *v;
        if (!PyArg_ParseTuple(args, "nO:insert", &i, &v))
                return NULL;
        if (ins1(self, i, v) == 0)
                Py_RETURN_NONE;
        return NULL;
}

static void blist_dealloc(PyBList *self)
{
        PyObject_GC_UnTrack(self);
        blist_forget_children(self);
}

PyDoc_STRVAR(getitem_doc,
"x.__getitem__(y) <==> x[y]");
PyDoc_STRVAR(reversed_doc,
"L.__reversed__() -- return a reverse iterator over the list");
PyDoc_STRVAR(append_doc,
"L.append(object) -- append object to end");
PyDoc_STRVAR(extend_doc,
"L.extend(iterable) -- extend list by appending elements from the iterable");
PyDoc_STRVAR(insert_doc,
"L.insert(index, object) -- insert object before index");
PyDoc_STRVAR(pop_doc,
"L.pop([index]) -> item -- remove and return item at index (default last)");
PyDoc_STRVAR(remove_doc,
"L.remove(value) -- remove first occurrence of value");
PyDoc_STRVAR(index_doc,
"L.index(value, [start, [stop]]) -> integer -- return first index of value");
PyDoc_STRVAR(count_doc,
"L.count(value) -> integer -- return number of occurrences of value");
PyDoc_STRVAR(reverse_doc,
"L.reverse() -- reverse *IN PLACE*");
PyDoc_STRVAR(sort_doc,
"L.sort(cmp=None, key=None, reverse=False) -- stable sort *IN PLACE*;\n\
cmp(x, y) -> -1, 0, 1");

static PyMethodDef blist_methods[] = {
        {"__getitem__", (PyCFunction)blist_subscript, METH_O|METH_COEXIST, getitem_doc},
//        {"__reversed__",(PyCFunction)blist_reversed, METH_NOARGS, reversed_doc},
        {"append",      (PyCFunction)blist_append,  METH_O, append_doc},
        {"insert",      (PyCFunction)blist_insert,  METH_VARARGS, insert_doc},
        {"extend",      (PyCFunction)blist_extend,  METH_O, extend_doc},
        {"pop",         (PyCFunction)blist_pop,     METH_VARARGS, pop_doc},
        {"remove",      (PyCFunction)blist_remove,  METH_O, remove_doc},
        {"index",       (PyCFunction)blist_index,   METH_VARARGS, index_doc},
        {"count",       (PyCFunction)blist_count,   METH_O, count_doc},
        {"reverse",     (PyCFunction)blist_reverse, METH_NOARGS, reverse_doc},
//        {"sort",        (PyCFunction)blist_sort,    METH_VARARGS | METH_KEYWORDS, sort_doc},
        {NULL,          NULL}           /* sentinel */
};

static PySequenceMethods blist_as_sequence = {
        (lenfunc)blist_length,                   /* sq_length */
        (binaryfunc)blist_concat,                /* sq_concat */
        (ssizeargfunc)blist_repeat,              /* sq_repeat */
        (ssizeargfunc)blist_get_item,            /* sq_item */
        (ssizessizeargfunc)blist_get_slice,      /* sq_slice */
        (ssizeobjargproc)blist_ass_item,         /* sq_ass_item */
        (ssizessizeobjargproc)blist_ass_slice,   /* sq_ass_slice */
        (objobjproc)blist_contains,              /* sq_contains */
        (binaryfunc)blist_inplace_concat,        /* sq_inplace_concat */
        (ssizeargfunc)blist_inplace_repeat,      /* sq_inplace_repeat */
};

PyDoc_STRVAR(blist_doc,
"BList() -> new list\n"
"BLst(sequence) -> new list initialized from sequence's items");

static PyMappingMethods blist_as_mapping = {
        (lenfunc)blist_length,
        (binaryfunc)blist_subscript,
        (objobjargproc)blist_ass_subscript
};

PyTypeObject PyBList_Type = {
        PyObject_HEAD_INIT(NULL)
        0,
        "BList",
        sizeof(PyBList),
        0,
        (destructor)blist_dealloc,              /* tp_dealloc */
        0,                                      /* tp_print */
        0,                                      /* tp_getattr */
        0,                                      /* tp_setattr */
        0,                                      /* tp_compare */
        0,                                      /* tp_repr */
        0,                                      /* tp_as_number */
        0,                                      /* tp_as_sequence */
        0,                                      /* tp_as_mapping */
        blist_nohash,                           /* tp_hash */
        0,                                      /* tp_call */
        0,                                      /* tp_str */
        PyObject_GenericGetAttr,                /* tp_getattro */
        0,                                      /* tp_setattro */
        0,                                      /* tp_as_buffer */
        Py_TPFLAGS_HAVE_GC,                     /* tp_flags */
        blist_doc,                              /* tp_doc */
        (traverseproc)blist_traverse,           /* tp_traverse */
        (inquiry)blist_clear,                   /* tp_clear */
        0,                                      /* tp_richcompare */
        0,                                      /* tp_weaklistoffset */
        0,                                      /* tp_iter */
        0,                                      /* tp_iternext */
        0,                                      /* tp_methods */
        0,                                      /* tp_members */
        0,                                      /* tp_getset */
        0,                                      /* tp_base */
        0,                                      /* tp_dict */
        0,                                      /* tp_descr_get */
        0,                                      /* tp_descr_set */
        0,                                      /* tp_dictoffset */
        (initproc)blist_init,                   /* tp_init */
        PyType_GenericAlloc,                    /* tp_alloc */
        PyType_GenericNew,                      /* tp_new */
        PyObject_GC_Del,                        /* tp_free */
};        

PyTypeObject PyUserBList_Type = {
        PyObject_HEAD_INIT(NULL)
        0,
        "UserBList",
        sizeof(PyBList),
        0,
        (destructor)blist_dealloc,              /* tp_dealloc */
        0,                                      /* tp_print */
        0,                                      /* tp_getattr */
        0,                                      /* tp_setattr */
        0,                                      /* tp_compare */
        (reprfunc)blist_repr,                   /* tp_repr */
        0,                                      /* tp_as_number */
        &blist_as_sequence,                     /* tp_as_sequence */
        &blist_as_mapping,                      /* tp_as_mapping */
        blist_nohash,                           /* tp_hash */
        0,                                      /* tp_call */
        0,                                      /* tp_str */
        PyObject_GenericGetAttr,                /* tp_getattro */
        0,                                      /* tp_setattro */
        0,                                      /* tp_as_buffer */
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
                Py_TPFLAGS_BASETYPE,            /* tp_flags */
        blist_doc,                              /* tp_doc */
        (traverseproc)blist_traverse,           /* tp_traverse */
        (inquiry)blist_clear,                   /* tp_clear */
        blist_richcompare,                      /* tp_richcompare */
        0,                                      /* tp_weaklistoffset */
        blist_iter,                             /* tp_iter */
        0,                                      /* tp_iternext */
        blist_methods,                          /* tp_methods */
        0,                                      /* tp_members */
        0,                                      /* tp_getset */
        0,                                      /* tp_base */
        0,                                      /* tp_dict */
        0,                                      /* tp_descr_get */
        0,                                      /* tp_descr_set */
        0,                                      /* tp_dictoffset */
        (initproc)blist_init,                   /* tp_init */
        PyType_GenericAlloc,                    /* tp_alloc */
        PyType_GenericNew,                      /* tp_new */
        PyObject_GC_Del,                        /* tp_free */
};        

/************************************************************************
 * BList iterator
 */

iter_t *iter_new2(PyBList *lst, int start, int stop)
{
        iter_t *iter;

        lst = blist_copy(lst); // Protect against modifications
        
        iter = PyMem_New(iter_t, 1);
        if (iter == NULL) {
                Py_DECREF(lst);
                return NULL;
        }

        iter->blist = lst;
        
        iter->depth = 0;
        iter->stack = PyMem_New(point_t, blist_get_height(lst));

        if (stop < 0)
                stop = 0;
        if (start < 0)
                start = 0;
        iter->remaining = stop - start;
        while (!lst->leaf) {
                PyBList *p;
                int k, so_far;
                blist_locate(lst, start, (PyObject **) &p, &k, &so_far);
                iter->stack[iter->depth].lst = lst;
                iter->stack[iter->depth++].i = k + 1;
                lst = (PyBList *) lst->children[0];
                start -= so_far;
        }

        iter->stack[iter->depth].lst = lst;
        iter->stack[iter->depth++].i = start;

        return iter;
}

PyObject *iter_next(iter_t *iter)
{
        if (!iter->remaining)
                return NULL;
        iter->remaining--;

        PyBList *p = iter->stack[iter->depth-1].lst;
        int i = iter->stack[iter->depth-1].i;

        if (i < p->num_children) {
                iter->stack[iter->depth-1].i++;
                return p->children[i];
        }

        do {
                if (!iter->depth)
                        return NULL;
                p = iter->stack[--iter->depth].lst;
                i = iter->stack[iter->depth].i;
        } while (i >= p->num_children);

        iter->stack[iter->depth].lst = p;
        iter->stack[iter->depth++].i = i;

        while (!p->leaf) {
                p = (PyBList *) p->children[i];
                i = 0;
                iter->stack[iter->depth].lst = p;
                iter->stack[iter->depth++].i = i+1;
        }

        return p->children[i];
}

iter_t *iter_copy(iter_t *iter)
{
        iter_t *rv = PyMem_New(iter_t, 1);
        if (iter == NULL)
                return NULL;
        Py_INCREF(rv->blist);

        int depth = blist_get_height(iter->blist);

        memcpy(rv, iter, sizeof (*iter));
        rv->stack = PyMem_New(point_t, depth);
        memcpy(rv->stack, iter->stack, depth * sizeof(point_t));
        return rv;
}

void iter_delete(iter_t *iter)
{
        Py_DECREF(iter->blist);
        PyMem_Free(iter->stack);
        PyMem_Free(iter);
}

PyTypeObject PyBListIter_Type = {
        PyObject_HEAD_INIT(NULL)
        0,                                      /* ob_size */
        "blistiterator",                        /* tp_name */
        sizeof(blistiterobject),                /* tp_basicsize */
        0,                                      /* tp_itemsize */
        /* methods */
        (destructor)blistiter_dealloc,          /* tp_dealloc */
        0,                                      /* tp_print */
        0,                                      /* tp_getattr */
        0,                                      /* tp_setattr */
        0,                                      /* tp_compare */
        0,                                      /* tp_repr */
        0,                                      /* tp_as_number */
        0,                                      /* tp_as_sequence */
        0,                                      /* tp_as_mapping */
        0,                                      /* tp_hash */
        0,                                      /* tp_call */
        0,                                      /* tp_str */
        PyObject_GenericGetAttr,                /* tp_getattro */
        0,                                      /* tp_setattro */
        0,                                      /* tp_as_buffer */
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,/* tp_flags */
        0,                                      /* tp_doc */
        (traverseproc)blistiter_traverse,       /* tp_traverse */
        0,                                      /* tp_clear */
        0,                                      /* tp_richcompare */
        0,                                      /* tp_weaklistoffset */
        PyObject_SelfIter,                      /* tp_iter */
        (iternextfunc)blistiter_next,           /* tp_iternext */
        0,                                      /* tp_methods */
        0,                                      /* tp_members */
};

static PyObject *blist_iter2(PyBList *seq, int start, int stop)
{
        blistiterobject *it;

        it = PyObject_GC_New(blistiterobject, &PyBListIter_Type);
        if (it == NULL)
                return NULL;
        it->ob_type = &PyType_Type;

        it->iter = iter_new2(seq, start, stop);
        PyObject_GC_Track(it);
        return (PyObject *) it;
}

static PyObject *blist_iter(PyObject *oseq)
{
        PyBList *seq;

        printf("blist_iter\n");
        
        if (!PyBList_Check(oseq)) {
                PyErr_BadInternalCall();
                return NULL;
        }

        seq = (PyBList *) oseq;

        return blist_iter2(seq, 0, seq->n);
}

static void blistiter_dealloc(blistiterobject *it)
{
        PyObject_GC_UnTrack(it);
        iter_delete(it->iter);
        PyObject_GC_Del(it);
}

static int blistiter_traverse(blistiterobject *it, visitproc visit, void *arg)
{
        Py_VISIT(it->iter->blist);
        return 0;
}

static PyObject *blistiter_next(PyObject *oit)
{
        blistiterobject *it = (blistiterobject *) oit;
        PyObject *obj = iter_next(it->iter);
        printf("blistiter_next!\n");
        if (obj == NULL)
                return NULL;
        Py_INCREF(obj);
        return obj;
}

static PyMethodDef module_methods[] = { { NULL } };

PyMODINIT_FUNC
initcblist(void)
{
        PyObject *m;

        PyBList_Type.ob_type = &PyType_Type;
        PyUserBList_Type.ob_type = &PyType_Type;
        PyBListIter_Type.ob_type = &PyType_Type;
        
        Py_INCREF(&PyBList_Type);
        Py_INCREF(&PyUserBList_Type);
        Py_INCREF(&PyBListIter_Type);

        if (PyType_Ready(&PyBListIter_Type) < 0) return;
        if (PyType_Ready(&PyBList_Type) < 0) return;
        if (PyType_Ready(&PyUserBList_Type) < 0) return;

        m = Py_InitModule3("cblist", module_methods, "BList");

        PyModule_AddObject(m, "BList", (PyObject *) &PyUserBList_Type);
}

