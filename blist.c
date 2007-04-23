/* Copyright (C) 2007 Stutzbach Enterprises, LLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */      

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
 *
 * This goes into an infinite loop:
 *   little_list = BList([0])
 *   big_list = little_list * 2**30
 *
 */

#ifndef LIMIT
#define LIMIT (8)       // Maximum size, currently low (for testing purposes)
#endif
#define HALF  (LIMIT/2) // Minimum size

#define MAX_HEIGHT 16   // ceil(log(PY_SSIZE_T_MAX)/log(HALF));
        

#if LIMIT & 1
#error LIMIT must be divisible by 2
#endif

#if LIMIT < 8
#error LIMIT must be at least 8
#endif

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
        int depth;
        PyBList *leaf;
        int i;
        int remaining;
        point_t stack[MAX_HEIGHT];
} iter_t;

typedef struct {
        PyObject_HEAD
        iter_t iter;
} blistiterobject;

/* Empty BList reuse scheme to save calls to malloc and free */
#define MAXFREELISTS 80
static PyBList *free_lists[MAXFREELISTS];
static int num_free_lists = 0;

static PyBList *free_ulists[MAXFREELISTS];
static int num_free_ulists = 0;

static blistiterobject *free_iters[MAXFREELISTS];
static int num_free_iters = 0;

#define PyBList_Check(op) (PyObject_TypeCheck((op), &PyBList_Type) || (PyObject_TypeCheck((op), &PyUserBList_Type)))
#define PyUserBList_Check(op) (PyObject_TypeCheck((op), &PyUserBList_Type))
#define PyBList_CheckExact(op) ((op)->ob_type == &PyBList_Type || (op)->ob_type == &PyUserBList_Type)

#define copy(self, k, other, k2, n) \
    (memcpy(&(self)->children[k], &(other)->children[k2], \
            sizeof(PyObject *) * n))
#define copyref(self, k, other, k2, n) { \
        register PyObject **src = &(other)->children[(k2)]; \
        register PyObject **dst = &(self)->children[(k)];  \
        register PyObject **stop = &src[n]; \
        while (src < stop) { \
                Py_INCREF(*src); \
                *dst++ = *src++; \
        } \
}

#ifdef Py_DEBUG
static void shift_right(PyBList *self, int k, int n)
{
        assert(k >= 0);
        assert(k <= LIMIT);
        assert(n + self->num_children <= LIMIT);
        memmove(&self->children[k+n], &self->children[k],
                sizeof(PyObject *) * (self->num_children - k));
}

static void shift_left(PyBList *self, int k, int n)
{
        assert(k - n >= 0);
        assert(k >= 0);
        assert(k <= LIMIT);
        assert(self->num_children -n >= 0);
        memmove(&self->children[k-n], &self->children[k],
                sizeof(PyObject *) * (self->num_children - k));
}

#else /* !Py_DEBUG */

#if 0
#define shift_right(self, k, n) \
        (memmove(&(self)->children[(k)+(n)], &(self)->children[(k)], \
                sizeof(PyObject *) * ((self)->num_children - (k))))
#define shift_left(self, k, n) \
        (memmove(&(self)->children[(k)-(n)], &(self)->children[(k)], \
                sizeof(PyObject *) * ((self)->num_children - (k))))
#endif

#define shift_right(self, k, n) ({\
        register PyObject **src = &(self)->children[(self)->num_children-1]; \
        register PyObject **dst = &(self)->children[(self)->num_children-1 + (n)]; \
        register PyObject **stop = &(self)->children[(k)]; \
        while (src >= stop) \
                *dst-- = *src--; \
}) 

#define shift_left(self, k, n) ({\
        register PyObject **src = &(self)->children[(k)]; \
        register PyObject **dst = &(self)->children[(k) - (n)]; \
        register PyObject **stop = &(self)->children[(self)->num_children]; \
        while (src < stop) \
                *dst++ = *src++; \
}) 

#endif

#define ITER2(lst, item, start, stop, block) \
        iter_t *_it = NULL; \
        if (lst->leaf) { \
                int _i; \
                for (_i = (start); _i < lst->num_children && _i < (stop); _i++) { \
                        item = lst->children[_i]; \
                        block; \
                } \
        } else { \
                PyBList *_p; \
                _it = iter_new2_stack((lst), (start), (stop)); \
                _p = _it->leaf; \
                while (1) { \
                        if (_it->i < _p->num_children) { \
                                if (_it->remaining == 0) break; \
                                _it->remaining--; \
                                item = _p->children[_it->i++]; \
                        } else { \
                                item = iter_next(_it); \
                                _p = _it->leaf; \
                                if (item == NULL) break; \
                        } \
                        block; \
                } \
                iter_cleanup(_it); \
        }

#define ITER(lst, item, block) \
        iter_t *_it = NULL; \
        if (lst->leaf) { \
                int _i; \
                for (_i = 0; _i < lst->num_children; _i++) { \
                        item = lst->children[_i]; \
                        block; \
                } \
        } else { \
                PyBList *_p; \
                _it = iter_new_stack((lst)); \
                _p = _it->leaf; \
                while (1) { \
                        if (_it->i < _p->num_children) { \
                                item = _p->children[_it->i++]; \
                        } else { \
                                item = iter_next(_it); \
                                _p = _it->leaf; \
                                if (item == NULL) break; \
                        } \
                        block; \
                } \
                iter_cleanup(_it); \
        }
#define ITER_CLEANUP() if (_it) iter_cleanup(_it)

/* Forward declarations */
static void iter_cleanup(iter_t *iter);
static iter_t *iter_init(iter_t *iter, PyBList *lst, int start, int stop);
#define blist_delslice(self, i, j) \
    rvalidate(self, VALID_RW|VALID_PARENT|VALID_COLLAPSE, int, \
        _blist_delslice(self, i, j))
static int _blist_delslice(PyBList *self, int i, int j);
static int blist_ass_item(PyBList *self, ssize_t i, PyObject *v);
static PyObject *iter_next(iter_t *iter);
#define iter_new(lst) (iter_new2((lst), 0, (lst)->n))
#define iter_new2_stack(lst, start, stop) \
        (iter_init(alloca(sizeof (iter_t) \
                   + (blist_get_height((lst))-1) * sizeof (point_t)), \
                   (lst), (start), (stop)))
#define iter_new_stack(lst) (iter_new2_stack((lst), 0, (lst)->n))

static PyObject *blist_iter(PyObject *);
static void blistiter_dealloc(blistiterobject *);
static int blistiter_traverse(blistiterobject *, visitproc, void *);
PyTypeObject PyBList_Type;
PyTypeObject PyUserBList_Type;
PyTypeObject PyBListIter_Type;
static PyObject *blistiter_next(PyObject *);
static PyObject *blist_iter(PyObject *oseq);
static PyObject *blist_repeat(PyBList *self, ssize_t n);
#define blist_get1(self, i) \
    rvalidate(self, VALID_USER, PyObject *, _blist_get1(self, i))
static PyObject *_blist_get1(PyBList *self, ssize_t i);
#define blist_forget_children2(self, i, j) \
    vvalidate(self, VALID_RW, _blist_forget_children2(self, i, j))
static void inline _blist_forget_children2(PyBList *self, int i, int j);
#define blist_forget_children1(self, i) \
        (blist_forget_children2((self), (i), (self)->num_children))
#define blist_forget_children(self) \
        (blist_forget_children2((self), 0, (self)->num_children))
#define blist_forget_child(self, i) \
        (blist_forget_children2((self), (i), (i)+1))
#define blist_become(self, other) vvalidate(self, VALID_RW, _blist_become(self, other))
static void _blist_become(PyBList *self, PyBList *other);
#define blist_init_from_seq(self, b) \
    rvalidate(self, VALID_RW, int, _blist_init_from_seq(self, b))
static int inline _blist_init_from_seq(PyBList *self, PyObject *b);
#define blist_underflow(self, k) \
    rvalidate(self, VALID_RW|VALID_COLLAPSE, int, _blist_underflow(self, k))
static int _blist_underflow(PyBList *self, int k);
#define blist_insert_subtree(self, side, subtree, depth) \
    rvalidate(self, VALID_RW|VALID_OVERFLOW, PyBList *, \
              _blist_insert_subtree(self, side, subtree, depth))
static PyBList *_blist_insert_subtree(PyBList *self, int side,
                                     PyBList *subtree, int depth);
#define blist_overflow_root(self, overflow) \
    rvalidate(self, VALID_RW, int, _blist_overflow_root(self, overflow))
static int _blist_overflow_root(PyBList *self, PyBList *overflow);
static PyBList *blist_new(void);
static PyBList *blist_concat_roots(PyBList *left_root, int left_height,
                                   PyBList *right_root, int right_height,
                                   int *pheight);
static PyObject *blist_append(PyBList *self, PyObject *v);
static int blist_ass_slice(PyBList *self, ssize_t ilow, ssize_t ihigh,
                           PyObject *v);

/************************************************************************
 * Debug functions
 */

#ifdef Py_DEBUG

static void check_invariants(PyBList *self)
{
        if (self->leaf) {
                assert(self->n == self->num_children);
                int i;

                for (i = 0; i < self->num_children; i++) {
                        PyObject *child = self->children[i];
                        assert(child->ob_refcnt > 0);
                }
        } else {
                int i, total = 0;

                assert(self->num_children > 0);

                for (i = 0; i < self->num_children; i++) {
                        assert(PyBList_Check(self->children[i]));
                        PyBList *child = (PyBList *) self->children[i];
                        assert(child != self);
                        total += child->n; 
                        assert(child->num_children <= LIMIT);
                        assert(HALF <= child->num_children);
                        //check_invariants(child);
                }
                assert(self->n == total);
                assert(self->num_children > 1 || self->num_children == 0);

        }

        assert (self->ob_refcnt >= 1 || self->ob_type == &PyUserBList_Type
                || (self->ob_refcnt == 0 && self->num_children == 0));
}

#define VALID_RW 1
#define VALID_PARENT 2
#define VALID_RET 4
#define VALID_USER 8
#define VALID_OVERFLOW 16
#define VALID_COLLAPSE 32

#define vvalidate(self, options, make_call) ({                              \
    PyBList *_self = self; \
    assert(!((options) & (VALID_USER|VALID_OVERFLOW|VALID_COLLAPSE)));        \
    if ((options) & VALID_RW) {                                               \
        assert(_self->ob_refcnt == 1                                         \
           || _self->ob_type == &PyUserBList_Type);                          \
    }                                                                       \
                                                                            \
    make_call;                                                              \
                                                                            \
    if ((options) & VALID_RW) {                                               \
        assert(_self->ob_refcnt == 1                                         \
           || _self->ob_refcnt == 0                                          \
           || _self->ob_type == &PyUserBList_Type);                          \
    }                                                                       \
    if ((options) & (VALID_PARENT))                                           \
        check_invariants(_self);                                             \
})

#define rvalidate(self, options, rtype, make_call) ({                       \
    PyBList *_self = self; \
    rtype _ret;                                                             \
    int _refs;                                                              \
    if ((options) & VALID_RW) {                                               \
        assert(_self->ob_refcnt == 1                                         \
           || _self->ob_type == &PyUserBList_Type);                          \
    }                                                                       \
    if ((options) & VALID_USER) {                                             \
        assert(_self->ob_refcnt >= 1);                                       \
        _refs = _self->ob_refcnt;                                            \
    }                                                                       \
                                                                            \
    _ret = make_call;                                                   \
                                                                            \
    if ((options) & VALID_OVERFLOW)                                           \
        if (_ret) {                                                 \
            assert(PyBList_Check((PyObject *)_ret));                        \
            check_invariants((PyBList *)_ret);                              \
        }                                                                   \
    if ((options) & VALID_COLLAPSE)                                           \
        assert (_ret >= 0);                                                 \
                                                                            \
    if ((options) & VALID_RW) {                                               \
        assert(_self->ob_refcnt == 1                                         \
           || _self->ob_refcnt == 0                                          \
           || _self->ob_type == &PyUserBList_Type);                          \
    }                                                                       \
    if ((options) & (VALID_PARENT|VALID_USER|VALID_OVERFLOW|VALID_COLLAPSE))  \
        check_invariants(_self);                                             \
    if ((options) & VALID_USER)                                               \
        assert(_self->ob_refcnt == _refs);                                   \
    _ret;                                                                   \
})

#else /* !Py_DEBUG */

#define check_invariants(self)
#define rvalidate(self, options, rtype, make_call) make_call
#define vvalidate(self, options, make_call) make_call

#endif




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

static PyBList *
forest_get_leaf(Forest *forest)
{
        PyBList *node = forest->list[--forest->num_trees];
        PyBList **list;
        while (!node->leaf) {
                int i;
                while (forest->num_trees + node->num_children
                       > forest->max_trees) {
                        list = forest->list;
                        forest->max_trees *= 2;
                        PyMem_Resize(list, PyBList*,forest->max_trees);
                        if (list == NULL) {
                                PyErr_NoMemory();
                                return NULL;
                        }
                        forest->list = list;
                }
                        
                for (i = node->num_children - 1; i >= 0; i--)
                        forest->list[forest->num_trees++]
                                = (PyBList *) node->children[i];

                node->num_children = 0;
                Py_DECREF(node);
                node = forest->list[--forest->num_trees];
        }

        return node;
}

#define MAX_FREE_FORESTS 4
static PyBList **forest_saved[MAX_FREE_FORESTS];
static unsigned forest_max_trees[MAX_FREE_FORESTS];
static unsigned num_free_forests = 0;

static inline Forest *forest_init(Forest *forest)
{
        forest->num_trees = 0;
        forest->num_leafs = 0;
        if (num_free_forests) {
                forest->list = forest_saved[--num_free_forests];
                forest->max_trees = forest_max_trees[num_free_forests];
        } else {
                forest->max_trees = LIMIT; // enough for O(LIMIT**2) items
                forest->list = PyMem_New(PyBList *, forest->max_trees);
                if (forest->list == NULL)
                        return (Forest *) PyErr_NoMemory();
        }
        return forest;
}
#define forest_new() (forest_init(alloca(sizeof(Forest))))

static inline int forest_append(Forest *forest, PyBList *leaf)
{
        if (!leaf->num_children) {  // Don't bother adding empty leaf nodes
                Py_DECREF(leaf);
                return 0;
        }

        leaf->n = leaf->num_children;

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

static inline void forest_delete(Forest *forest)
{
        int i;
        for (i = 0; i < forest->num_trees; i++)
                Py_DECREF(forest->list[i]);
        if (num_free_forests < MAX_FREE_FORESTS) {
                forest_saved[num_free_forests] = forest->list;
                forest_max_trees[num_free_forests++] = forest->max_trees;
        } else 
                PyMem_Free(forest->list);
}

/* Combine the forest into a final BList */
static inline PyBList *forest_finish(Forest *forest)
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

        if (num_free_lists) {
                self = free_lists[--num_free_lists];
                _Py_NewReference((PyObject *) self);
        } else {
                self = PyObject_GC_New(PyBList, &PyBList_Type);
                if (self == NULL)
                        return NULL;

                self->leaf = 1; // True
                self->num_children = 0;
                self->n = 0;
        }

        PyObject_GC_Track(self);
        
        return self;
}

/* Creates a blist for user use */
static PyBList *blist_user_new(void)
{
        PyBList *self;

        if (num_free_ulists) {
                self = free_ulists[--num_free_ulists];
                _Py_NewReference((PyObject *) self);
        } else {
                self = PyObject_GC_New(PyBList, &PyUserBList_Type);
                if (self == NULL)
                        return NULL;

                self->leaf = 1; // True
                self->n = 0;
                self->num_children = 0;
        }

        PyObject_GC_Track(self);
        
        return self;
}

static PyObject *blist_user_tp_new(PyTypeObject *subtype, PyObject *args,
                                  PyObject *kwds)
{
        PyBList *self;
        
        if (subtype == &PyUserBList_Type)
                return (PyObject *) blist_user_new();

        self = (PyBList *) subtype->tp_alloc(subtype, 0);
        if (self == NULL)
                return NULL;

        self->leaf = 1;
        
        return (PyObject *) self;
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
        self->n = 0;
        self->leaf = 1;
        return 0;
}

static PyBList *blist_copy(PyBList *self)
{
        PyBList *copy;

        copy = blist_new();
        blist_become(copy, self);
        return copy;
}

static PyBList *blist_user_copy(PyBList *self)
{
        PyBList *copy;

        copy = blist_user_new();
        blist_become(copy, self);
        return copy;
}

static int blist_init(PyBList *self, PyObject *args, PyObject *kw)
{
        PyObject *arg = NULL;
        static char *kwlist[] = {"sequence", 0};

        if (!PyArg_ParseTupleAndKeywords(args, kw, "|O:list", kwlist, &arg))
                return -1;

        if (self->n)
                blist_clear(self); /* XXX: this could add things back! */

        if (arg == NULL) return 0;

        return blist_init_from_seq(self, arg);
}

/************************************************************************
 * Useful internal utility functions
 */

static void _blist_become(PyBList *self, PyBList *other)
{
        assert(self != other);

        int i;
        for (i = 0; i < other->num_children; i++)
                Py_INCREF(other->children[i]);

        Py_INCREF(other); // "other" may be one of self's children
        blist_forget_children(self);
        self->n = other->n;
        copy(self, 0, other, 0, other->num_children);
        self->num_children = other->num_children;
        self->leaf = other->leaf;
        Py_DECREF(other); 
}

/* We are searching for the child that contains leaf element i.
 *
 * Returns a 3-tuple: (the child object, our index of the child,
 *                     the number of leaf elements before the child)
 */
#define blist_locate(self, i, child, idx, before) \
    vvalidate(self, VALID_PARENT, _blist_locate(self, i, child, idx, before))
static void _blist_locate(PyBList *self, int i,
                         PyObject **child, int *idx, int *before)
{
        assert (!self->leaf);

        if (i <= self->n/2) {
                /* Search from the left */
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
        } else {
                /* Search from the right */
                int so_far = self->n;
                int k;
                for (k = self->num_children-1; k >= 0; k--) {
                        PyBList *p = (PyBList *) self->children[k];
                        so_far -= p->n;
                        if (i >= so_far) {
                                *child = (PyObject *) p;
                                *idx = k;
                                *before = so_far;
                                return;
                        }
                }
        }

        /* Just append */
        *child = self->children[self->num_children-1];
        *idx = self->num_children-1;
        *before = self->n - ((PyBList *)(*child))->n;
}

/* Find the current height of the tree.
 *
 *      We could keep an extra few bytes in each node rather than
 *      figuring this out dynamically, which would reduce the
 *      asymptotic complexitiy of a few operations.  However, I
 *      suspect it's not worth the extra overhead of updating it all
 *      over the place.
 */
#define blist_get_height(self) rvalidate(self, VALID_PARENT, int, _blist_get_height(self))
static int _blist_get_height(PyBList *self)
{
        if (self->leaf)
                return 1;
        return 1 + blist_get_height((PyBList *)
                                    self->children[self->num_children - 1]);
}

/* Remove links to some of our children, decrementing their refcounts */
static void _blist_forget_children2(PyBList *self, int i, int j)
{
        int k;
        int delta = j - i;

        for (k = i; k < j; k++)
                Py_DECREF(self->children[k]);
        shift_left(self, j, delta);
        self->num_children -= delta;
}

#define blist_prepare_write(self, pt) \
    rvalidate(self, VALID_RW, PyBList *, _blist_prepare_write(self, pt))
static PyBList *_blist_prepare_write(PyBList *self, int pt)
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

#define blist_adjust_n(self) vvalidate(self, (VALID_PARENT|VALID_RW), _blist_adjust_n(self))
static void _blist_adjust_n(PyBList *self)
{
        if (self->leaf) {
                self->n = self->num_children;
                return;
        }
        self->n = 0;
        int i;
        for (i = 0; i < self->num_children; i++)
                self->n += ((PyBList *)self->children[i])->n;
        check_invariants(self);
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
#define blist_borrow_right(self, k) \
    vvalidate(self, VALID_RW, _blist_borrow_right(self, k))
static void _blist_borrow_right(PyBList *self, int k)
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
#define blist_borrow_left(self, k) \
    vvalidate(self, VALID_RW, _blist_borrow_left(self, k))
static void _blist_borrow_left(PyBList *self, int k)
{
        PyBList *p = (PyBList *) self->children[k];
        PyBList *left = blist_prepare_write(self, k-1);
        unsigned total = p->num_children + left->num_children;
        unsigned split = total / 2;

        assert(split >= HALF);
        assert(total-split >= HALF);

        unsigned migrate = split - p->num_children;

        shift_right(p, 0, migrate);
        copy(p, 0, left, left->num_children - migrate, migrate);
        p->num_children += migrate;
        left->num_children -= migrate;
        blist_adjust_n(left);
        blist_adjust_n(p);
}

/* Child k has underflowed.  Merge with k+1 */
#define blist_merge_right(self, k) \
    vvalidate(self, VALID_RW, _blist_merge_right(self, k))
static void _blist_merge_right(PyBList *self, int k)
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
#define blist_merge_left(self, k) \
    vvalidate(self, VALID_RW, _blist_merge_left(self, k))
static void _blist_merge_left(PyBList *self, int k)
{
        PyBList *p = (PyBList *) self->children[k];
        PyBList *p2 = (PyBList *) self->children[k-1];
        shift_right(p, 0, p2->num_children);
        p->num_children += p2->num_children;
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

        adj += -blist_overflow_root(root, overflow);
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

static PyBList *
blist_concat_unknown_roots(PyBList *left_root, PyBList *right_root)
{
        return blist_concat_roots(left_root, blist_get_height(left_root),
                                  right_root, blist_get_height(right_root),
                                  NULL);
}

/* Collapse the tree, if possible */
#define blist_collapse(self) \
    rvalidate(self, VALID_RW|VALID_COLLAPSE, int, _blist_collapse(self))
static int _blist_collapse(PyBList *self)
{
        if (self->num_children != 1 || self->leaf) {
                blist_adjust_n(self);
                return 0;
        }

        PyBList *p = (PyBList *) self->children[0];
        blist_become(self, p);
        check_invariants(self);
        return 1;
}

/* Check if children k-1, k, or k+1 have underflowed.
 *
 * If so, move things around until self is the root of a valid
 * subtree again, possibly requiring collapsing the tree.
 * 
 * Always calls self._adjust_n() (often via self.__collapse()).
 */
static int _blist_underflow(PyBList *self, int k)
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
                            && ((PyBList *)self->children[k+1])->num_children >= HALF + shrt)
                                blist_borrow_right(self, k);
                        else if (k > 0
                                 && (((PyBList *)self->children[k-1])->num_children
                                     >= HALF + shrt))
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
static int _blist_overflow_root(PyBList *self, PyBList *overflow)
{
        if (!overflow) return 0;
        PyBList *child = blist_copy(self);
        blist_forget_children(self);
        self->children[0] = (PyObject *)child;
        self->children[1] = (PyObject *)overflow;
        self->num_children = 2;
        self->leaf = 0;
        blist_adjust_n(self);
        return -1;
}

/* Insert 'item', which may be a subtree, at index k. */
#define blist_insert_here(self, k, item) \
    rvalidate(self, VALID_RW|VALID_OVERFLOW, PyBList *, \
              _blist_insert_here(self, k, item))
static PyBList *_blist_insert_here(PyBList *self, int k, PyObject *item)
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

        assert(k >= 0);

        if (self->num_children < LIMIT) {
                shift_right(self, k, 1);
                self->num_children++;
                self->children[k] = item;
                int collapse = blist_underflow(self, k);
                assert(!collapse);
                return NULL;
        }

        PyBList *sibling = blist_new_sibling(self);

        if (k < HALF) {
                shift_right(self, k, 1);
                self->num_children++;
                self->children[k] = item;
                int collapse = blist_underflow(self, k);
                assert(!collapse);
        } else {
                shift_right(sibling, k - HALF, 1);
                sibling->num_children++;
                sibling->children[k - HALF] = item;
                int collapse = blist_underflow(sibling, k - HALF);
                assert(!collapse);
                blist_adjust_n(sibling);
        }

        blist_adjust_n(self);
        check_invariants(self);
        return sibling;
}

/* Recurse depth layers, then insert subtree on the left or right */
static PyBList *_blist_insert_subtree(PyBList *self, int side,
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
#define blist_reinsert_subtree(self, k, depth) \
    rvalidate(self, VALID_RW, int, \
             _blist_reinsert_subtree(self, k, depth))
static int _blist_reinsert_subtree(PyBList *self, int k, int depth)
{
        assert(self->children[k]->ob_refcnt == 1);
        PyBList *subtree = (PyBList *) self->children[k];
        shift_left(self, k+1, 1);
        self->num_children--;
        
        if (self->num_children > k) {
                // Merge right
                PyBList *p = blist_prepare_write(self, k);
                PyBList *overflow = blist_insert_subtree(p, 0,
                                                         subtree, depth-1);
                if (overflow) {
                        shift_right(self, k+1, 1);
                        self->num_children++;
                        self->children[k+1] = (PyObject *) overflow;
                }
        } else {
                // Merge left
                PyBList *p = blist_prepare_write(self, k-1);
                PyBList *overflow = blist_insert_subtree(p, -1,
                                                         subtree, depth-1);
                if (overflow) {
                        shift_right(self, k, 1);
                        self->num_children++;
                        self->children[k] = (PyObject *) overflow;
                }
        }
        
        return blist_underflow(self, k);
}

/************************************************************************
 * The main insert and deletion operations
 */

/* Recursive to find position i, and insert item just there. */
#define ins1(self, i, item) \
  rvalidate(self, VALID_RW|VALID_OVERFLOW, PyBList *, _ins1(self, i, item))
static PyBList *_ins1(PyBList *self, int i, PyObject *item)
{
        /* This function may cause an overflow. */

        PyBList *ret;

        if (self->leaf) {
                Py_INCREF(item);

                /* Speed up the common case */
                if (self->num_children < LIMIT) {
                        shift_right(self, i, 1);
                        self->num_children++;
                        self->n++;
                        self->children[i] = item;
                        return NULL;
                }

                return blist_insert_here(self, i, item);
        }

        PyBList *p;
        int k, so_far;
        blist_locate(self, i, (PyObject **) &p, &k, &so_far);

        self->n += 1;
        p = blist_prepare_write(self, k);
        PyBList *overflow = ins1(p, i - so_far, item);

        if (!overflow) ret = NULL;
        else ret = blist_insert_here(self, k+1, (PyObject *) overflow);

        check_invariants(self);
        
        return ret;
}

#define blist_extend_blist(self, other) \
    rvalidate(self, VALID_RW|VALID_USER, int, _blist_extend_blist(self, other))
static int _blist_extend_blist(PyBList *self, PyBList *other)
{
        /* Special case for speed */
        if (self->leaf && other->leaf && self->n + other->n <= LIMIT) {
                copyref(self, self->n, other, 0, other->n);
                self->n += other->n;
                self->num_children = self->n;
                return 0;
        }

        // Make not-user-visible roots for the subtrees
        PyBList *right = blist_copy(other); // XXX ignoring return value
        PyBList *left = blist_copy(self);

        int left_height = blist_get_height(left);
        int right_height = blist_get_height(right);

        PyBList *root = blist_concat_subtrees(left, -left_height,
                                              right, -right_height, NULL);
        blist_become(self, root);
        Py_DECREF(root);
        return 0;
}

/* XXX Missing validation */
static PyObject *blist_extend(PyBList *self, PyObject *other)
{
        int err;
        PyBList *bother = NULL;
        
        if (PyBList_Check(other)) {
                err = blist_extend_blist(self, (PyBList *) other);
                goto done;
        }

        bother = blist_user_new();
        err = blist_init_from_seq(bother, other);
        if (err < 0)
                goto done;
        err = blist_extend_blist(self, bother);

 done:
        Py_XDECREF(bother);
        if (err < 0)
                return NULL;
        Py_RETURN_NONE;
}

/* XXX Missing validation */
static PyObject *blist_inplace_concat(PyBList *self, PyObject *other)
{
        PyObject *result;

        check_invariants(self);
        
        result = blist_extend(self, other);
        if (result == NULL)
                return result;
        Py_DECREF(result);
        Py_INCREF(self);
        return (PyObject *)self;
}

/* XXX Missing validation */
static PyBList *blist_inplace_repeat(PyBList *self, Py_ssize_t n)
{
        PyBList *tmp = (PyBList *) blist_repeat(self, n);
        if (tmp == NULL)
                return NULL;
        blist_become(self, tmp);
        Py_INCREF(self);
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
static int _blist_delslice(PyBList *self, int i, int j)
{
        /* This may cause self to collapse.  It returns 0 if it did
         * not.  If a collapse occured, it returns a positive integer
         * indicating how much shorter this subtree is compared to when
         * _delslice() was entered.
         *
         * As a special exception, it may return 0 if the entire subtree
         * is deleted.
         * 
         * Additionally, this function may cause an underflow.
         */

        check_invariants(self);

        if (j > self->n)
                j = self->n;

        if (i == j)
                return 0;
        
        if (i == 0 && j >= self->n) {
                // Delete everything.
                blist_clear(self);
                return 0;
        }

        if (self->leaf) {
                blist_forget_children2(self, i, j);
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
        int low = i-so_far2 > 0 ? i-so_far2 : 0;
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
                self->num_children--;
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
        Py_INCREF(rv);
        blist_delitem(self, i);
        return rv;
}

static int inline blist_init_from_fast_seq(PyBList *self, PyObject *b)
{
        int i, n = PySequence_Fast_GET_SIZE(b);
        PyObject **dst, **src = PySequence_Fast_ITEMS(b);
        PyObject **stop = &src[n];
        if (n <= LIMIT) {
                dst = self->children;
                while (src < stop) {
                        Py_INCREF(*src);
                        *dst++ = *src++;
                }
                self->num_children = n;
                self->n = n;
                return 0;
        }

        Forest *forest = forest_new();
        if (forest == NULL)
                return -1;
        PyBList *cur = blist_new();
        dst = cur->children;
        i = 0;
        
        while (src < stop) {
                if (i == LIMIT) {
                        cur->num_children = LIMIT;
                        if (forest_append(forest, cur) < 0)
                                goto error3;
                        cur = blist_new();
                        dst = cur->children;
                        i = 0;
                }

                Py_INCREF(*src);
                dst[i++] = *src++;
        }

        if (i) {
                cur->num_children = i;
                if (forest_append(forest, cur) < 0) {
                error3:
                        forest_delete(forest);
                        Py_DECREF(cur);
                        return -1;
                }
        } else {
                Py_DECREF(cur);
        }

        PyBList *final = forest_finish(forest);
        blist_become(self, final);
        Py_DECREF(final);
        return 0;
}

static int _blist_init_from_seq(PyBList *self, PyObject *b)
{
        if (PyBList_Check(b)) {
                // We can copy other BLists in O(1) time :-)
                blist_become(self, (PyBList *) b);
                return 0;
        }

        if (PyList_CheckExact(b) || PyTuple_CheckExact(b))
                return blist_init_from_fast_seq(self, b);
        
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

        PyBList *cur = blist_copy(self);
        blist_clear(self);

        Forest *forest = forest_new();
        if (forest == NULL) {
                Py_DECREF(it);
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
#define blist_repr_r(self) \
    rvalidate(self, VALID_RW, int, _blist_repr_r(self))
static int _blist_repr_r(PyBList *self)
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
/* XXX Missing validation */
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
        PyObject *s, *tmp;

        i = Py_ReprEnter((PyObject *) self);
        if (i) {
                return i > 0 ? PyString_FromString("[...]") : NULL;
        }

        if (self->n == 0) {
                result = PyString_FromString("[]");
                goto Done;
        }

        pieces = blist_user_copy(self);
        if (pieces == NULL)
                goto Done;

        if (blist_repr_r(pieces) < 0)
                goto Done;

        s = PyString_FromString("[");
        if (s == NULL)
                goto Done;
        tmp = blist_get1(pieces, 0);
        PyString_Concat(&s, tmp);
        blist_ass_item(pieces, 0, s);
        Py_DECREF(s);

        s = PyString_FromString("]");
        if (s == NULL)
                goto Done;
        tmp = blist_get1(pieces, pieces->n-1);
        Py_INCREF(tmp);
        PyString_ConcatAndDel(&tmp, s);
        blist_ass_item(pieces, pieces->n-1, tmp);
        Py_DECREF(tmp);

        s = PyString_FromString(", ");
        if (s == NULL)
                goto Done;
        result = PyUnicode_Join(s, (PyObject *) pieces);
        Py_DECREF(s);
        
 Done:
        Py_XDECREF(pieces);
        Py_ReprLeave((PyObject *) self);
        return result;
}

/* Return a string that shows the internal structure of the BList */
static PyObject *blist_debug(PyBList *self, PyObject *indent)
{
        PyObject *result, *s, *nl_indent, *comma, *indent2;

        check_invariants(self);

        comma = PyString_FromString(", ");
        
        if (indent == NULL)
                indent = PyString_FromString("");
        else
                Py_INCREF(indent);

        indent2 = indent;
        Py_INCREF(indent);
        PyString_ConcatAndDel(&indent2, PyString_FromString("  "));

        if (!self->leaf) {
                nl_indent = indent2;
                Py_INCREF(nl_indent);
                PyString_ConcatAndDel(&nl_indent, PyString_FromString("\n"));
        
                result = PyString_FromFormat("BList(leaf=%d, n=%d, r=%d, ",
                                             self->leaf, self->n,
                                             self->ob_refcnt);
                //PyString_Concat(&result, nl_indent);

                int i;
                for (i = 0; i < self->num_children; i++) {
                        s = blist_debug((PyBList *)self->children[i], indent2);
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
                        PyString_Concat(&result, comma);
                }
        }

        s = indent;
        Py_INCREF(s);
        PyString_ConcatAndDel(&s, result);
        result = s;

        Py_DECREF(comma);
        Py_DECREF(indent);
        check_invariants(self);
        return result;
}

static PyObject *_blist_get1(PyBList *self, ssize_t i)
{
        check_invariants(self);
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
        PyObject *ret;
        
        if (i < 0 || i >= self->n) {
                set_index_error();
                return NULL;
        }

        if (self->leaf)
                ret = self->children[i];
        else
                ret = blist_get1(self, i);
        Py_INCREF(ret);
        return ret;
}

/* XXX missing validation */
static PyBList *blist_get_slice(PyBList *self, ssize_t ilow, ssize_t ihigh)
{
        check_invariants(self);

        if (ilow < 0) ilow = 0;
        else if (ilow > self->n) ilow = self->n;
        if (ihigh < ilow) ihigh = ilow;
        else if (ihigh > self->n) ihigh = self->n;

        PyBList *rv = blist_user_new();
        if (rv == NULL)
                return NULL;

        if (ihigh <= ilow || ilow >= self->n)
                return rv;

        if (self->leaf) {
                int delta = ihigh - ilow;
                copyref(rv, 0, self, ilow, delta);
                rv->num_children = delta;
                rv->n = delta;
                return rv;
        }

        blist_become(rv, self);
        blist_delslice(rv, ihigh, self->n);
        blist_delslice(rv, 0, ilow);
        return rv;
}

/* XXX missing validation */
static PyObject *blist_subscript(PyBList *self, PyObject *item)
{
        check_invariants(self);

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
                /* Also, by special-casing small trees */
                for (cur = start, i = 0; i < slicelength; cur += step, i++) {
                        it = blist_get1(self, cur);
                        PyObject *ret = blist_append(result, it);
                        if (ret == NULL) {
                                Py_DECREF(result);
                                return NULL;
                        }
                        Py_DECREF(ret);
                }

                return (PyObject *) result;
        } else {
                PyErr_SetString(PyExc_TypeError,
                                "list indices must be integers");
                return NULL;
        }
}

#define blist_ass_item_return(self, i, v) \
    rvalidate(self, VALID_RW|VALID_USER, PyObject *, \
        _blist_ass_item_return(self, i, v))
static PyObject *_blist_ass_item_return(PyBList *self, ssize_t i, PyObject *v)
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
        p = blist_prepare_write(self, k);
        return blist_ass_item_return(p, i - so_far, v);
}

static int blist_ass_item(PyBList *self, ssize_t i, PyObject *v)
{
        check_invariants(self);

        if (i >= self->n || i < 0) {
                set_index_error();
                return -1;
        }

        if (v == NULL) {
                blist_delitem(self, i);
                return 0;
        }
        
        PyObject *old_value = blist_ass_item_return(self, i, v);
        Py_DECREF(old_value);
        return 0;
}

static int blist_ass_subscript(PyBList *self, PyObject *item, PyObject *value)
{
        check_invariants(self);

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

                /* treat L[slice(a,b)] = v _exactly_ like L[a:b] = v */
                if (step == 1 && ((PySliceObject*)item)->step == Py_None)
                        return blist_ass_slice(self, start, stop, value);

                if (value == NULL) {
                        /* Delete back-to-front */
                        PyObject **garbage;
                        int i, cur;

                        if (slicelength <= 0)
                                return 0;

                        if (step > 0) {
                                stop = start - 1;
                                start = start + step*(slicelength-1);
                                step = -step;
                        }
                        
                        garbage = (PyObject **)
                                PyMem_New(PyObject *, slicelength);
                        if (!garbage) {
                                PyErr_NoMemory();
                                return -1;
                        }
                        
                        for (cur = start, i = 0; i < slicelength;
                             cur += step, i++)
                                garbage[i] = blist_delitem_return(self, cur);
                        
                        for (i = 0; i < slicelength; i++)
                                Py_DECREF(garbage[i]);
                        
                        PyMem_Free(garbage);
                        
                        return 0;
                } else { // assign slice
                        PyObject **garbage, *ins, *seq, **seqitems;
                        ssize_t cur, i;

                        /* XXX if value is a BTree, we could just merge trees
                         * for an asymptotic speed improvement
                         */
                        
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

static PyObject *blist_richcompare_list(PyBList *v, PyListObject *w, int op)
{
        Py_ssize_t i;
        iter_t *it = NULL;
        int cmp;

        if (v->n != w->ob_size && (op == Py_EQ || op == Py_NE)) {
                /* Shortcut: if the lengths differe, the lists differ */
                PyObject *res;
                if (op == Py_EQ) {
                false:
                        res = Py_False;
                } else {
                true:
                        res = Py_True;
                }
                if (it) iter_cleanup(it);
                Py_INCREF(res);
                return res;
        }

        /* Search for the first index where items are different */
        it = iter_new_stack(v);
        if (it == NULL)
                return NULL;
        
        int v_stopped = 0;
        int w_stopped = 0;

        for (i = 0 ;; i++) {
                PyObject *item1 = iter_next(it);
                PyObject *item2;

                if (item1 == NULL)
                        v_stopped = 1;

                if (i == w->ob_size)
                        w_stopped = 1;

                if (v_stopped || w_stopped)
                        break;

                item2 = w->ob_item[i];
                
                cmp = PyObject_RichCompareBool(item1, item2, Py_EQ);
                if (cmp < 0) {
                        goto error;
                } else if (!cmp) {
                        if (op == Py_EQ) goto false;
                        if (op == Py_NE) goto true;
                        iter_cleanup(it);
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
        if (it)
                iter_cleanup(it);
        return NULL;
}

static inline PyObject *blist_richcompare_item(int c, int op, PyObject *item1, PyObject *item2)
{
        if (c < 0)
                return NULL;
        if (!c) {
                if (op == Py_EQ) {
                        Py_INCREF(Py_False);
                        return Py_False;
                }
                if (op == Py_NE) {
                        Py_INCREF(Py_True);
                        return Py_True;
                }
                return PyObject_RichCompare(item1, item2, op);
        }

        /* Impossible to get here */
        assert(0);
        return NULL;
}

#define Py_RETURN_TRUE return Py_INCREF(Py_True), Py_True
#define Py_RETURN_FALSE return Py_INCREF(Py_False), Py_False

static inline PyObject *blist_richcompare_len(PyBList *v, PyBList *w, int op)
{
        /* No more items to compare -- compare sizes */
        switch (op) {
        case Py_LT: if (v->n <  w->n) Py_RETURN_TRUE; else Py_RETURN_FALSE;
        case Py_LE: if (v->n <= w->n) Py_RETURN_TRUE; else Py_RETURN_FALSE;
        case Py_EQ: if (v->n == w->n) Py_RETURN_TRUE; else Py_RETURN_FALSE;
        case Py_NE: if (v->n != w->n) Py_RETURN_TRUE; else Py_RETURN_FALSE;
        case Py_GT: if (v->n >  w->n) Py_RETURN_TRUE; else Py_RETURN_FALSE;
        case Py_GE: if (v->n >= w->n) Py_RETURN_TRUE; else Py_RETURN_FALSE;
        default: return NULL; /* cannot happen */
        }
}

static PyObject *blist_richcompare_slow(PyBList *v, PyBList *w, int op)
{
        /* Search for the first index where items are different */
        PyObject *item1, *item2;
        iter_t *it1, *it2;
        int c;
        PyBList *leaf1, *leaf2;

        it1 = iter_new_stack(v);
        if (it1 == NULL)
                return NULL;
        it2 = iter_new_stack(w);
        if (it2 == NULL) {
                iter_cleanup(it1);
                return NULL;
        }

        leaf1 = it1->leaf;
        leaf2 = it2->leaf;
        do {
                if (it1->i < leaf1->num_children) {
                        item1 = leaf1->children[it1->i++];
                } else {
                        item1 = iter_next(it1);
                        leaf1 = it1->leaf;
                        if (item1 == NULL) {
                        compare_len:
                                iter_cleanup(it1);
                                iter_cleanup(it2);
                                return blist_richcompare_len(v, w, op);
                        }
                }

                if (it2->i < leaf2->num_children) {
                        item2 = leaf2->children[it2->i++];
                } else {
                        item2 = iter_next(it2);
                        leaf2 = it2->leaf;
                        if (item2 == NULL)
                                goto compare_len;
                }

                c = PyObject_RichCompareBool(item1, item2, Py_EQ);
        } while (c >= 1);
        
        iter_cleanup(it1);
        iter_cleanup(it2);
        return blist_richcompare_item(c, op, item1, item2);
}

static inline PyObject *blist_richcompare_blist(PyBList *v, PyBList *w, int op)
{
        int i, c;

        if (v->n != w->n) {
                /* Shortcut: if the lengths differ, the lists differ */
                if (op == Py_EQ) {
                        Py_INCREF(Py_False);
                        return Py_False;
                } else if (op == Py_NE) {
                        Py_INCREF(Py_True);
                        return Py_True;
                }
        }

        if (!v->leaf || !w->leaf)
                return blist_richcompare_slow(v, w, op);
                
        for (i = 0; i < v->num_children && i < w->num_children; i++) {
                c = PyObject_RichCompareBool(v->children[i],
                                             w->children[i],Py_EQ);
                if (c < 1)
                        return blist_richcompare_item(c, op, v->children[i],
                                                      w->children[i]);
        }
        return blist_richcompare_len(v, w, op);
                                     
}

static PyObject *blist_richcompare(PyObject *v, PyObject *w, int op)
{
        if (PyUserBList_Check(v)) {
                if (PyUserBList_Check(w))
                        return blist_richcompare_blist((PyBList *)v,
                                                       (PyBList *)w, op);
                if (PyList_Check(w))
                        return blist_richcompare_list((PyBList*)v,
                                                      (PyListObject*)w, op);
        }

        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
}

static int blist_contains(PyBList *self, PyObject *el)
{
        int c;
        PyObject *item;

        ITER(self, item, {
                c = PyObject_RichCompareBool(el, item, Py_EQ);
                if (c < 0) {
                        ITER_CLEANUP();
                        return -1;
                }
                if (c > 0) {
                        ITER_CLEANUP();
                        return 1;
                }
        });

        return 0;
}

static int blist_ass_slice(PyBList *self, ssize_t ilow, ssize_t ihigh,
                           PyObject *v)
{
        int net;
        PyBList *other;
        
        if (ilow < 0) ilow = 0;
        else if (ilow > self->n) ilow = self->n;
        if (ihigh < ilow) ihigh = ilow;
        else if (ihigh > self->n) ihigh = self->n;

        if (!v) {
                blist_delslice(self, ilow, ihigh);
                return 0;
        }
        
        if (PyBList_Check(v) && (PyObject *) self != v) {
                other = (PyBList *) v;
                Py_INCREF(other);
        } else {
                other = blist_user_new();
                if (v) {
                        int err = blist_init_from_seq(other, v);
                        if (err < 0) {
                                Py_DECREF(other);
                                return -1;
                        }
                }
        }

        net = other->n - (ihigh - ilow);

        /* Special case small lists */
        if (self->leaf && other->leaf && (self->n + net <= LIMIT))
        {
                int i;

                for (i = ilow; i < ihigh; i++)
                        Py_DECREF(self->children[i]);

                if (net >= 0)
                        shift_right(self, ihigh, net);
                else
                        shift_left(self, ihigh, -net);
                self->num_children += net;
                copyref(self, ilow, other, 0, other->n);
                Py_DECREF(other);
                blist_adjust_n(self);
                return 0;
        }

        PyBList *left = self;
        PyBList *right = blist_user_copy(self);
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
        PyObject *item;
        
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

        i = start;
        ITER2(self, item, start, stop, {
                c = PyObject_RichCompareBool(item, v, Py_EQ);
                if (c > 0) {
                        ITER_CLEANUP();
                        return PyInt_FromSsize_t(i);
                } else if (c < 0) {
                        ITER_CLEANUP();
                        return NULL;
                }
                i++;
        })

        PyErr_SetString(PyExc_ValueError, "list.index(x): x not in list");
        return NULL;
}

static PyObject *blist_remove(PyBList *self, PyObject *v)
{
        ssize_t i;
        PyObject *item;

        i = 0;
        ITER(self, item, {
                int c = PyObject_RichCompareBool(item, v, Py_EQ);
                if (c > 0) {
                        ITER_CLEANUP();
                        blist_delitem(self, i);
                        Py_RETURN_NONE;
                } else if (c < 0) {
                        ITER_CLEANUP();
                        return NULL;
                }
                i++;
        })

        PyErr_SetString(PyExc_ValueError, "list.index(x): x not in list");
        return NULL;
}

static PyObject *blist_count(PyBList *self, PyObject *v)
{
        ssize_t count = 0;
        PyObject *item;

        ITER(self, item, {
                int c = PyObject_RichCompareBool(item, v, Py_EQ);
                if (c > 0)
                        count++;
                else if (c < 0) {
                        ITER_CLEANUP();
                        return NULL;
                }
        })

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
        PyObject *ret;
        
        if (self->n > 1) {
                reverse_slice(self->children,
                              &self->children[self->num_children]);
                if (!self->leaf) {
                        int i;
                        for (i = 0; i < self->num_children; i++) {
                                PyBList *p = blist_prepare_write(self, i);
                                ret = blist_reverse(p);
                                if (ret == NULL)
                                        return NULL;
                                Py_DECREF(ret);
                        }
                }
        }

        Py_RETURN_NONE;
}

static void blist_double(PyBList *self)
{
        if (self->num_children > HALF) {
                blist_extend_blist(self, self);
                return;
        }

        copyref(self, self->num_children, self, 0, self->num_children);
        self->num_children *= 2;
        self->n *= 2;
}

static PyObject *blist_repeat(PyBList *self, ssize_t n)
{
        int mask;
        PyBList *power = NULL, *rv, *remainder = NULL;
        int remainder_n = 0;

        if (n <= 0 || self->n == 0)
                return (PyObject *) blist_user_new();

        if (n > (PY_SSIZE_T_MAX / self->n / 2))
                return PyErr_NoMemory();
        
        rv = blist_user_new();
        if (rv == NULL) {
                return NULL;
        }

        if (n == 1) {
                blist_become(rv, self);
                return (PyObject *) rv;
        }

        if (self->num_children <= HALF) {
                int fit, fitn, so_far;
                
                fit = LIMIT / self->num_children;
                if (fit > n) fit = n;
                fitn = fit * self->num_children;
                copyref(rv, 0, self, 0, self->num_children);
                so_far = self->num_children;
                while (so_far*2 < fitn) {
                        copyref(rv, so_far, rv, 0, so_far);
                        so_far *= 2;
                }
                copyref(rv, so_far, rv, 0, (fitn - so_far));
                
                rv->num_children = fitn;
                blist_adjust_n(rv);

                if (fit == n)
                        return (PyObject *) rv;

                remainder_n = n % fit;
                n /= fit;

                if (remainder_n) {
                        remainder_n *= self->num_children;
                        remainder = blist_user_new();
                        if (remainder == NULL)
                                goto error;
                        copyref(remainder, 0, rv, 0, remainder_n);
                        remainder->num_children = remainder_n;
                        blist_adjust_n(remainder);
                }
        }

        if (n == 0) 
                goto do_remainder;
                
        power = rv;
        rv = blist_user_new();
        if (rv == NULL) {
                Py_XDECREF(remainder);
        error:
                Py_DECREF(power);
                return NULL;
        }

        if (n & 1)
                blist_become(rv, power);

        for (mask = 2; mask <= n; mask <<= 1) {
                blist_double(power);
                if (mask & n)
                        blist_extend_blist(rv, power);
        }
        Py_DECREF(power);

 do_remainder:
        
        if (remainder) {
                blist_extend_blist(rv, remainder);
                Py_DECREF(remainder);
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

        PyBList *rv = blist_user_copy(self);
        blist_extend_blist(rv, other);
        return (PyObject *) rv;
}

static PyObject *blist_append(PyBList *self, PyObject *v)
{
        if (self->n == PY_SSIZE_T_MAX) {
                PyErr_SetString(PyExc_OverflowError,
                                "cannot add more objects to list");
                return NULL;
        }
        
        PyBList *overflow = ins1(self, self->n, v);
        if (overflow)
                blist_overflow_root(self, overflow);
        Py_RETURN_NONE;
}

static PyObject *blist_insert(PyBList *self, PyObject *args)
{
        ssize_t i;
        PyObject *v;
        PyBList *overflow;
        
        if (!PyArg_ParseTuple(args, "nO:insert", &i, &v))
                return NULL;

        if (self->n == PY_SSIZE_T_MAX) {
                PyErr_SetString(PyExc_OverflowError,
                                "cannot add more objects to list");
                return NULL;
        }

        if (i < 0) {
                i += self->n;
                if (i < 0)
                        i = 0;
        } else if (i > self->n)
                i = self->n;
        
        /* Speed up the common case */
        if (self->leaf && self->num_children < LIMIT) {
                Py_INCREF(v);

                shift_right(self, i, 1);
                self->num_children++;
                self->n++;
                self->children[i] = v;
                Py_RETURN_NONE;
        }
        
        overflow = ins1(self, i, v);
        if (overflow)
                blist_overflow_root(self, overflow);
        Py_RETURN_NONE;
}

static void blist_dealloc(PyBList *self)
{
        PyObject_GC_UnTrack(self);
        Py_TRASHCAN_SAFE_BEGIN(self)
                assert(self->ob_refcnt == 0);
#ifdef Py_DEBUG
        self->ob_refcnt = 1; // Hack to make validator happy
#endif
        blist_clear(self);
#ifdef Py_DEBUG
        self->ob_refcnt = 0; // Hack to make validator happy
#endif
        if (num_free_lists < MAXFREELISTS
            && (self->ob_type == &PyBList_Type)) {
                free_lists[num_free_lists++] = self;
        } else if (num_free_ulists < MAXFREELISTS
                   && (self->ob_type == &PyUserBList_Type)) {
                free_ulists[num_free_ulists++] = self;
        } else
                self->ob_type->tp_free((PyObject *)self);
        Py_TRASHCAN_SAFE_END(self);
}

/************************************************************************
 * Sorting code
 *
 * Bits and pieces swiped from Python's listobject.c
 ************************************************************************/

/* If COMPARE is NULL, calls PyObject_RichCompareBool with Py_LT, else calls
 * islt.  This avoids a layer of function call in the usual case, and
 * sorting does many comparisons.
 * Returns -1 on error, 1 if x < y, 0 if x >= y.
 */
#define ISLT(X, Y, COMPARE) ((COMPARE) == NULL ?                        \
                             PyObject_RichCompareBool(X, Y, Py_LT) :    \
                             islt(X, Y, COMPARE))

typedef struct {
        PyObject *compare;
        PyObject *keyfunc;
} compare_t;

/* XXX

   Efficiency improvement:
   Keep one PyTuple in compare_t and just change what it points to.
   We can also skip all the INCREF/DECREF stuff then and just borrow
   references
*/

static int islt(PyObject *x, PyObject *y, const compare_t *compare)
{
        PyObject *res;
        PyObject *args;
        Py_ssize_t i;

        if (compare->keyfunc != NULL) {
                x = PyObject_CallFunctionObjArgs(compare->keyfunc, x, NULL);
                if (x == NULL) return -1;
                y = PyObject_CallFunctionObjArgs(compare->keyfunc, y, NULL);
                if (y == NULL) {
                        Py_DECREF(x);
                        return -1;
                }
        } else {
                Py_INCREF(x);
                Py_INCREF(y);
        }

        if (compare->compare == NULL) {
                i = PyObject_RichCompareBool(x, y, Py_LT);
                Py_DECREF(x);
                Py_DECREF(y);
                if (i < 0)
                        return -1;
                return i < 0;
        }
        
        args = PyTuple_New(2);
        if (args == NULL) {
                Py_DECREF(x);
                Py_DECREF(y);
                return -1;
        }

        PyTuple_SET_ITEM(args, 0, x);
        PyTuple_SET_ITEM(args, 1, y);
        res = PyObject_Call(compare->compare, args, NULL);
        Py_DECREF(args);
        if (res == NULL)
                return -1;
        if (!PyInt_CheckExact(res)) {
                PyErr_Format(PyExc_TypeError,
                             "comparison function must return int, not %.200s",
                             res->ob_type->tp_name);
                Py_DECREF(res);
                return -1;
        }
        i = PyInt_AsLong(res);
        Py_DECREF(res);
        return i < 0;
}

#define INSERTION_THRESH 0
#define BINARY_THRESH 10

/* Compare X to Y via "<".  Goto "fail" if the comparison raises an
   error.  Else "k" is set to true iff X<Y, and an "if (k)" block is
   started.  It makes more sense in context <wink>.  X and Y are PyObject*s.
*/
#define IFLT(X, Y) if ((k = ISLT(X, Y, compare)) < 0) goto fail;  \
                   if (k)

#define SWAP(x, y) {PyObject *_tmp = x; x = y; y = _tmp;}
#define TESTSWAP(x, y) IFLT(y, x) SWAP(x, y)

static int
network_sort(PyObject **array, int n, const compare_t *compare)
{
        int k;

        switch(n) {
        case 0:
        case 1:
                return 0;
        case 2:
                TESTSWAP(array[0], array[1]);
                return 0;
        case 3:
                TESTSWAP(array[0], array[1]);
                TESTSWAP(array[0], array[2]);
                TESTSWAP(array[1], array[2]);
                return 0;
        case 4:
                TESTSWAP(array[0], array[1]);
                TESTSWAP(array[2], array[3]);
                TESTSWAP(array[0], array[2]);
                TESTSWAP(array[1], array[3]);
                TESTSWAP(array[1], array[2]);
                return 0;
        case 5:
                TESTSWAP(array[0], array[1]);
                TESTSWAP(array[3], array[4]);
                TESTSWAP(array[0], array[2]);
                TESTSWAP(array[1], array[2]);
                TESTSWAP(array[0], array[3]);
                TESTSWAP(array[2], array[3]);
                TESTSWAP(array[1], array[4]);
                TESTSWAP(array[1], array[2]);
                TESTSWAP(array[3], array[4]);
                return 0;
        default:
                /* Should not be possible */
                assert (0);
                abort();
        }

 fail:
        return -1;
}

static int insertion_sort(PyObject **array, int n, const compare_t *compare)
{
        int i, j;
        PyObject *tmp;
        for (i = 1; i < n; i++) {
                tmp = array[i];
                for (j = i; j >= 1; j--) {
                        int c = ISLT(array[j-1], tmp, compare);
                        if (c < 0) {
                                array[j] = tmp;
                                return -1;
                        }
                        if (c > 0)
                                break;
                        array[j] = array[j-1];
                }
                array[j] = tmp;
        }

        return 0;
}

static int binary_sort(PyObject **array, int n, const compare_t *compare)
{
        int i, j, low, high, mid, c;
        PyObject *tmp;

        for (i = 1; i < n; i++) {
                tmp = array[i];

                c = ISLT(array[i-1], tmp, compare);
                if (c < 0)
                        return -1;
                if (c > 0)
                        continue;
                
                low = 0;
                high = i-1;

                while (low < high) {
                        mid = low + (high - low)/2;
                        c = ISLT(array[mid], tmp, compare);
                        if (c < 0) 
                                return -1;
                        if (c > 0)
                                low = mid+1;
                        else
                                high = mid;
                }

                for (j = i; j >= low; j--)
                        array[j] = array[j-1];

                array[low] = tmp;
        }

        return 0;
}

static inline int
mini_merge(PyObject **array, int middle, int n, const compare_t *compare)
{
        int c, ret = 0;

        PyObject *copy[LIMIT];
        PyObject **left;
        PyObject **right = &array[middle];
        PyObject **rend = &array[n];
        PyObject **lend = &copy[middle];
        PyObject **src;
        PyObject **dst;

        assert (middle <= LIMIT);

        for (left = array; left < right; left++) {
                c = ISLT(*left, *right, compare);
                if (c < 0)
                        return -1;
                if (!c)
                        goto normal;
        }

        return 0;
        
 normal:
        src = left;
        dst = left;
        
        for (left = copy; src < right; left++)
                *left = *src++;

        lend = left;

        *dst++ = *right++;        
        
        for (left = copy; left < lend && right < rend; dst++) {
                c = ISLT(*left, *right, compare);
                if (c < 0) {
                        ret = -1;
                        goto done;
                }
                if (c > 0)
                        *dst = *left++;
                else
                        *dst = *right++;
        }

 done:
        while (left < lend)
                *dst++ = *left++;

        return ret;
}

#define RUN_THRESH 5

static int
gallop_sort(PyObject **array, int n, const compare_t *compare)
{
        int i, j;
        int run_length = 1, run_dir;
        PyObject **runs[n/RUN_THRESH+2];
        int ns[n/RUN_THRESH+2];
        int num_runs = 0;
        PyObject **run = array;
        for (i = 1; i < n; i++) {
                int c = ISLT(array[i-1], array[i], compare);
                if (c < 0)
                        return -1;
                c = !!c; /* Ensure c is 0 or 1 */
                if (run_length == 1)
                        run_dir = c;
                if (c == run_dir)
                        run_length++;
                else if (run_length >= RUN_THRESH) {
                                if (run_dir == 0)
                                        reverse_slice(run, &array[i]);
                                runs[num_runs] = run;
                                ns[num_runs++] = run_length;
                                run = &array[i];
                                run_length = 1;
                } else {
                        int low = run - array;
                        int high = i-1;
                        int mid;
                        PyObject *tmp = array[i];

                        /* XXX: Is this a stable sort? */
                        
                        while (low < high) {
                                mid = low + (high - low)/2;
                                c = ISLT(array[mid], tmp, compare);
                                if (c < 0) 
                                        return -1;
                                if ((!!c) == run_dir)
                                        low = mid+1;
                                else
                                        high = mid;
                        }

                        for (j = i; j >= low; j--)
                                array[j] = array[j-1];

                        array[low] = tmp;

                        run_length++;
                }
        }

        if (run_dir == 0)
                reverse_slice(run, &array[i]);
        runs[num_runs] = run;
        ns[num_runs++] = run_length;

        while(num_runs > 1) {
                for (i = 0; i < num_runs/2; i++) {
                        int total = ns[2*i] + ns[2*i+1];
                        if (0 > mini_merge(runs[2*i], ns[2*i], total,
                                           compare)) {
                                /* XXX validity? */
                                return -1;
                        }

                        runs[i] = runs[2*i];
                        ns[i] = total;
                }

                if (num_runs & 1) {
                        runs[i] = runs[num_runs - 1];
                        ns[i] = ns[num_runs - 1];
                }
                num_runs = (num_runs+1)/2;
        }

        assert(ns[0] == n);

        return 0;
        
}

static int
mini_merge_sort(PyObject **array, int n, const compare_t *compare)
{
        int i, run_size = BINARY_THRESH;
        
        for (i = 0; i < n; i += run_size) {
                int len = run_size;
                if (n - i < len)
                        len = n - i;
                if (binary_sort(&array[i], len, compare) < 0)
                        return -1;
        }

        run_size *= 2;
        while (run_size < n) {
                for (i = 0; i < n; i += run_size) {
                        int len = run_size;
                        if (n - i < len)
                                len = n - i;
                        if (len <= run_size/2)
                                continue;
                        if (mini_merge(&array[i], run_size/2, len, compare) < 0)
                                return -1;
                }
                run_size *= 2;
        }

        return 0;
}

static int
is_default_cmp(PyObject *cmpfunc)
{
        PyCFunctionObject *f;
        if (cmpfunc == NULL || cmpfunc == Py_None)
                return 1;
        if (!PyCFunction_Check(cmpfunc))
                return 0;
        f = (PyCFunctionObject *)cmpfunc;
        if (f->m_self != NULL)
                return 0;
        if (!PyString_Check(f->m_module))
                return 0;
        if (strcmp(PyString_AS_STRING(f->m_module), "__builtin__") != 0)
                return 0;
        if (strcmp(f->m_ml->ml_name, "cmp") != 0)
                return 0;
        return 1;
}

static PyBList *
merge(PyBList *self, PyBList *other, const compare_t *compare)
{
        int c, i, j;

#if 0
        c = ISLT(blist_get1(self, self->n-1), blist_get1(other, 0), compare);
        if (c < 0) {
                /* XXX */
                return NULL;
        }
        if (c > 0) {
                blist_extend_blist(self, other);
                Py_DECREF(other);
                return self;
        }
#endif
        
        Forest *forest1, *forest2, *forest_out;
        PyBList *leaf1, *leaf2, *output;

        forest1 = forest_new();
        forest2 = forest_new();
        forest_out = forest_new();

        /* XXX: Check return values */
        forest_append(forest1, self);
        forest_append(forest2, other);

        leaf1 = forest_get_leaf(forest1);
        leaf2 = forest_get_leaf(forest2);

        i = 0; /* Index into leaf 1 */
        j = 0; /* Index into leaf 2 */

        output = blist_new();

        while ((forest1->num_trees || i < leaf1->num_children)
               && (forest2->num_trees || j < leaf2->num_children)) {

                /* Check if we need to get a new input leaf node */
                if (i == leaf1->num_children) {
                        leaf1->num_children = 0;
                        Py_DECREF(leaf1);
                        leaf1 = forest_get_leaf(forest1);
                        i = 0;
                }

                if (j == leaf2->num_children) {
                        leaf2->num_children = 0;
                        Py_DECREF(leaf2);
                        leaf2 = forest_get_leaf(forest2);
                        j = 0;
                }

                /* Check if we have filled up an output leaf node */
                if (output->n == LIMIT) {
                        forest_append(forest_out, output);
                        output = blist_new();
                }

                /* Figure out which input leaf has the lower element */
                c = ISLT(leaf1->children[i], leaf2->children[j], compare);
                if (c < 0) {
                        /* XXX: Need to return to a sane state here */
                        return NULL;
                }
                if (c > 0) {
                        output->children[output->num_children++]
                                = leaf1->children[i++];
                } else {
                        output->children[output->num_children++]
                                = leaf2->children[j++];
                }

                output->n++;
        }

        /* At this point, we have completely consumed at least one of
         * the lists */

        /* Append our partially-complete output leaf node to the forest */
        forest_append(forest_out, output);

        /* Append a partially-consumed input leaf node, if one exists */
        if (i < leaf1->num_children) {
                shift_left(leaf1, i, i);
                leaf1->num_children -= i;
                forest_append(forest_out, leaf1);
        } else {
                leaf1->num_children = 0;
                Py_DECREF(leaf1);
        }

        if (j < leaf2->num_children) {
                shift_left(leaf2, j, j);
                leaf2->num_children -= j;
                forest_append(forest_out, leaf2);
        } else {
                leaf2->num_children = 0;
                Py_DECREF(leaf2);
        }

        /* Append the rest of whichever input forest still has nodes. */

        PyBList *ret = forest_finish(forest_out);
        while (forest1->num_trees) {
                PyBList *tree = forest1->list[--forest1->num_trees];
                ret = blist_concat_unknown_roots(ret, tree);
        }
        while (forest2->num_trees) {
                PyBList *tree = forest2->list[--forest2->num_trees];
                ret = blist_concat_unknown_roots(ret, tree);
        }
                
        forest_delete(forest1);
        forest_delete(forest2);
                                   
        return ret;
}

static int
sort(PyBList *self, const compare_t *compare)
{
        int i, ret;
        PyBList *s;

        if (self->leaf)
                return gallop_sort(self->children, self->num_children,
                                   compare);

        for (i = 0; i < self->num_children; i++) {
                blist_prepare_write(self, i);
                ret = sort((PyBList *) self->children[i], compare);
                if (ret < 0)
                        return ret;
        }

        while (self->num_children != 1) {
                for (i = 0; i < self->num_children/2; i++) {
                        s = merge((PyBList *) self->children[2*i],
                                  (PyBList *) self->children[2*i+1], compare);
                        if (s == NULL) {
                                /* XXX: we need to return the tree to
                                 * a valid state here */
                                return -1;
                        }

                        self->children[i] = (PyObject *) s;
                }

                if (self->num_children & 1)
                        self->children[i]
                                = self->children[self->num_children - 1];
                self->num_children = (self->num_children+1)/2;
        }

        blist_become(self, (PyBList *) self->children[0]);
        return 0;
}

static PyObject *
blist_sort(PyBList *self, PyObject *args, PyObject *kwds)
{
        static char *kwlist[] = {"cmp", "key", "reverse", 0};
        int reverse = 0;
        compare_t compare = {NULL, NULL};
        int ret;
        PyBList saved;
        PyObject *result = Py_None;

        if (args != NULL) {
                if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OOi:sort",
                                                 kwlist, &compare.compare,
                                                 &compare.keyfunc,
                                                 &reverse))
                        return NULL;
        }
        
        if (is_default_cmp(compare.compare))
                compare.compare = NULL;
        if (compare.keyfunc == Py_None)
                compare.keyfunc = NULL;

        saved.ob_type = &PyUserBList_Type; // Make validator happy
        saved.n = self->n;
        saved.num_children = self->num_children;
        saved.leaf = self->leaf;
        copy(&saved, 0, self, 0, self->num_children);
        self->n = 0;
        self->num_children = 0;
        self->leaf = 1;

        if (compare.compare == NULL && compare.keyfunc == NULL)
                ret = sort(&saved, NULL);
        else
                ret = sort(&saved, &compare);

        if (ret < 0)
                result = NULL;

        if (self->n && saved.n) {
                PyErr_SetString(PyExc_ValueError, "list modified during sort");
                result = NULL;
                blist_clear(self);
                /* XXX: Bug.  the __del__ method on an object could
                   add more items back to the list.  We need to delay
                   these deallocations until the sort is complete to
                   gaurantee we don't leak references */
        }

        assert(!self->n);
        self->n = saved.n;
        self->num_children = saved.num_children;
        self->leaf = saved.leaf;
        copy(self, 0, &saved, 0, saved.num_children);        
        
        Py_XINCREF(result);
        return result;
}


PyDoc_STRVAR(getitem_doc,
             "x.__getitem__(y) <==> x[y]");
#if 0
PyDoc_STRVAR(reversed_doc,
             "L.__reversed__() -- return a reverse iterator over the list");
#endif
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
        {"sort",        (PyCFunction)blist_sort,    METH_VARARGS | METH_KEYWORDS, sort_doc},
        {"debug",       (PyCFunction)blist_debug,   METH_NOARGS, NULL},
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
        //PyType_GenericNew,                      /* tp_new */
        blist_user_tp_new,                      /* tp_new */
        PyObject_GC_Del,                        /* tp_free */
};        

/************************************************************************
 * BList iterator
 */

#if 0
static iter_t *iter_new2(PyBList *lst, int start, int stop)
{
        iter_t *iter;

        iter = (iter_t *) PyMem_Malloc(sizeof (iter_t));
        if (iter == NULL)
                return NULL;
        iter_init(iter, lst, start, stop);
        return iter;
}
#endif

static iter_t *iter_init(iter_t *iter, PyBList *lst, int start, int stop)
{
        iter->depth = 0;

        assert(stop >= 0);
        assert(start >= 0);
        iter->remaining = stop - start;
        while (!lst->leaf) {
                PyBList *p;
                int k, so_far;
                blist_locate(lst, start, (PyObject **) &p, &k, &so_far);
                iter->stack[iter->depth].lst = lst;
                iter->stack[iter->depth++].i = k + 1;
                Py_INCREF(lst);
                lst = (PyBList *) lst->children[0];
                start -= so_far;
        }

        iter->leaf = lst;
        iter->i = start;
        iter->depth++;
        Py_INCREF(lst);

        return iter;
}

static PyObject *iter_next(iter_t *iter)
{
        PyBList *p;
        int i;

        p = iter->leaf;
        if (iter->remaining == 0)
                return NULL;
        
        iter->remaining--;
        if (iter->i < p->num_children) 
                return p->children[iter->i++];

        iter->depth--;
        do {
                Py_DECREF(p);
                if (!iter->depth) {
                        iter->remaining = 0;
                        iter->leaf = NULL;
                        return NULL;
                }
                p = iter->stack[--iter->depth].lst;
                i = iter->stack[iter->depth].i;
        } while (i >= p->num_children);
        
        assert(iter->stack[iter->depth].lst == p);
        iter->stack[iter->depth++].i = i+1;

        while (!p->leaf) {
                p = (PyBList *) p->children[i];
                Py_INCREF(p);
                i = 0;
                iter->stack[iter->depth].lst = p;
                iter->stack[iter->depth++].i = i+1;
        }

        iter->leaf = iter->stack[iter->depth-1].lst;
        iter->i = iter->stack[iter->depth-1].i;

        return p->children[i];
}

static void iter_cleanup(iter_t *iter)
{
        int i;
        for (i = 0; i < iter->depth-1; i++)
                Py_DECREF(iter->stack[i].lst);
        if (iter->depth) {
                Py_DECREF(iter->leaf);
        }
}

#if 0
static void iter_delete(iter_t *iter)
{
        iter_cleanup(iter);
        PyMem_Free(iter);
}
#endif

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

static PyObject *blist_iter(PyObject *oseq)
{
        PyBList *seq;
        blistiterobject *it;

        if (!PyBList_Check(oseq)) {
                PyErr_BadInternalCall();
                return NULL;
        }

        seq = (PyBList *) oseq;

        if (num_free_iters) {
                it = free_iters[--num_free_iters];
                _Py_NewReference((PyObject *) it);
        } else {
                it = PyObject_GC_New(blistiterobject, &PyBListIter_Type);
                if (it == NULL)
                        return NULL;
        }

        if (seq->leaf) {
                /* Speed up common case */
                it->iter.leaf = seq;
                it->iter.i = 0;
                it->iter.depth = 1;
                it->iter.remaining = seq->n;
                Py_INCREF(seq);
        } else 
                iter_init(&it->iter, seq, 0, seq->n);

        PyObject_GC_Track(it);
        return (PyObject *) it;
}

static void blistiter_dealloc(blistiterobject *it)
{
        PyObject_GC_UnTrack(it);
        iter_cleanup(&it->iter);
        if (num_free_iters < MAXFREELISTS
            && (it->ob_type == &PyBListIter_Type))
                free_iters[num_free_iters++] = it;
        else
                PyObject_GC_Del(it);
}

static int blistiter_traverse(blistiterobject *it, visitproc visit, void *arg)
{
        int i;
        for (i = 0; i < it->iter.depth-1; i++)
                Py_VISIT(it->iter.stack[i].lst);
        if (it->iter.depth)
                Py_VISIT(it->iter.leaf);
        return 0;
}

static PyObject *blistiter_next(PyObject *oit)
{
        blistiterobject *it = (blistiterobject *) oit;
        PyObject *obj;
        
        /* Speed up common case */
        PyBList *p;
        p = it->iter.leaf;
        if (it->iter.remaining == 0)
                return NULL;
        if (it->iter.i < p->num_children) {
                it->iter.remaining--;
                obj = p->children[it->iter.i++];
                Py_INCREF(obj);
                return obj;
        }

        obj = iter_next(&it->iter);
        if (obj == NULL)
                return NULL;
        Py_INCREF(obj);
        return obj;
}

static PyMethodDef module_methods[] = { { NULL } };

PyMODINIT_FUNC
initblist(void)
{
        PyObject *m;

        PyBList_Type.ob_type = &PyType_Type;
        PyUserBList_Type.ob_type = &PyType_Type;
        PyBListIter_Type.ob_type = &PyType_Type;
        
        Py_INCREF(&PyBList_Type);
        Py_INCREF(&PyUserBList_Type);
        Py_INCREF(&PyBListIter_Type);

        if (PyType_Ready(&PyUserBList_Type) < 0) return;
        if (PyType_Ready(&PyBList_Type) < 0) return;
        if (PyType_Ready(&PyBListIter_Type) < 0) return;

        m = Py_InitModule3("blist", module_methods, "BList");

        PyModule_AddObject(m, "BList", (PyObject *) &PyUserBList_Type);
}

