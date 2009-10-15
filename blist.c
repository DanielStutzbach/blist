/* Copyright 2007-2009 Stutzbach Enterprises, LLC
 * daniel@stutzbachenterprises.com
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *    3. The name of the author may not be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**********************************************************************
 *                                                                    *
 *        PLEASE READ blist.rst BEFORE MODIFYING THIS CODE            *
 *                                                                    *
 **********************************************************************/


#include <Python.h>

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#define restrict
#endif

#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION < 6
/* Backward compatibility with Python 2.5 */
#define PyUnicode_FromString PyString_FromString
#define Py_REFCNT(ob)           (((PyObject*)(ob))->ob_refcnt)
#define Py_TYPE(ob)             (((PyObject*)(ob))->ob_type)
#define Py_SIZE(ob)             (((PyVarObject*)(ob))->ob_size)
#define PyVarObject_HEAD_INIT(type, size)       \
        PyObject_HEAD_INIT(type) size,
#define PyUnicode_FromFormat PyString_FromFormat

#elif PY_MAJOR_VERSION == 3
/* Backward compatibility with Python 3 */
#define PyInt_FromSsize_t PyLong_FromSsize_t
#define PyInt_CheckExact PyLong_CheckExact
#define PyInt_AsLong PyLong_AsLong
#define PyInt_AsSsize_t PyLong_AsSsize_t
#define PyInt_FromLong PyLong_FromLong
#endif

#ifndef BLIST_IN_PYTHON
#include "blist.h"
#endif

#define BLIST_PYAPI(type) static type

typedef struct {
        PyBList *lst;
        int i;
} point_t;

typedef struct {
        int depth;
        PyBList *leaf;
        int i;
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

#define PyBList_Check(op) (PyObject_TypeCheck((op), &PyBList_Type) || (PyObject_TypeCheck((op), &PyRootBList_Type)))
#define PyRootBList_Check(op) (PyObject_TypeCheck((op), &PyRootBList_Type))
#define PyRootBList_CheckExact(op) (Py_TYPE((op)) == &PyRootBList_Type)
#define PyBList_CheckExact(op) ((op)->ob_type == &PyBList_Type || (op)->ob_type == &PyRootBList_Type)
#define PyBListIter_Check(op) (PyObject_TypeCheck((op), &PyBListIter_Type) || (PyObject_TypeCheck((op), &PyBListReverseIter_Type)))

/************************************************************************
 * Utility functions for copying and moving children.
 */

/* copy n children from index k2 of other to index k of self */
BLIST_LOCAL(void)
copy(PyBList *self, int k, PyBList *other, int k2, int n)
{
        PyObject **restrict src = &other->children[k2];
        PyObject **restrict dst = &self->children[k];
        PyObject **stop = &other->children[k2+n];

        assert(self != other);

        while (src < stop)
                *dst++ = *src++;
}

/* like copy(), but incrementing references */
BLIST_LOCAL(void)
copyref(PyBList *self, int k, PyBList *other, int k2,int n) {
        PyObject **restrict src = &other->children[k2];
        PyObject **restrict dst = &self->children[k];
        PyObject **stop = &src[n];

        while (src < stop) {
                Py_INCREF(*src);
                *dst++ = *src++;
        }
}

/* like copy(), but incrementing references but check for NULL */
BLIST_LOCAL(void)
xcopyref(PyBList *self, int k, PyBList *other,int k2,int n) {
        PyObject **restrict src = &other->children[k2];
        PyObject **restrict dst = &self->children[k];
        PyObject **stop = &src[n];

        while (src < stop) {
                Py_XINCREF(*src);
                *dst++ = *src++;
        }
}

/* Move children starting at k to the right by n */
BLIST_LOCAL(void)
shift_right(PyBList *self, int k, int n)
{
        PyObject **src = &self->children[self->num_children-1];
        PyObject **dst = &self->children[self->num_children-1 + n];
        PyObject **stop = &self->children[k];

        if (self->num_children == 0)
                return;

        assert(k >= 0);
        assert(k <= LIMIT);
        assert(n + self->num_children <= LIMIT);

        while (src >= stop)
                *dst-- = *src--;
}

/* Move children starting at k to the left by n */
BLIST_LOCAL(void)
shift_left(PyBList *self, int k, int n)
{
        PyObject **src = &self->children[k];
        PyObject **dst = &self->children[k - n];
        PyObject **stop = &self->children[self->num_children];

        assert(k - n >= 0);
        assert(k >= 0);
        assert(k <= LIMIT);
        assert(self->num_children -n >= 0);

        while (src < stop)
                *dst++ = *src++;

#ifdef Py_DEBUG
        while (dst < stop)
                *dst++ = NULL;
#endif
}

BLIST_LOCAL(void)
balance_leafs(PyBList *restrict leaf1, PyBList *restrict leaf2)
{
        assert(leaf1->leaf);
        assert(leaf2->leaf);
        if (leaf1->num_children + leaf2->num_children <= LIMIT) {
                copy(leaf1, leaf1->num_children, leaf2, 0,leaf2->num_children);
                leaf1->num_children += leaf2->num_children;
                leaf1->n += leaf2->num_children;
                leaf2->num_children = 0;
                leaf2->n = 0;
        } else if (leaf1->num_children < HALF) {
                int needed = HALF - leaf1->num_children;

                copy(leaf1, leaf1->num_children, leaf2, 0, needed);
                leaf1->num_children += needed;
                leaf1->n += needed;
                shift_left(leaf2, needed, needed);
                leaf2->num_children -= needed;
                leaf2->n -= needed;
        } else if (leaf2->num_children < HALF) {
                int needed = HALF - leaf2->num_children;

                shift_right(leaf2, 0, needed);
                copy(leaf2, 0, leaf1, leaf1->num_children-needed, needed);
                leaf1->num_children -= needed;
                leaf1->n -= needed;
                leaf2->num_children += needed;
                leaf2->n += needed;
        }
}

/************************************************************************
 * Macros for O(1) iteration over a BList via Depth-First-Search.  If
 * the root is also a leaf, it will skip the memory allocation and
 * just use a for loop.
 */

/* Iteration over part of the list */
#define ITER2(lst, item, start, stop, block) {\
        iter_t _it; int _use_iter; \
        if (lst->leaf) { \
                Py_ssize_t _i; _use_iter = 0; \
                for (_i = (start); _i < lst->num_children && _i < (stop); _i++) { \
                        item = lst->children[_i]; \
                        block; \
                } \
        } else { \
                Py_ssize_t _remaining = (stop) - (start);\
                PyBList *_p; _use_iter = 1;\
                iter_init2(&_it, (lst), (start)); \
                _p = _it.leaf; \
                while (_p != NULL && _remaining--) { \
                        if (_it.i < _p->num_children) { \
                                item = _p->children[_it.i++]; \
                        } else { \
                                item = iter_next(&_it); \
                                _p = _it.leaf; \
                                if (item == NULL) break; \
                        } \
                        block; \
                } \
                iter_cleanup(&_it); \
        } \
}

/* Iteration over the whole list */
#define ITER(lst, item, block) {\
        if ((lst)->leaf) { \
                iter_t _it; \
                Py_ssize_t _i; const int _use_iter = 0;\
                for (_i = 0; _i < (lst)->num_children; _i++) { \
                        item = (lst)->children[_i]; \
                        block; \
                } ITER_CLEANUP(); \
        } else { \
                iter_t _it; \
                PyBList *_p; \
                const int _use_iter = 1; \
                iter_init(&_it, (lst)); \
                _p = _it.leaf; \
                while (_p) { \
                        if (_it.i < _p->num_children) { \
                                item = _p->children[_it.i++]; \
                        } else { \
                                item = iter_next(&_it); \
                                _p = _it.leaf; \
                                if (item == NULL) break; \
                        } \
                        block; \
                } \
                ITER_CLEANUP(); \
        } \
}

/* Call this before when leaving the ITER via return or goto. */
#define ITER_CLEANUP() if (_use_iter) iter_cleanup(&_it)

/* Forward declarations */
PyTypeObject PyBList_Type;
PyTypeObject PyRootBList_Type;
PyTypeObject PyBListIter_Type;
PyTypeObject PyBListReverseIter_Type;
static void ext_init(PyBListRoot *root);
static void ext_mark(PyBList *broot, Py_ssize_t offset, int value);
static void ext_mark_set_dirty(PyBList *broot, Py_ssize_t i, Py_ssize_t j);
static void ext_mark_set_dirty_all(PyBList *broot);
#define CLEAN (-1) /* also hard-coded in blist.h */
#define DIRTY (-2)

static PyObject *_indexerr = NULL;
void set_index_error(void)
{
        if (_indexerr == NULL)
                _indexerr = PyUnicode_FromString("list index out of range");
        PyErr_SetObject(PyExc_IndexError, _indexerr);
}

/************************************************************************
 * Debugging forward declarations
 */

#ifdef Py_DEBUG

static int blist_unstable = 0;
static int blist_in_code = 0;
static int blist_danger = 0;
#define DANGER_BEGIN { int _blist_unstable = blist_unstable, _blist_in_code = blist_in_code; blist_unstable = 0; blist_in_code = 0; blist_danger++
#define DANGER_END assert(!blist_unstable); assert(!blist_in_code); blist_unstable = _blist_unstable; blist_in_code = _blist_in_code; assert(blist_danger); blist_danger--; } while (0)

#else

#define DANGER_BEGIN while(0)
#define DANGER_END while(0)

#endif

/************************************************************************
 * Utility functions for removal items from a BList
 *
 * Objects in Python can execute arbitrary code when garbage
 * collected, which means they may make calls that modify the BList
 * that we're deleting items from.  Yuck.
 *
 * To avoid this in the general case, any function that removes items
 * from a BList calls decref_later() on the object instead of
 * Py_DECREF().  The objects are accumulated in a global list of
 * objects pending for deletion.  Just before the function returns to
 * the interpreter, decref_flush() is called to actually decrement the
 * reference counters.
 *
 * decref_later() can be passed PyBList objects to delete whole
 * subtrees.
 */

static PyObject **decref_list = NULL;
static Py_ssize_t decref_max = 0;
static Py_ssize_t decref_num = 0;

#define DECREF_BASE (2*128)

int decref_init(void)
{
        decref_max = DECREF_BASE;
        decref_list = (PyObject **) PyMem_New(PyObject *, decref_max);
        if (decref_list == NULL)
                return -1;
        return 0;
}

static void _decref_later(PyObject *ob)
{
        if (decref_num == decref_max) {
                PyObject **tmp = decref_list;
                decref_max *= 2;

                PyMem_Resize(decref_list, PyObject *, decref_max);
                if (decref_list == NULL) {
                        PyErr_NoMemory();
                        decref_list = tmp;
                        decref_max /= 2;
                        return;
                }
        }

        decref_list[decref_num++] = ob;
}
#define decref_later(ob) do { if (Py_REFCNT((ob)) > 1) { Py_DECREF((ob)); } else { _decref_later((ob)); } } while (0)

static void xdecref_later(PyObject *ob)
{
        if (ob == NULL)
                return;

        decref_later(ob);
}

/* Like shift_left(), adding overwritten entries to the decref_later list */
static void shift_left_decref(PyBList *self, int k, int n)
{
        register PyObject **src = &self->children[k];
        register PyObject **dst = &self->children[k - n];
        register PyObject **stop = &self->children[self->num_children];
        register PyObject **dec;
        register PyObject **dst_stop = &self->children[k];

        if (decref_num + n > decref_max) {
                while (decref_num + n > decref_max)
                        decref_max *= 2;
                /* XXX Out of memory not handled */
                PyMem_Resize(decref_list, PyObject *, decref_max);
        }

        dec = &decref_list[decref_num];

        assert(n >= 0);
        assert(k - n >= 0);
        assert(k >= 0);
        assert(k <= LIMIT);
        assert(self->num_children - n >= 0);

        while (src < stop && dst < dst_stop) {
                if (*dst != NULL) {
                        if (Py_REFCNT(*dst) > 1) {
                                Py_DECREF(*dst);
                        } else {
                                *dec++ = *dst;
                        }
                }
                *dst++ = *src++;
        }

        while (src < stop) {
                *dst++ = *src++;
        }

        while (dst < dst_stop) {
                if (*dst != NULL) {
                        if (Py_REFCNT(*dst) > 1) {
                                Py_DECREF(*dst);
                        } else {
                                *dec++ = *dst;
                        }
                }
                dst++;
        }

#ifdef Py_DEBUG
        src = &self->children[self->num_children - n];
        while (src < stop)
                *src++ = NULL;
#endif

        decref_num += dec - &decref_list[decref_num];
}

static void _decref_flush(void)
{
        while (decref_num) {
                /* Py_DECREF() can cause arbitrary other oerations on
                 * BList, potentially even resulting in additional
                 * calls to decref_later() and decref_flush()!
                 *
                 * Any changes to this function must be made VERY
                 * CAREFULLY to handle this case.
                 *
                 * Invariant: whenever we call Py_DECREF, the
                 * decref_list is in a coherent state.  It contains
                 * only items that still need to be decrefed.
                 * Furthermore, we can't cache anything about the
                 * global variables.
                 */

                decref_num--;
                DANGER_BEGIN;
                Py_DECREF(decref_list[decref_num]);
                DANGER_END;
        }

        if (decref_max > DECREF_BASE) {
                /* Return memory to the system now.
                 * We may never get another chance
                 */

                decref_max = DECREF_BASE;
                PyMem_Resize(decref_list, PyObject *, decref_max);
        }
}

/* Redefined in debug mode */
#define decref_flush() (_decref_flush())
#define SAFE_DECREF(x) Py_DECREF((x))
#define SAFE_XDECREF(x) Py_XDECREF((x))

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
                        if (child != NULL)
                                assert(Py_REFCNT(child) > 0);
                }
        } else {
                int i;
                Py_ssize_t total = 0;

                assert(self->num_children > 0);

                for (i = 0; i < self->num_children; i++) {
                        assert(PyBList_Check(self->children[i]));
                        assert(!PyRootBList_Check(self->children[i]));

                        PyBList *child = (PyBList *) self->children[i];
                        assert(child != self);
                        total += child->n;
                        assert(child->num_children <= LIMIT);
                        assert(HALF <= child->num_children);
                        /* check_invariants(child); */
                }
                assert(self->n == total);
                assert(self->num_children > 1 || self->num_children == 0);

        }

        assert (Py_REFCNT(self) >= 1 || Py_TYPE(self) == &PyRootBList_Type
                || (Py_REFCNT(self) == 0 && self->num_children == 0));
}

#define VALID_RW 1
#define VALID_PARENT 2
#define VALID_USER 4
#define VALID_OVERFLOW 8
#define VALID_COLLAPSE 16
#define VALID_DECREF 32
#define VALID_ROOT 64

typedef struct
{
        PyBList *self;
        int options;
} debug_t;

static void debug_setup(debug_t *debug)
{
        Py_INCREF(Py_None);
        blist_in_code++;

        assert(PyBList_Check(debug->self));

        if (debug->options & VALID_DECREF) {
                assert(blist_in_code == 1);
                assert(debug->options & VALID_USER);
        }

#if 0
        /* Comment this test out since users can get references via
         * the gc module */
        if (debug->options & VALID_RW) {
                assert(Py_REFCNT(debug->self) == 1
                       || PyRootBList_Check(debug->self));
        }
#endif

        if (debug->options & VALID_USER) {
                debug->options |= VALID_ROOT;
                assert(PyRootBList_Check(debug->self));
                if (!debug->self->leaf)
                        assert(((PyBListRoot *)debug->self)->last_n
                               == debug->self->n);
                assert(((PyBListRoot *)debug->self)->dirty_length
                       || ((PyBListRoot *)debug->self)->dirty_root);

                if (!blist_danger)
                        assert(decref_num == 0);
        }
        
        if (debug->options & VALID_ROOT) {
                debug->options |= VALID_PARENT;

                assert(Py_REFCNT(debug->self) >= 1);
        }

        if (debug->options & (VALID_USER | VALID_PARENT)) {
                check_invariants(debug->self);
        }

        if ((debug->options & VALID_USER) && (debug->options & VALID_RW)) {
                assert(!blist_unstable);
                blist_unstable = 1;
        }
}

static void debug_return(debug_t *debug)
{
        Py_DECREF(Py_None);
        assert(blist_in_code);
        blist_in_code--;

#if 0
        /* Comment this test out since users can get references via
         * the gc module */
        if (debug->options & VALID_RW) {
                assert(Py_REFCNT(debug->self) == 1
                       || PyRootBList_Check(debug->self));
        }
#endif

        if (debug->options
            & (VALID_PARENT|VALID_USER|VALID_OVERFLOW|VALID_COLLAPSE|VALID_ROOT))
                check_invariants(debug->self);

        if (debug->options & VALID_USER) {
                if (!blist_danger)
                        assert(decref_num == 0);
                if (!debug->self->leaf)
                        assert(((PyBListRoot *)debug->self)->last_n
                               == debug->self->n);
                assert(((PyBListRoot *)debug->self)->dirty_length
                       || ((PyBListRoot *)debug->self)->dirty_root);
        }

        if ((debug->options & VALID_USER) && (debug->options & VALID_RW)) {
                assert(blist_unstable);
                blist_unstable = 0;
        }
}

static PyObject *debug_return_overflow(debug_t *debug, PyObject *ret)
{
        if (debug->options & VALID_OVERFLOW) {
                if (ret == NULL) {
                        debug_return(debug);
                        return ret;
                }

                assert(PyBList_Check((PyObject *) ret));
                check_invariants((PyBList *) ret);
        }

        assert(!(debug->options & VALID_COLLAPSE));

        debug_return(debug);

        return ret;
}

static Py_ssize_t debug_return_collapse(debug_t *debug, Py_ssize_t ret)
{
        if (debug->options & VALID_COLLAPSE)
                assert (((Py_ssize_t) ret) >= 0);

        assert(!(debug->options & VALID_OVERFLOW));

        debug_return(debug);

        return ret;
}

#define invariants(self, options) debug_t _debug = { (PyBList *) (self), (options) }; \
        debug_setup(&_debug)

#define _blist(ret) (PyBList *) debug_return_overflow(&_debug, (PyObject *) (ret))
#define _ob(ret) debug_return_overflow(&_debug, (ret))
#define _int(ret) debug_return_collapse(&_debug, (ret))
#define _void() do {assert(!(_debug.options & (VALID_OVERFLOW|VALID_COLLAPSE))); debug_return(&_debug);} while (0)
#define _redir(ret) ((debug_return(&_debug), 1) ? (ret) : (ret))

#undef Py_RETURN_NONE
#define Py_RETURN_NONE return Py_INCREF(Py_None), _ob(Py_None)

#undef decref_flush
#define decref_flush() do { assert(_debug.options & VALID_DECREF); _decref_flush(); } while (0)

static void safe_decref_check(PyBList *self)
{
        int i;

        assert(PyBList_Check((PyObject *) self));

        if (Py_REFCNT(self) > 1)
                return;

        if (self->leaf) {
                for (i = 0; i < self->num_children; i++)
                        assert(self->children[i] == NULL
                               || Py_REFCNT(self->children[i]) > 1);
                return;
        }

        for (i = 0; i < self->num_children; i++)
                safe_decref_check((PyBList *)self->children[i]);
}

static void safe_decref(PyBList *self)
{
        assert(PyBList_Check((PyObject *) self));
        safe_decref_check(self);

        DANGER_BEGIN;
        Py_DECREF(self);
        DANGER_END;
}

#undef SAFE_DECREF
#undef SAFE_XDECREF
#define SAFE_DECREF(self) (safe_decref((PyBList *)(self)))
#define SAFE_XDECREF(self) if ((self) == NULL) ; else SAFE_DECREF((self))

#else /* !Py_DEBUG */

#define check_invariants(self)
#define invariants(self, options)
#define _blist(ret) (ret)
#define _ob(ret) (ret)
#define _int(ret) (ret)
#define _void()
#define _redir(ret) (ret)

#endif

BLIST_LOCAL(int)
append_and_squish(PyBList **out, int n, PyBList *leaf)
{
        if (n >= 1) {
                PyBList *last = out[n-1];
                if (last->num_children + leaf->num_children <= LIMIT) {
                        copy(last, last->num_children, leaf, 0,
                             leaf->num_children);
                        last->num_children += leaf->num_children;
                        last->n += leaf->num_children;
                        leaf->num_children = 0;
                        leaf->n = 0;
                } else {
                        int moved = LIMIT - last->num_children;
                        copy(last, last->num_children, leaf, 0, moved);
                        shift_left(leaf, moved, moved);
                        last->num_children = LIMIT;
                        last->n = LIMIT;
                        leaf->num_children -= moved;
                        leaf->n -= moved;
                }
        } 
        if (!leaf->num_children) 
                SAFE_DECREF(leaf);
        else
                out[n++] = leaf;
        return n;
}

BLIST_LOCAL(int)
balance_last_2(PyBList **out, int n)
{
        PyBList *last;

        if (n >= 2)
                balance_leafs(out[n-2], out[n-1]);
        if (n >= 1) {
                last = out[n-1];
                if (!last->num_children) {
                        SAFE_DECREF(last);
                        n--;
                }
        }
        return n;
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
                DANGER_BEGIN;
                self = PyObject_GC_New(PyBList, &PyBList_Type);
                DANGER_END;
                if (self == NULL)
                        return NULL;
                self->children = PyMem_New(PyObject *, LIMIT);
                if (self->children == NULL) {
                        PyObject_GC_Del(self);
                        PyErr_NoMemory();
                        return NULL;
                }
        }

        self->leaf = 1; /* True */
        self->num_children = 0;
        self->n = 0;
        
        PyObject_GC_Track(self);
                
        return self;
}

/* Creates a blist for user use */
static PyBList *blist_root_new(void)
{
        PyBList *self;

        if (num_free_ulists) {
                self = free_ulists[--num_free_ulists];
                _Py_NewReference((PyObject *) self);
        } else {
                DANGER_BEGIN;
                self = (PyBList *) PyObject_GC_New(PyBListRoot, &PyRootBList_Type);
                DANGER_END;
                if (self == NULL)
                        return NULL;
                self->children = PyMem_New(PyObject *, LIMIT);
                if (self->children == NULL) {
                        PyObject_GC_Del(self);
                        PyErr_NoMemory();
                        return NULL;
                }
        }
        
        self->leaf = 1; /* True */
        self->n = 0;
        self->num_children = 0;

        ext_init((PyBListRoot *) self);

        PyObject_GC_Track(self);

        return self;
}

/* Remove links to some of our children, decrementing their refcounts */
static void blist_forget_children2(PyBList *self, int i, int j)
{
        int delta = j - i;

        invariants(self, VALID_RW);

        shift_left_decref(self, j, delta);
        self->num_children -= delta;

        _void();
}

/* Remove links to all children */
#define blist_forget_children(self) \
        (blist_forget_children2((self), 0, (self)->num_children))

/* Remove link to one child */
#define blist_forget_child(self, i) \
        (blist_forget_children2((self), (i), (i)+1))

/* Version for internal use defers Py_DECREF calls */
static int blist_CLEAR(PyBList *self)
{
        invariants(self, VALID_RW|VALID_PARENT);

        blist_forget_children(self);
        self->n = 0;
        self->leaf = 1;

        return _int(0);
}

/* Make self into a copy of other */
BLIST_LOCAL(void)
blist_become(PyBList *restrict self, PyBList *restrict other)
{
        invariants(self, VALID_RW);
        assert(self != other);

        Py_INCREF(other); /* "other" may be one of self's children */
        blist_forget_children(self);
        self->n = other->n;
        xcopyref(self, 0, other, 0, other->num_children);
        self->num_children = other->num_children;
        self->leaf = other->leaf;

        SAFE_DECREF(other);
        _void();
}

/* Make self into a copy of other and empty other */
BLIST_LOCAL(void)
blist_become_and_consume(PyBList *restrict self, PyBList *restrict other)
{
        PyObject **tmp;

        invariants(self, VALID_RW);
        assert(self != other);
        assert(Py_REFCNT(other) == 1 || PyRootBList_Check(other));

        Py_INCREF(other);
        blist_forget_children(self);
        tmp = self->children;
        self->children = other->children;
        self->n = other->n;
        self->num_children = other->num_children;
        self->leaf = other->leaf;

        other->children = tmp;
        other->n = 0;
        other->num_children = 0;
        other->leaf = 1;

        SAFE_DECREF(other);
        _void();
}

/* Create a copy of self */
static PyBList *blist_copy(PyBList *restrict self)
{
        PyBList *copy;

        copy = blist_new();
        if (!copy) return NULL;
        blist_become(copy, self);
        return copy;
}

/* Create a copy of self, which is a root node */
static PyBList *blist_root_copy(PyBList *restrict self)
{
        PyBList *copy;

        copy = blist_root_new();
        if (!copy) return NULL;
        blist_become(copy, self);
        ext_mark(copy, 0, DIRTY);
        ext_mark_set_dirty_all(self);
        return copy;
}

/************************************************************************
 * Useful internal utility functions
 */

/* We are searching for the child that contains leaf element i.
 *
 * Returns a 3-tuple: (the child object, our index of the child,
 *                     the number of leaf elements before the child)
 */
static void blist_locate(PyBList *self, Py_ssize_t i,
                         PyObject **child, int *idx, Py_ssize_t *before)
{
        invariants(self, VALID_PARENT);
        assert (!self->leaf);

        if (i <= self->n/2) {
                /* Search from the left */
                Py_ssize_t so_far = 0;
                int k;
                for (k = 0; k < self->num_children; k++) {
                        PyBList *p = (PyBList *) self->children[k];
                        if (i < so_far + p->n) {
                                *child = (PyObject *) p;
                                *idx = k;
                                *before = so_far;
                                _void();
                                return;
                        }
                        so_far += p->n;
                }
        } else {
                /* Search from the right */
                Py_ssize_t so_far = self->n;
                int k;
                for (k = self->num_children-1; k >= 0; k--) {
                        PyBList *p = (PyBList *) self->children[k];
                        so_far -= p->n;
                        if (i >= so_far) {
                                *child = (PyObject *) p;
                                *idx = k;
                                *before = so_far;
                                _void();
                                return;
                        }
                }
        }

        /* Just append */
        *child = self->children[self->num_children-1];
        *idx = self->num_children-1;
        *before = self->n - ((PyBList *)(*child))->n;

        _void();
}

/* Find the current height of the tree.
 *
 *      We could keep an extra few bytes in each node rather than
 *      figuring this out dynamically, which would reduce the
 *      asymptotic complexitiy of a few operations.  However, I
 *      suspect it's not worth the extra overhead of updating it all
 *      over the place.
 */
BLIST_LOCAL(int)
blist_get_height(PyBList *self)
{
        invariants(self, VALID_PARENT);
        if (self->leaf)
                return _int(1);
        return _int(blist_get_height((PyBList *)
                                     self->children[self->num_children - 1])
                    + 1);
}

BLIST_LOCAL(PyBList *)
blist_prepare_write(PyBList *self, int pt)
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

        invariants(self, VALID_RW);
        assert(!self->leaf);

        if (pt < 0)
                pt += self->num_children;
        if (Py_REFCNT(self->children[pt]) > 1) {
                PyBList *new_copy = blist_new();
                if (!new_copy) return NULL;
                blist_become(new_copy, (PyBList *) self->children[pt]);
                SAFE_DECREF(self->children[pt]);
                self->children[pt] = (PyObject *) new_copy;
        }

        return (PyBList *) _ob(self->children[pt]);
}

/* Macro version assumes that pt is non-negative */
#define blist_PREPARE_WRITE(self, pt) (Py_REFCNT((self)->children[(pt)]) > 1 ? blist_prepare_write((self), (pt)) : (PyBList *) (self)->children[(pt)])

/* Recompute self->n */
BLIST_LOCAL(void)
blist_adjust_n(PyBList *restrict self)
{
        int i;

        invariants(self, VALID_RW);

        if (self->leaf) {
                self->n = self->num_children;
                _void();
                return;
        }
        self->n = 0;
        for (i = 0; i < self->num_children; i++)
                self->n += ((PyBList *)self->children[i])->n;

        _void();
}

/* Non-default constructor.  Create a node with specific children.
 *
 * We steal the reference counters from the caller.
 */
static PyBList *blist_new_sibling(PyBList *sibling)
{
        PyBList *restrict self = blist_new();
        if (!self) return NULL;
        assert(sibling->num_children == LIMIT);
        copy(self, 0, sibling, HALF, HALF);
        self->leaf = sibling->leaf;
        self->num_children = HALF;
        sibling->num_children = HALF;
        blist_adjust_n(self);
        return self;
}

/************************************************************************
 * Bit twiddling utility function
 */

/* Return the highest set bit.  E.g., for 0x00845894 return 0x00800000 */
static unsigned highest_set_bit_slow(unsigned x)
{
        unsigned rv = 0;
        unsigned mask;
        for (mask = 0x1; mask; mask <<= 1)
                if (mask & x) rv = mask;
        return rv;
}

#if SIZEOF_INT == 4
static unsigned highest_set_bit_table[256];

static void highest_set_bit_init(void)
{
        unsigned i;
        for (i = 0; i < 256; i++)
                highest_set_bit_table[i] = highest_set_bit_slow(i);
}

static unsigned highest_set_bit(unsigned v)
{
        register unsigned tt, t;

        if ((tt = v >> 16))
                return (t = tt >> 8) ? highest_set_bit_table[t] << 24
                        : highest_set_bit_table[tt] << 16;
        else
                return (t = v >> 8) ? highest_set_bit_table[t] << 8
                        : highest_set_bit_table[v];
}
#else
#define highest_set_bit_init() ()
#define highest_set_bit(v) (highest_set_bit_slow((v)))
#endif

/************************************************************************
 * Functions for the index extension used by the root node
 */

/* Initialize the index extension.  Does not allocate any memory */
static void ext_init(PyBListRoot *root)
{
        root->index_list = NULL;
        root->offset_list = NULL;
        root->setclean_list = NULL;
        root->index_length = 0;
        root->dirty = NULL;
        root->dirty_length = 0;
        root->dirty_root = DIRTY;
        root->free_root = -1;

#ifdef Py_DEBUG
        root->last_n = root->n;
#endif
}

/* Deallocate any memory used by the index extension */
static void ext_dealloc(PyBListRoot *root)
{
        if (root->index_list) PyMem_Free(root->index_list);
        if (root->offset_list) PyMem_Free(root->offset_list);
        if (root->setclean_list) PyMem_Free(root->setclean_list);
        if (root->dirty) PyMem_Free(root->dirty);
        ext_init(root);
}

/* Find or create a new free node in "dirty" and return an index to it.
 * amortized O(1) */
static Py_ssize_t ext_alloc(PyBListRoot *root)
{
        Py_ssize_t i, parent;

        if (root->free_root < 0) {
                int newl;
                int i;

                if (!root->dirty) {
                        newl = 32;
                        root->dirty = PyMem_New(Py_ssize_t, newl);
                        root->dirty_root = DIRTY;
                        if (!root->dirty) return -1;
                } else {
                        void *tmp;

                        assert(root->dirty_length > 0);
                        newl = root->dirty_length*2;
                        tmp = root->dirty;
                        PyMem_Resize(tmp, Py_ssize_t, newl);
                        if (!tmp) {
                                PyMem_Free(root->dirty);
                                root->dirty = NULL;
                                root->dirty_root = DIRTY;
                                return -1;
                        }
                        root->dirty = tmp;
                }

                for (i = root->dirty_length; i < newl; i += 2) {
                        root->dirty[i] = i+2;
                        root->dirty[i+1] = -1;
                }
                root->dirty[newl-2] = -1;
                root->free_root = root->dirty_length;
                root->dirty_length = newl;
                assert(root->free_root >= 0);
                assert(root->free_root+1 < root->dirty_length);
        }

        /* Depth-first search for a node with fewer than 2 children.
         * Guaranteed to terminate in O(log n) since any leaf node
         * will suffice.
         */

        i = root->free_root;
        parent = -1;
        assert(i >= 0);
        assert(i+1 < root->dirty_length);
        while (root->dirty[i] >= 0 && root->dirty[i+1] >= 0) {
                assert(0);
                assert(i >= 0);
                assert(i+1 < root->dirty_length);
                parent = i;
                i = root->dirty[i];
        }

        /* At this point, "i" is the node to be alloced.  "parent" is
         * the node containing a pointer to "i" or -1 if free_root
         * points to "i"
         *
         * parent's pointer to i is always the left-hand pointer
         *
         * i has at most one child
         */

        if (parent < 0) {
                if (root->dirty[i] >= 0)
                        root->free_root = root->dirty[i];
                else
                        root->free_root = root->dirty[i+1];
        } else {
                if (root->dirty[i] >= 0)
                        root->dirty[parent] = root->dirty[i];
                else
                        root->dirty[parent] = root->dirty[i+1];
        }

        assert(i >= 0);
        assert(i+1 < root->dirty_length);
        return i;
}

/* Add each node in the tree rooted at loc to the free tree */
/* Amortized O(1), since each node to be freed corresponds with
 * an earlier lookup.  Unamortized worst-case O(n)*/
static void ext_free(PyBListRoot *root, Py_ssize_t loc)
{
        assert(loc >= 0);
        assert(loc+1 < root->dirty_length);
        if (root->dirty[loc] >= 0)
                ext_free(root, root->dirty[loc]);
        if (root->dirty[loc+1] >= 0)
                ext_free(root, root->dirty[loc+1]);

        root->dirty[loc] = root->free_root;
        root->dirty[loc+1] = -1;
        root->free_root = loc;
        assert(root->free_root >= 0);
        assert(root->free_root+1 < root->dirty_length);
}

BLIST_LOCAL(void)
ext_mark_r(PyBListRoot *root, Py_ssize_t offset, Py_ssize_t i, 
	   int bit, int value)
{
        Py_ssize_t j, next;

        if (!(offset & bit)) {
                /* Take left fork */

                if (value == DIRTY) {
                        /* Mark right fork dirty */
                        assert(i >= 0 && i+1 < root->dirty_length);
                        if (root->dirty[i+1] >= 0)
                                ext_free(root, root->dirty[i+1]);
                        root->dirty[i+1] = DIRTY;
                }
                next = i;
        } else {
                /* Take right fork */
                next = i+1;
        }

        assert(next >= 0 && next < root->dirty_length);

        j = root->dirty[next];

        if (j == value)
                return;

        if (bit == 1) {
                root->dirty[next] = value;
                return;
        }

        if (j < 0) {
                Py_ssize_t nvalue = j;
                Py_ssize_t tmp;
                tmp = ext_alloc(root);
                if (tmp < 0) {
                        ext_dealloc(root);
                        return;
                }
                root->dirty[next] = tmp;
                j = root->dirty[next];
                assert(j >= 0);
                assert(j+1 < root->dirty_length);
                root->dirty[j] = nvalue;
                root->dirty[j+1] = nvalue;
        }

        ext_mark_r(root, offset, j, bit >> 1, value);

        if (root->dirty
            && (root->dirty[j] == root->dirty[j+1]
                || (root->dirty[j] < 0
                    && (((offset | (bit>>1)) & ~((bit>>1)-1))
                        > (root->n-1) /INDEX_FACTOR)))) {
                /* Both the same?  Consolidate */
                ext_free(root, j);
                root->dirty[next] = value;
        }
}

/* If "value" is CLEAN, mark the list clean at exactly "offset".
 * If "value" is DIRTY, mark the list dirty for all >= "offset".
 */
static void ext_mark(PyBList *broot, Py_ssize_t offset, int value)
{
        int bit;

        PyBListRoot *root = (PyBListRoot*) broot;
        if (!root->n) {
#ifdef Py_DEBUG
                root->last_n = root->n;
#endif
                return;
        }
        if ((!offset && value == DIRTY) || root->n <= INDEX_FACTOR) {
                if (root->dirty_root >= 0)
                        ext_free(root, root->dirty_root);
                root->dirty_root = DIRTY;
#ifdef Py_DEBUG
                root->last_n = root->n;
#endif
                return;
        }

        assert(root->last_n == root->n);

        if (root->dirty_root == value) return;

        if (root->dirty_root < 0) {
                Py_ssize_t nvalue = root->dirty_root;
                root->dirty_root = ext_alloc(root);
                if (root->dirty_root < 0) {
                        ext_dealloc(root);
                        return;
                }
                assert(root->dirty_root >= 0);
                assert(root->dirty_root+1 < root->dirty_length);
                root->dirty[root->dirty_root] = nvalue;
                root->dirty[root->dirty_root+1] = nvalue;
        }
        offset /= INDEX_FACTOR;

        bit = highest_set_bit((root->n-1) / INDEX_FACTOR);
        ext_mark_r(root, offset, root->dirty_root, bit, value);
        if (root->dirty &&
            (root->dirty[root->dirty_root] ==root->dirty[root->dirty_root+1])){
                ext_free(root, root->dirty_root);
                root->dirty_root = value;
        }
}

/* Mark a section of the list dirty for set operations */
static void ext_mark_set_dirty(PyBList *broot, Py_ssize_t i, Py_ssize_t j)
{
        /* XXX We could set only the values in the setclean_list, but
         * that takes O(n) time.  Marking the nodes dirty for reading
         * takes O(log n) time but will slow down future reads.
         * Ideally there'd be a separate index_list for set
         * operations */
        ext_mark(broot, i, DIRTY); 
}

/* Mark an entire list dirty for set operations */
static void ext_mark_set_dirty_all(PyBList *broot)
{
        ext_mark_set_dirty(broot, 0, broot->n);
}

#if 0
/* These functions are unused, but useful for debugging.  Do not remove. */

static void ext_print_r(PyBListRoot *root, Py_ssize_t i)
{
        printf("(");
        if (root->dirty[i] < 0)
                printf("%d", root->dirty[i]);
        else
                ext_print_r(root, root->dirty[i]);
        printf(",");
        if (root->dirty[i+1] < 0)
                printf("%d", root->dirty[i+1]);
        else
                ext_print_r(root, root->dirty[i+1]);
        printf(")");
}

static void ext_print(PyBListRoot *root)
{
        if (root->dirty_root < 0)
                printf("%d", root->dirty_root);
        else
                ext_print_r(root, root->dirty_root);
        printf("\n");
}
#endif

/* Find an arbitrary DIRTY node in "dirty" at or below node "i" which
 * corresponds with "offset" into the list.  "bit" corresponds with
 * the bit tested to determine the right or left child of "i".
 * Returns the offset corresponding with the DIRTY node
 */
static Py_ssize_t
ext_find_dirty(PyBListRoot *root, Py_ssize_t offset, int bit, Py_ssize_t i)
{
        assert(root->dirty);
        assert(i >= 0);
        assert(bit);

        if (root->dirty[i] == DIRTY)
                return offset;
        if (root->dirty[i] >= 0)
                return ext_find_dirty(root, offset, bit >> 1, root->dirty[i]);

        if (root->dirty[i+1] == DIRTY)
                return offset | bit;
        assert(root->dirty[i+1] >= 0);
        return ext_find_dirty(root, offset | bit, bit >> 1, root->dirty[i+1]);
}

/* Determine if "offset" is DIRTY, returning a Boolean value.  Sets
 * "dirty_offset" to an arbitrary DIRTY node located along the way,
 * even if the requested offset is CLEAN.  "dirty_offset" will be set
 * to -1 if there are no DIRTY nodes.  Worst-case O(log n)
 */
static int
 ext_is_dirty(PyBListRoot *root, Py_ssize_t offset, Py_ssize_t *dirty_offset)
{
	Py_ssize_t i, parent;
	int bit;

        if (root->dirty == NULL || root->dirty_root < 0) {
                *dirty_offset = -1;
                return root->dirty_root == DIRTY;
        }
        i = root->dirty_root;
        parent = -1;
        offset /= INDEX_FACTOR;
        bit = highest_set_bit((root->n-1) / INDEX_FACTOR);

        assert(root->last_n == root->n);

        do {
                assert(bit);
                parent = i;
                if (!(offset & bit)) {
                        assert (i >= 0 && i < root->dirty_length);
                        i = root->dirty[i];
                } else {
                        assert (i >= 0 && i+1 < root->dirty_length);
                        i = root->dirty[i+1];
                }
                bit >>= 1;
        } while (i >= 0);

        if (i != DIRTY) {
                if (!bit) bit = 1; else bit <<= 1;
                *dirty_offset = INDEX_FACTOR *
                        ext_find_dirty(root, (offset ^ bit) & ~(bit-1), bit,
                                       parent);
                assert(*dirty_offset >= 0);
                assert(*dirty_offset < root->n);
        }

        return i == DIRTY;
}

/* The length of the BList may have changed.  Adjust the lengths of
 * the extension data structures as needed */
static int 
ext_grow_index(PyBListRoot *root)
{
        Py_ssize_t oldl = root->index_length;
        if (!root->index_length) {
                if (root->index_list) PyMem_Free(root->index_list);
                if (root->offset_list) PyMem_Free(root->offset_list);
                if (root->setclean_list) PyMem_Free(root->setclean_list);

                root->index_list = NULL;
                root->offset_list = NULL;
                root->setclean_list = NULL;

                root->index_length = root->n / INDEX_FACTOR + 1;
                root->index_list = PyMem_New(PyBList *, root->index_length);
                if (!root->index_list) {
                fail:
                        root->index_length = oldl;
                        return -1;
                }
                root->offset_list = PyMem_New(Py_ssize_t, root->index_length);
                if (!root->offset_list) goto fail;
                root->setclean_list
                        = PyMem_New(unsigned,SETCLEAN_LEN(root->index_length));
                if (!root->setclean_list) goto fail;
        } else {
                void *tmp;

                do {
                        root->index_length *= 2;
                } while (root->n / INDEX_FACTOR + 1 > root->index_length);
                tmp = root->index_list;
                PyMem_Resize(tmp, PyBList *, root->index_length);
                if (!tmp) goto fail;
                root->index_list = tmp;

                tmp = root->offset_list;
                PyMem_Resize(tmp, Py_ssize_t, root->index_length);
                if (!tmp) goto fail;
                root->offset_list = tmp;

                tmp = root->setclean_list;
                PyMem_Resize(tmp, unsigned, SETCLEAN_LEN(root->index_length));
                if (!tmp) goto fail;
                root->setclean_list = tmp;
        }
        return 0;
}

#define SET_OK_NO 0
#define SET_OK_YES 1
#define SET_OK_ALL 2

BLIST_LOCAL(void)
ext_index_all_r(PyBListRoot *root, PyBList *self, Py_ssize_t i, int set_ok)
{
        int j;
        if (self != (PyBList *)root) {
                assert(!(set_ok == SET_OK_ALL && Py_REFCNT(self) != 1));
                set_ok = set_ok && (Py_REFCNT(self) == 1);
        }

        if (((PyBList*)self->children[0])->leaf) {
                for (j = 0; j < self->num_children; j++) {
                        PyBList *child = (PyBList *) self->children[j];
                        Py_ssize_t ioffset = i / INDEX_FACTOR;
                        if (ioffset * INDEX_FACTOR < i) ioffset++;
                        do {
                                assert(ioffset < root->index_length);
                                root->index_list[ioffset] = child;
                                root->offset_list[ioffset] = i;
                                if (set_ok != SET_OK_ALL) {
                                        if (Py_REFCNT(child) > 1 || !set_ok)
                                                CLEAR_BIT(root->setclean_list,ioffset);
                                        else
                                                SET_BIT(root->setclean_list, ioffset);
                                }
                        } while (++ioffset * INDEX_FACTOR < i + child->n);
                        i += child->n;
                }
        } else {
                for (j = 0; j < self->num_children; j++) {
                        PyBList *child = (PyBList *) self->children[j];
                        ext_index_all_r(root, child, i, set_ok);
                        i += child->n;
                }
        }
                        
}

/* Make everything clean in O(n) time.  Any operation that alters the
 * list and already takes Omega(n) time should call this.
 *
 * The caller may have just permuted the list, so assume that the
 * entire cache is currently dirty even if not marked as such.
 */
void _ext_index_all(PyBListRoot *root, int set_ok_all)
{
        Py_ssize_t ioffset_max = root->n / INDEX_FACTOR + 1;
        int set_ok;

        if (root->index_length < ioffset_max)
                ext_grow_index(root);
        if (set_ok_all) {
                set_ok = SET_OK_ALL;
                memset(root->setclean_list, 255,
                       SETCLEAN_LEN(root->index_length));
        } else
                set_ok = SET_OK_YES;

#ifdef Py_DEBUG
        root->last_n = root->n;
#endif
        if (root->dirty_root >= 0)
                ext_free(root, root->dirty_root);
        root->dirty_root = CLEAN;

        ext_index_all_r(root, (PyBList*)root, 0, set_ok);
}
#define ext_index_all(root) do { if (!(root)->leaf) _ext_index_all((root), 0); } while (0)
#define ext_index_set_all(root) do { if (!(root)->leaf) _ext_index_all((root), 1); } while (0)

/* We found a particular node at a certain offset.  Add it to the
 * index and mark it clean. */
BLIST_LOCAL(void)
ext_mark_clean(PyBListRoot *root, Py_ssize_t offset, PyBList *p, int setclean)
{
        Py_ssize_t ioffset = offset / INDEX_FACTOR;

        assert(offset < root->n);

        while (ioffset * INDEX_FACTOR < offset)
                ioffset++;
        for (;ioffset * INDEX_FACTOR < offset + p->n; ioffset++) {
                ext_mark((PyBList*)root, ioffset * INDEX_FACTOR,CLEAN);

                if (ioffset >= root->index_length) {
                        int err = ext_grow_index(root);
                        if (err < -1) {
                                ext_dealloc(root);
                                return;
                        }
                }

                assert(ioffset >= 0);
                assert(ioffset < root->index_length);
                root->index_list[ioffset] = p;
                root->offset_list[ioffset] = offset;

                if (setclean)
                        SET_BIT(root->setclean_list, ioffset);
                else
                        CLEAR_BIT(root->setclean_list, ioffset);
        }
}

/* Lookup the node at offset i and mark it clean */
static PyObject *ext_make_clean(PyBListRoot *root, Py_ssize_t i)
{
        PyObject *rv;
        Py_ssize_t so_far;
        Py_ssize_t offset = 0;
        PyBList *p = (PyBList *)root;
        Py_ssize_t j = i;
	int k;
        int setclean = 1;
        do {
                blist_locate(p, j, (PyObject **) &p, &k, &so_far);
                if (Py_REFCNT(p) > 1)
                        setclean = 0;
                offset += so_far;
                j -= so_far;
        } while (!p->leaf);

        rv = p->children[j];
        ext_mark_clean(root, offset, p, setclean);
        return rv;
}

/************************************************************************
 * Functions for manipulating the tree
 */

/* Child k has underflowed.  Borrow from k+1 */
static void blist_borrow_right(PyBList *self, int k)
{
        PyBList *restrict p = (PyBList *) self->children[k];
        PyBList *restrict right;
        unsigned total;
        unsigned split;
        unsigned migrate;

        invariants(self, VALID_RW);

        right = blist_prepare_write(self, k+1);
        total = p->num_children + right->num_children;
        split = total / 2;
        migrate = split - p->num_children;

        assert(split >= HALF);
        assert(total-split >= HALF);

        copy(p, p->num_children, right, 0, migrate);
        p->num_children += migrate;
        shift_left(right, migrate, migrate);
        right->num_children -= migrate;
        blist_adjust_n(right);
        blist_adjust_n(p);

        _void();
}

/* Child k has underflowed.  Borrow from k-1 */
static void blist_borrow_left(PyBList *self, int k)
{
        PyBList *restrict p = (PyBList *) self->children[k];
        PyBList *restrict left;
        unsigned total;
        unsigned split;
        unsigned migrate;

        invariants(self, VALID_RW);

        left = blist_prepare_write(self, k-1);
        total = p->num_children + left->num_children;
        split = total / 2;
        migrate = split - p->num_children;

        assert(split >= HALF);
        assert(total-split >= HALF);

        shift_right(p, 0, migrate);
        copy(p, 0, left, left->num_children - migrate, migrate);
        p->num_children += migrate;
        left->num_children -= migrate;
        blist_adjust_n(left);
        blist_adjust_n(p);

        _void();
}

/* Child k has underflowed.  Merge with k+1 */
static void blist_merge_right(PyBList *self, int k)
{
        int i;
        PyBList *restrict p = (PyBList *) self->children[k];
        PyBList *restrict p2 = (PyBList *) self->children[k+1];

        invariants(self, VALID_RW);

        copy(p, p->num_children, p2, 0, p2->num_children);
        for (i = 0; i < p2->num_children; i++)
                Py_INCREF(p2->children[i]);
        p->num_children += p2->num_children;
        blist_forget_child(self, k+1);
        blist_adjust_n(p);

        _void();
}

/* Child k has underflowed.  Merge with k-1 */
static void blist_merge_left(PyBList *self, int k)
{
        int i;
        PyBList *restrict p = (PyBList *) self->children[k];
        PyBList *restrict p2 = (PyBList *) self->children[k-1];

        invariants(self, VALID_RW);

        shift_right(p, 0, p2->num_children);
        p->num_children += p2->num_children;
        copy(p, 0, p2, 0, p2->num_children);
        for (i = 0; i < p2->num_children; i++)
                Py_INCREF(p2->children[i]);
        blist_forget_child(self, k-1);
        blist_adjust_n(p);

        _void();
}

/* Collapse the tree, if possible */
BLIST_LOCAL(int)
blist_collapse(PyBList *self)
{
        PyBList *p;
        invariants(self, VALID_RW|VALID_COLLAPSE);

        if (self->num_children != 1 || self->leaf) {
                blist_adjust_n(self);
                return _int(0);
        }

        p = blist_PREPARE_WRITE(self, 0);
        blist_become_and_consume(self, p);
        check_invariants(self);
        return _int(1);
}

/* Check if children k-1, k, or k+1 have underflowed.
 *
 * If so, move things around until self is the root of a valid
 * subtree again, possibly requiring collapsing the tree.
 *
 * Always calls self._adjust_n() (often via self.__collapse()).
 */
BLIST_LOCAL(int)
blist_underflow(PyBList *self, int k)
{
        invariants(self, VALID_RW|VALID_COLLAPSE);

        if (self->leaf) {
                blist_adjust_n(self);
                return _int(0);
        }

        if (k < self->num_children) {
                PyBList *restrict p = blist_prepare_write(self, k);
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
                        else /* No siblings for p */
                                return _int(blist_collapse(self));

                        p = blist_prepare_write(self, k);
                        shrt = HALF - p->num_children;
                }
        }

        if (k > 0 && ((PyBList *)self->children[k-1])->num_children < HALF) {
                int collapse = blist_underflow(self, k-1);
                if (collapse) return _int(collapse);
        }

        if (k+1 < self->num_children
            && ((PyBList *)self->children[k+1])->num_children < HALF) {
                int collapse = blist_underflow(self, k+1);
                if (collapse) return _int(collapse);
        }

        return _int(blist_collapse(self));
}

/* Insert 'item', which may be a subtree, at index k. */
BLIST_LOCAL(PyBList *)
blist_insert_here(PyBList *self, int k, PyObject *item)
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

        PyBList *restrict sibling;

        invariants(self, VALID_RW|VALID_OVERFLOW);
        assert(k >= 0);

        if (self->num_children < LIMIT) {
                int collapse;

                shift_right(self, k, 1);
                self->num_children++;
                self->children[k] = item;
                collapse = blist_underflow(self, k);
                assert(!collapse); (void) collapse;
                return _blist(NULL);
        }

        sibling = blist_new_sibling(self);

        if (k < HALF) {
                int collapse;

                shift_right(self, k, 1);
                self->num_children++;
                self->children[k] = item;
                collapse = blist_underflow(self, k);
                assert(!collapse); (void) collapse;
        } else {
                int collapse;

                shift_right(sibling, k - HALF, 1);
                sibling->num_children++;
                sibling->children[k - HALF] = item;
                collapse = blist_underflow(sibling, k - HALF);
                assert(!collapse); (void) collapse;
                blist_adjust_n(sibling);
        }

        blist_adjust_n(self);
        check_invariants(self);
        return _blist(sibling);
}

/* Recurse depth layers, then insert subtree on the left or right */
BLIST_LOCAL(PyBList *)
blist_insert_subtree(PyBList *self, int side, PyBList *subtree, int depth)
{
        /* This function may cause an overflow.
         *
         * depth == 0 means insert the subtree as a child of self.
         * depth == 1 means insert the subtree as a grandchild, etc.
         */

        PyBList *sibling;
        invariants(self, VALID_RW|VALID_OVERFLOW);
        assert(side == 0 || side == -1);

        self->n += subtree->n;

        if (depth) {
                PyBList *restrict p = blist_prepare_write(self, side);
                PyBList *overflow = blist_insert_subtree(p, side,
                                                         subtree, depth-1);
                if (!overflow) return _blist(NULL);
                if (side == 0)
                        side = 1;
                subtree = overflow;
        }

        if (side < 0)
                side = self->num_children;

        sibling = blist_insert_here(self, side, (PyObject *) subtree);

        return _blist(sibling);
}

/* Handle the case where a user-visible node overflowed */
BLIST_LOCAL(int)
blist_overflow_root(PyBList *self, PyBList *overflow)
{
        PyBList *child;

        invariants(self, VALID_RW);

        if (!overflow) return _int(0);
        child = blist_new();
        if (!child) {
                decref_later((PyObject*)overflow);
                return _int(0);
        }
        blist_become_and_consume(child, self);
        self->children[0] = (PyObject *)child;
        self->children[1] = (PyObject *)overflow;
        self->num_children = 2;
        self->leaf = 0;
        blist_adjust_n(self);
        return _int(-1);
}

/* Concatenate two trees of potentially different heights. */
BLIST_LOCAL(PyBList *)
blist_concat_blist(PyBList *left_subtree, PyBList *right_subtree,
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

        int adj = 0;
        PyBList *overflow;
        PyBList *root;

        assert(Py_REFCNT(left_subtree) == 1);
        assert(Py_REFCNT(right_subtree) == 1);

        if (height_diff == 0) {
                int collapse;

                root = blist_new();
                if (!root) {
                        decref_later((PyObject*)left_subtree);
                        decref_later((PyObject*)right_subtree);
                        return NULL;
                }
                root->children[0] = (PyObject *) left_subtree;
                root->children[1] = (PyObject *) right_subtree;
                root->leaf = 0;
                root->num_children = 2;
                collapse = blist_underflow(root, 0);
                if (!collapse)
                        collapse = blist_underflow(root, 1);
                if (!collapse)
                        adj = 1;
                overflow = NULL;
        } else if (height_diff > 0) { /* Left is larger */
                root = left_subtree;
                overflow = blist_insert_subtree(root, -1, right_subtree,
                                                height_diff - 1);
        } else { /* Right is larger */
                root = right_subtree;
                overflow = blist_insert_subtree(root, 0, left_subtree,
                                                -height_diff - 1);
        }

        adj += -blist_overflow_root(root, overflow);
        if (padj) *padj = adj;

        return root;
}

/* Concatenate two subtrees of potentially different heights. */
BLIST_LOCAL(PyBList *)
blist_concat_subtrees(PyBList *left_subtree, int left_depth,
                      PyBList *right_subtree, int right_depth, int *pdepth)
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
BLIST_LOCAL(PyBList *)
blist_concat_roots(PyBList *left_root, int left_height,
                   PyBList *right_root, int right_height, int *pheight)
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

BLIST_LOCAL(PyBList *)
blist_concat_unknown_roots(PyBList *left_root, PyBList *right_root)
{
        return blist_concat_roots(left_root, blist_get_height(left_root),
                                  right_root, blist_get_height(right_root),
                                  NULL);
}

/* Child at position k is too short by "depth".  Fix it */
BLIST_LOCAL(int)
blist_reinsert_subtree(PyBList *self, int k, int depth)
{
        PyBList *subtree;

        invariants(self, VALID_RW);

        assert(Py_REFCNT(self->children[k]) == 1);
        subtree = (PyBList *) self->children[k];
        shift_left(self, k+1, 1);
        self->num_children--;

        if (self->num_children > k) {
                /* Merge right */
                PyBList *p = blist_prepare_write(self, k);
                PyBList *overflow = blist_insert_subtree(p, 0,
                                                         subtree, depth-1);
                if (overflow) {
                        shift_right(self, k+1, 1);
                        self->num_children++;
                        self->children[k+1] = (PyObject *) overflow;
                }
        } else {
                /* Merge left */
                PyBList *p = blist_prepare_write(self, k-1);
                PyBList *overflow = blist_insert_subtree(p, -1,
                                                         subtree, depth-1);
                if (overflow) {
                        shift_right(self, k, 1);
                        self->num_children++;
                        self->children[k] = (PyObject *) overflow;
                }
        }

        return _int(blist_underflow(self, k));
}

/************************************************************************
 * The main insert and deletion operations
 */

/* Recursive to find position i, and insert item just there. */
BLIST_LOCAL(PyBList *)
ins1(PyBList *self, Py_ssize_t i, PyObject *item)
{
        PyBList *ret;
        PyBList *restrict p;
        int k;
        Py_ssize_t so_far;
        PyBList *overflow;

        invariants(self, VALID_RW|VALID_OVERFLOW);

        if (self->leaf) {
                Py_INCREF(item);

                /* Speed up the common case */
                if (self->num_children < LIMIT) {
                        shift_right(self, i, 1);
                        self->num_children++;
                        self->n++;
                        self->children[i] = item;
                        return _blist(NULL);
                }

                return _blist(blist_insert_here(self, i, item));
        }

        blist_locate(self, i, (PyObject **) &p, &k, &so_far);

        self->n += 1;
        p = blist_prepare_write(self, k);
        overflow = ins1(p, i - so_far, item);

        if (!overflow) ret = NULL;
        else ret = blist_insert_here(self, k+1, (PyObject *) overflow);

        return _blist(ret);
}

BLIST_LOCAL(int)
blist_extend_blist(PyBList *self, PyBList *other)
{
        PyBList *right, *left, *root;

        invariants(self, VALID_RW);

        /* Special case for speed */
        if (self->leaf && other->leaf && self->n + other->n <= LIMIT) {
                copyref(self, self->n, other, 0, other->n);
                self->n += other->n;
                self->num_children = self->n;
                return _int(0);
        }

        /* Make not-user-visible roots for the subtrees */
        right = blist_copy(other); /* XXX not checking return values */
        left = blist_new();
        if (left == NULL)
                return _int(-1);
        blist_become_and_consume(left, self);

        if (left->leaf && right->leaf) {
                balance_leafs(left, right);
                self->children[0] = (PyObject *) left;
                self->children[1] = (PyObject *) right;
                self->num_children = 2;
                self->leaf = 0;
                blist_adjust_n(self);
                return _int(0);
        }
        
        root = blist_concat_unknown_roots(left, right);
        blist_become_and_consume(self, root);
        SAFE_DECREF(root);
        return _int(0);
}

/* Recursive version of __delslice__ */
static int blist_delslice(PyBList *self, Py_ssize_t i, Py_ssize_t j)
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

        PyBList *restrict p, *restrict p2;
        int k, k2, depth;
        Py_ssize_t so_far, so_far2, low;
        int collapse_left, collapse_right, deleted_k, deleted_k2;

        invariants(self, VALID_RW | VALID_PARENT | VALID_COLLAPSE);
        check_invariants(self);

        if (j > self->n)
                j = self->n;

        if (i == j)
                return _int(0);

        if (self->leaf) {
                blist_forget_children2(self, i, j);
                self->n = self->num_children;
                return _int(0);
        }

        if (i == 0 && j >= self->n) {
                /* Delete everything. */
                blist_CLEAR(self);
                return _int(0);
        }

        blist_locate(self, i, (PyObject **) &p, &k, &so_far);
        blist_locate(self, j-1, (PyObject **) &p2, &k2, &so_far2);

        if (k == k2) {
                /* All of the deleted elements are contained under a single
                 * child of this node.  Recurse and check for a short
                 * subtree and/or underflow
                 */

                assert(so_far == so_far2);
                p = blist_prepare_write(self, k);
                depth = blist_delslice(p, i - so_far, j - so_far);
                if (p->n == 0) {
                        SAFE_DECREF(p);
                        shift_left(self, k+1, 1);
                        self->num_children--;
                        return _int(blist_collapse(self));
                }
                if (!depth)
                        return _int(blist_underflow(self, k));
                return _int(blist_reinsert_subtree(self, k, depth));
        }

        /* Deleted elements are in a range of child elements.  There
         * will be:
         * - a left child (k) where we delete some (or all) of its children
         * - a right child (k2) where we delete some (or all) of it children
         * - children in between who are deleted entirely
         */

        /* Call _delslice recursively on the left and right */
        p = blist_prepare_write(self, k);
        collapse_left = blist_delslice(p, i - so_far, j - so_far);
        p2 = blist_prepare_write(self, k2);
        low = i-so_far2 > 0 ? i-so_far2 : 0;
        collapse_right = blist_delslice(p2, low, j - so_far2);

        deleted_k = 0; /* False */
        deleted_k2 = 0; /* False */

        /* Delete [k+1:k2] */
        blist_forget_children2(self, k+1, k2);
        k2 = k+1;

        /* Delete k1 and k2 if they are empty */
        if (!((PyBList *)self->children[k2])->n) {
                decref_later((PyObject *) self->children[k2]);
                shift_left(self, k2+1, 1);
                self->num_children--;
                deleted_k2 = 1; /* True */
        }
        if (!((PyBList *)self->children[k])->n) {
                decref_later(self->children[k]);
                shift_left(self, k+1, 1);
                self->num_children--;
                deleted_k = 1; /* True */
        }

        if (deleted_k && deleted_k2) /* # No messy subtrees.  Good. */
                return _int(blist_collapse(self));

        /* The left and right may have collapsed and/or be in an
         * underflow state.  Clean them up.  Work on fixing collapsed
         * trees first, then worry about underflows.
         */

        if (!deleted_k && !deleted_k2 && collapse_left && collapse_right) {
                /* Both exist and collapsed.  Merge them into one subtree. */
                PyBList *left, *right, *subtree;

                left = (PyBList *) self->children[k];
                right = (PyBList *) self->children[k+1];
                shift_left(self, k+1, 1);
                self->num_children--;
                subtree = blist_concat_subtrees(left, collapse_left,
                                                right, collapse_right,
                                                &depth);
                self->children[k] = (PyObject *) subtree;
        } else if (deleted_k) {
                /* Only the right potentially collapsed, point there. */
                depth = collapse_right;
                /* k already points to the old k2, since k was deleted */
        } else if (!deleted_k2 && !collapse_left) {
                /* Only the right potentially collapsed, point there. */
                k = k + 1;
                depth = collapse_right;
        } else {
                depth = collapse_left;
        }

        /* At this point, we have a potentially short subtree at k,
         * with depth "depth".
         */

        if (!depth || self->num_children == 1) {
                /* Doesn't need merging, or no siblings to merge with */
                return _int(depth + blist_underflow(self, k));
        }

        /* We have a short subtree at k, and we have other children */
        return _int(blist_reinsert_subtree(self, k, depth));
}

BLIST_LOCAL(PyObject *)
blist_get1(PyBList *self, Py_ssize_t i)
{
        PyBList *p;
        int k;
        Py_ssize_t so_far;

        invariants(self, VALID_PARENT);

        if (self->leaf)
                return _ob(self->children[i]);

        blist_locate(self, i, (PyObject **) &p, &k, &so_far);
        assert(i >= so_far);
        return _ob(blist_get1(p, i - so_far));
}

BLIST_LOCAL(PyObject *)
blist_pop_last_fast(PyBList *self)
{
        PyBList *p;

        invariants(self, VALID_ROOT|VALID_RW);

        for (p = self; !p->leaf;
             p = (PyBList*)p->children[p->num_children-1]) {
                if (p != self && Py_REFCNT(p) > 1)
                        goto cleanup_and_slow;
                p->n--;
        }
        
        if ((Py_REFCNT(p) > 1 || p->num_children == HALF)
            && self != p) {
                PyBList *p2;
        cleanup_and_slow:
                for (p2 = self; p != p2;
                     p2 = (PyBList*)p2->children[p2->num_children-1])
                        p2->n++;
                return _ob(NULL);
        }
        p->n--;
        p->num_children--;
        
        if ((self->n) % INDEX_FACTOR == 0)
                ext_mark(self, 0, DIRTY);
#ifdef Py_DEBUG
        else
                ((PyBListRoot*)self)->last_n--;
#endif
        return _ob(p->children[p->num_children]);
}

static void blist_delitem(PyBList *self, Py_ssize_t i)
{
        invariants(self, VALID_ROOT|VALID_RW);
        if (i == self->n-1) {
                PyObject *v = blist_pop_last_fast(self);
                if (v) {
                        decref_later(v);
                        _void();
                        return;
                }
        }

        blist_delslice(self, i, i+1);
        _void();
}

static PyObject *blist_delitem_return(PyBList *self, Py_ssize_t i)
{
        PyObject *rv;

        rv = blist_get1(self, i);
        Py_INCREF(rv);
        blist_delitem(self, i);
        return rv;
}

/************************************************************************
 * BList iterator
 */

static iter_t *iter_init(iter_t *iter, PyBList *lst)
{
        iter->depth = 0;

        while(!lst->leaf) {
                iter->stack[iter->depth].lst = lst;
                iter->stack[iter->depth++].i = 1;
                Py_INCREF(lst);
                lst = (PyBList *) lst->children[0];
        }

        iter->leaf = lst;
        iter->i = 0;
        iter->depth++;
        Py_INCREF(lst);

        return iter;
}

static iter_t *iter_init2(iter_t *iter, PyBList *lst, Py_ssize_t start)
{
        iter->depth = 0;

        assert(start >= 0);
        while (!lst->leaf) {
                PyBList *p;
                int k;
                Py_ssize_t so_far;

                blist_locate(lst, start, (PyObject **) &p, &k, &so_far);
                iter->stack[iter->depth].lst = lst;
                iter->stack[iter->depth++].i = k + 1;
                Py_INCREF(lst);
                lst = p;
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
        if (p == NULL)
                return NULL;

	if (!p->leaf) {
		/* If p is the root, it may have been a leaf when we began
		 * iterating, but turned into a non-leaf during iteration.
		 * Modifying the list during iteration results in undefined
		 * behavior, so just throw in the towel.
		 */
		
		return NULL;			
	}

        if (iter->i < p->num_children)
                return p->children[iter->i++];

        iter->depth--;
        do {
                decref_later((PyObject *) p);
                if (!iter->depth) {
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
                decref_later((PyObject *) iter->stack[i].lst);
        if (iter->depth)
                decref_later((PyObject *) iter->leaf);
}

BLIST_PYAPI(PyObject *)
py_blist_iter(PyObject *oseq)
{
        PyBList *seq;
        blistiterobject *it;

        if (!PyRootBList_Check(oseq)) {
                PyErr_BadInternalCall();
                return NULL;
        }

        seq = (PyBList *) oseq;

        invariants(seq, VALID_USER);

        if (num_free_iters) {
                it = free_iters[--num_free_iters];
                _Py_NewReference((PyObject *) it);
        } else {
                DANGER_BEGIN;
                it = PyObject_GC_New(blistiterobject, &PyBListIter_Type);
                DANGER_END;
                if (it == NULL)
                        return _ob(NULL);
        }

        if (seq->leaf) {
                /* Speed up common case */
                it->iter.leaf = seq;
                it->iter.i = 0;
                it->iter.depth = 1;
                Py_INCREF(seq);
        } else
                iter_init(&it->iter, seq);

        PyObject_GC_Track(it);
        return _ob((PyObject *) it);
}

static void blistiter_dealloc(PyObject *oit)
{
        blistiterobject *it;

        assert(PyBListIter_Check(oit));
        it = (blistiterobject *) oit;

        PyObject_GC_UnTrack(it);
        iter_cleanup(&it->iter);
        if (num_free_iters < MAXFREELISTS
            && (Py_TYPE(it) == &PyBListIter_Type))
                free_iters[num_free_iters++] = it;
        else
                PyObject_GC_Del(it);
        _decref_flush();
}

static int blistiter_traverse(PyObject *oit, visitproc visit, void *arg)
{
        blistiterobject *it;
        int i;

        assert(PyBListIter_Check(oit));
        it = (blistiterobject *) oit;

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
        if (p == NULL)
                return NULL;
        if (p->leaf && it->iter.i < p->num_children) {
                obj = p->children[it->iter.i++];
                Py_INCREF(obj);
                return obj;
        }

        obj = iter_next(&it->iter);
        if (obj != NULL)
                Py_INCREF(obj);

        _decref_flush();
        return obj;
}

BLIST_PYAPI(PyObject *)
blistiter_len(blistiterobject *it)
{
        iter_t *iter = &it->iter;
        int depth;
        Py_ssize_t total = 0;

        if (!iter->leaf)
                return PyInt_FromLong(0);
        
        total += iter->leaf->n - iter->i;
        
        for (depth = iter->depth-2; depth >= 0; depth--) {
                point_t point = iter->stack[depth];
                int j;
                if (point.lst->leaf) continue;
                assert(point.i > 0);
                for (j = point.i; j < point.lst->num_children; j++) {
                        PyBList *child = (PyBList *) point.lst->children[j];
                        total += child->n;
                }
        }
	if (iter->depth > 1 && iter->stack[0].lst->leaf) {
		int extra = iter->stack[0].lst->n - iter->stack[0].i;
		if (extra > 0) total += extra;
	}
        return PyInt_FromLong(total); 
}

PyDoc_STRVAR(length_hint_doc, "Private method returning an estimate of len(list(it)).");

static PyMethodDef blistiter_methods[] = {
        {"__length_hint__", (PyCFunction)blistiter_len, METH_NOARGS, length_hint_doc},
        {NULL,          NULL}           /* sentinel */
};

PyTypeObject PyBListIter_Type = {
        PyVarObject_HEAD_INIT(NULL, 0)
        "blistiterator",                        /* tp_name */
        sizeof(blistiterobject),                /* tp_basicsize */
        0,                                      /* tp_itemsize */
        /* methods */
        blistiter_dealloc,                      /* tp_dealloc */
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
        blistiter_traverse,                     /* tp_traverse */
        0,                                      /* tp_clear */
        0,                                      /* tp_richcompare */
        0,                                      /* tp_weaklistoffset */
        PyObject_SelfIter,                      /* tp_iter */
        blistiter_next,                         /* tp_iternext */
        blistiter_methods,                      /* tp_methods */
        0,                                      /* tp_members */
};

/************************************************************************
 * BList reverse iterator
 */

BLIST_LOCAL(iter_t *)
riter_init2(iter_t *iter, PyBList *lst, Py_ssize_t start, Py_ssize_t stop)
{
        iter->depth = 0;

        assert(stop >= 0);
        assert(start >= 0);
        assert(start >= stop);
        while (!lst->leaf) {
                PyBList *p;
                int k;
                Py_ssize_t so_far;

                blist_locate(lst, start-1, (PyObject **) &p, &k, &so_far);
                iter->stack[iter->depth].lst = lst;
                iter->stack[iter->depth++].i = k - 1;
                Py_INCREF(lst);
                lst = p;
                start -= so_far;
        }

        iter->leaf = lst;
        iter->i = start-1;
        iter->depth++;
        Py_INCREF(lst);

        return iter;
}
#define riter_init(iter, lst) (riter_init2((iter), (lst), (lst)->n, 0))

BLIST_LOCAL(PyObject *)
iter_prev(iter_t *iter)
{
        PyBList *p;
        int i;

        p = iter->leaf;
        if (p == NULL)
                return NULL;

	if (!p->leaf) {
		/* If p is the root, it may have been a leaf when we began
		 * iterating, but turned into a non-leaf during iteration.
		 * Modifying the list during iteration results in undefined
		 * behavior, so just throw in the towel.
		 */
		
		return NULL;			
	}

        if (iter->i >= p->num_children && iter->i >= 0)
                iter->i = p->num_children - 1;

        if (iter->i >= 0)
                return p->children[iter->i--];

        iter->depth--;
        do {
                decref_later((PyObject *) p);
                if (!iter->depth) {
                        iter->leaf = NULL;
                        return NULL;
                }
                p = iter->stack[--iter->depth].lst;
                i = iter->stack[iter->depth].i;

                if (i >= p->num_children && i >= 0)
                        i = p->num_children - 1;
        } while (i < 0);

        assert(iter->stack[iter->depth].lst == p);
        iter->stack[iter->depth++].i = i-1;

        while (!p->leaf) {
                p = (PyBList *) p->children[i];
                Py_INCREF(p);
                i = p->num_children-1;
                iter->stack[iter->depth].lst = p;
                iter->stack[iter->depth++].i = i-1;
        }

        iter->leaf = iter->stack[iter->depth-1].lst;
        iter->i = iter->stack[iter->depth-1].i;

        return p->children[i];
}

BLIST_PYAPI(PyObject *)
py_blist_reversed(PyBList *seq)
{
        blistiterobject *it;

        invariants(seq, VALID_USER);

        DANGER_BEGIN;
        it = PyObject_GC_New(blistiterobject,
                             &PyBListReverseIter_Type);
        DANGER_END;
        if (it == NULL)
                return _ob(NULL);

        if (seq->leaf) {
                /* Speed up common case */
                it->iter.leaf = seq;
                it->iter.i = seq->n-1;
                it->iter.depth = 1;
                Py_INCREF(seq);
        } else
                riter_init(&it->iter, seq);

        PyObject_GC_Track(it);
        return _ob((PyObject *) it);
}

static PyObject *blistiter_prev(PyObject *oit)
{
        blistiterobject *it = (blistiterobject *) oit;
        PyObject *obj;

        /* Speed up common case */
        PyBList *p;
        p = it->iter.leaf;
        if (p == NULL)
                return NULL;

        if (it->iter.i >= p->num_children && it->iter.i >= 0)
                it->iter.i = p->num_children - 1;

        if (p->leaf && it->iter.i >= 0) {
                obj = p->children[it->iter.i--];
                Py_INCREF(obj);
                return obj;
        }

        obj = iter_prev(&it->iter);
        if (obj != NULL)
                Py_INCREF(obj);

        _decref_flush();
        return obj;
}

BLIST_PYAPI(PyObject *)
blistriter_len(blistiterobject *it)
{
        iter_t *iter = &it->iter;
        int depth;
        Py_ssize_t total = 0;

        total += iter->i + 1;
        
        for (depth = iter->depth-2; depth >= 0; depth--) {
                point_t point = iter->stack[depth];
                int j;
                if (point.lst->leaf) continue;
                for (j = 0; j <= point.i; j++) {
                        PyBList *child = (PyBList *) point.lst->children[j];
                        total += child->n;
                }
        }
	if (iter->depth > 1 && iter->stack[0].lst->leaf) {
		int extra = iter->stack[0].i + 1;
		if (extra > 0) total += extra;
	}
        return PyInt_FromLong(total); 
}

static PyMethodDef blistriter_methods[] = {
        {"__length_hint__", (PyCFunction)blistriter_len, METH_NOARGS, length_hint_doc},
        {NULL,          NULL}           /* sentinel */
};

PyTypeObject PyBListReverseIter_Type = {
        PyVarObject_HEAD_INIT(NULL, 0)
        "blistreverseiterator",                 /* tp_name */
        sizeof(blistiterobject),                /* tp_basicsize */
        0,                                      /* tp_itemsize */
        /* methods */
        blistiter_dealloc,                      /* tp_dealloc */
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
        blistiter_traverse,                     /* tp_traverse */
        0,                                      /* tp_clear */
        0,                                      /* tp_richcompare */
        0,                                      /* tp_weaklistoffset */
        PyObject_SelfIter,                      /* tp_iter */
        blistiter_prev,                         /* tp_iternext */
        blistriter_methods,                      /* tp_methods */
        0,                                      /* tp_members */
};

/************************************************************************
 * A forest is an array of BList tree structures, which may be of
 * different heights.  It's a temporary utility structure for certain
 * operations.  Specifically, it allows us to compose or decompose a
 * BList in O(n) time.
 *
 * The BList trees in the forest are stored in position order, left-to-right.
 *
 */

typedef struct Forest
{
        Py_ssize_t num_leafs;
        Py_ssize_t num_trees;
        Py_ssize_t max_trees;
        PyBList **list;
} Forest;

/* Remove the right-most element.  If it's a leaf, return it.
 * Otherwise, add all of its childrent to the forest *in reverse
 * order* and try again.  Assuming only one BList was added to the
 * forest, the effect is to return all of the leafs one-at-a-time
 * left-to-right. */
BLIST_LOCAL(PyBList *)
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
                                = blist_PREPARE_WRITE(node, i);

                node->num_children = 0;
                SAFE_DECREF(node);
                node = forest->list[--forest->num_trees];
        }

        return node;
}

#define MAX_FREE_FORESTS 20
static PyBList **forest_saved[MAX_FREE_FORESTS];
static unsigned forest_max_trees[MAX_FREE_FORESTS];
static unsigned num_free_forests = 0;

BLIST_LOCAL(Forest *)
forest_init(Forest *forest)
{
        forest->num_trees = 0;
        forest->num_leafs = 0;
        if (num_free_forests) {
                forest->list = forest_saved[--num_free_forests];
                forest->max_trees = forest_max_trees[num_free_forests];
        } else {
                forest->max_trees = LIMIT; /* enough for O(LIMIT**2) items */
                forest->list = PyMem_New(PyBList *, forest->max_trees);
                if (forest->list == NULL)
                        return (Forest *) PyErr_NoMemory();
        }
        return forest;
}

#if 0
BLIST_LOCAL(Forest *)
forest_new(void)
{
        Forest *forest = PyMem_New(Forest, 1);
        Forest *rv;
        if (forest == NULL)
                return (Forest *) PyErr_NoMemory();
        rv = forest_init(forest);
        if (rv == NULL)
                PyMem_Free(forest);
        return rv;
}

BLIST_LOCAL(void)
forest_grow(Forest *forest, Py_ssize_t new_max)
{
        if (forest->max_trees > new_max) return;
        /* XXX Check return value */
        PyMem_Resize(forest->list, PyBList *, new_max);
        forest->max_trees = new_max;
}
#endif

/* Append a tree to the forest.  Steals the reference to "leaf" */
BLIST_LOCAL(int)
forest_append(Forest *forest, PyBList *leaf)
{
        Py_ssize_t power = LIMIT;

        if (!leaf->num_children) {  /* Don't bother adding empty leaf nodes */
                SAFE_DECREF(leaf);
                return 0;
        }

        leaf->n = leaf->num_children;

        if (forest->num_trees == forest->max_trees) {
                PyBList **list = forest->list;

                forest->max_trees <<= 1;
                PyMem_Resize(list, PyBList *, forest->max_trees);
                if (list == NULL) {
                        PyErr_NoMemory();
                        return -1;
                }
                forest->list = list;
        }

        forest->list[forest->num_trees++] = leaf;
        forest->num_leafs++;

        while (forest->num_leafs % power == 0) {
                struct PyBList *parent = blist_new();
                int x;

                if (parent == NULL) {
                        PyErr_NoMemory();
                        return -1;
                }
                parent->leaf = 0;
                memcpy(parent->children,
                       &forest->list[forest->num_trees - LIMIT],
                       sizeof (PyBList *) * LIMIT);
                parent->num_children = LIMIT;
                forest->num_trees -= LIMIT;
                x = blist_underflow(parent, LIMIT - 1);
                assert(!x); (void) x;

                forest->list[forest->num_trees++] = parent;
                power *= LIMIT;
        }

        return 0;
}

#if 0
/* Like forest_append(), but handles the case where the previously
 * added leaf is in an underflow state. */
BLIST_LOCAL(int)
forest_append_safe(Forest *forest, PyBList *leaf)
{
        PyBList *last;

        if (forest->num_trees == 0)
                goto append;

        last = forest->list[forest->num_trees-1];

        if (!last->leaf || last->num_children >= HALF)
                goto append;

        if (last->num_children + leaf->num_children <= LIMIT) {
                copy(last, last->num_children, leaf, 0, leaf->num_children);
                last->num_children += leaf->num_children;
                last->n += leaf->num_children;
                leaf->num_children = 0;
        } else {
                int needed = HALF - last->num_children;

                copy(last, last->num_children, leaf, 0, needed);
                last->num_children += needed;
                last->n += needed;
                shift_left(leaf, needed, needed);
                leaf->num_children -= needed;
        }

 append:
        return forest_append(forest, leaf);
}
#endif

BLIST_LOCAL(void)
forest_uninit(Forest *forest)
{
        Py_ssize_t i;
        for (i = 0; i < forest->num_trees; i++)
                decref_later((PyObject *) forest->list[i]);
        if (num_free_forests < MAX_FREE_FORESTS && forest->max_trees == LIMIT){
                forest_saved[num_free_forests] = forest->list;
                forest_max_trees[num_free_forests++] = forest->max_trees;
        } else
                PyMem_Free(forest->list);
}

BLIST_LOCAL(void)
forest_uninit_now(Forest *forest)
{
        Py_ssize_t i;
        for (i = 0; i < forest->num_trees; i++)
                Py_DECREF((PyObject *) forest->list[i]);
        if (num_free_forests < MAX_FREE_FORESTS && forest->max_trees == LIMIT){
                forest_saved[num_free_forests] = forest->list;
                forest_max_trees[num_free_forests++] = forest->max_trees;
        } else
                PyMem_Free(forest->list);
}

#if 0
BLIST_LOCAL(void)
forest_delete(Forest *forest)
{
        forest_uninit(forest);
        PyMem_Free(forest);
}
#endif

/* Combine the forest into a final BList and delete the forest.
 *
 * forest_finish() assumes that only leaf nodes were passed to forest_append()
 */
static PyBList *forest_finish(Forest *forest)
{
        PyBList *out_tree = NULL; /* The final BList we are building */
        int out_height = 0;       /* It's height */
        int group_height = 1;     /* height of the next group from forest */

        while(forest->num_trees) {
                int n = forest->num_leafs % LIMIT;
                PyBList *group;
                int adj;

                forest->num_leafs /= LIMIT;
                group_height++;

                if (!n) continue;  /* No nodes at this height */

                /* Merge nodes of the same height into 1 node, and
                 * merge it into our output BList.
                 */
                group = blist_new();
                if (group == NULL) {
                        forest_uninit(forest);
                        xdecref_later((PyObject *) out_tree);
                        return NULL;
                }
                group->leaf = 0;
                memcpy(group->children,
                       &forest->list[forest->num_trees - n],
                       sizeof (PyBList *) * n);
                group->num_children = n;
                forest->num_trees -= n;
                adj = blist_underflow(group, n - 1);
                if (out_tree == NULL) {
                        out_tree = group;
                        out_height = group_height - adj;
                } else {
                        out_tree = blist_concat_roots(group, group_height- adj,
                                                      out_tree, out_height,
                                                      &out_height);
                }
        }

        forest_uninit(forest);

        return out_tree;
}

/************************************************************************
 * Functions that rely on forests.
 */

/* Initialize an empty BList from an array of PyObjects in O(n) time */
BLIST_LOCAL(int)
blist_init_from_array(PyBList *self, PyObject **restrict src, Py_ssize_t n)
{
        int i;
        PyBList *final, *cur;
        PyObject **dst;
        PyObject **stop = &src[n];
        PyObject **next;
        Forest forest;

        invariants(self, VALID_ROOT|VALID_RW);

        if (n <= LIMIT) {
                dst = self->children;
                while (src < stop) {
                        Py_INCREF(*src);
                        *dst++ = *src++;
                }
                self->num_children = n;
                self->n = n;
                return _int(0);
        }

        if (forest_init(&forest) == NULL)
                return _int(-1);
        cur = blist_new();
        if (cur == NULL)
                goto error2;
        dst = cur->children;

        while (src < stop) {
                next = &src[LIMIT];
                if (next > stop) next = stop;
                while (src < next) {
                        Py_INCREF(*src);
                        *dst++ = *src++;
                }
                if (src == stop) break;
                
                cur->num_children = LIMIT;
                if (forest_append(&forest, cur) < 0)
                        goto error;
                cur = blist_new();
                if (cur == NULL)
                        goto error2;
                dst = cur->children;
        }

        i = dst - cur->children;
        
        if (i) {
                cur->num_children = i;
                if (forest_append(&forest, cur) < 0) {
                error:
                        Py_DECREF(cur);
                error2:
                        forest_uninit(&forest);
                        return _int(-1);
                }
        } else {
                Py_DECREF(cur);
        }

        final = forest_finish(&forest);
        blist_become_and_consume(self, final);

        ext_index_all((PyBListRoot *) self);
        SAFE_DECREF(final);

        return _int(0);
}

/* Initialize an empty BList from a Python sequence in O(n) time */
BLIST_LOCAL(int)
blist_init_from_seq(PyBList *self, PyObject *b)
{
        PyObject *it;
        PyObject *(*iternext)(PyObject *);
        PyBList *cur, *final;
        Forest forest;

        invariants(self, VALID_ROOT | VALID_RW);

        if (PyBList_Check(b)) {
                /* We can copy other BLists in O(1) time :-) */
                blist_become(self, (PyBList *) b);
                ext_mark(self, 0, DIRTY);
                ext_mark_set_dirty_all((PyBList *) b);
                return _int(0);
        }

        if (PyTuple_CheckExact(b)) {
                PyTupleObject *t = (PyTupleObject *) b;
                return _int(blist_init_from_array(self, t->ob_item,
                                                  PyTuple_GET_SIZE(t)));
        }
#ifndef Py_BUILD_CORE
        if (PyList_CheckExact(b)) {
                PyListObject *l = (PyListObject *) b;
                return _int(blist_init_from_array(self, l->ob_item,
                                                  PyList_GET_SIZE(l)));
        }
#endif
	
	DANGER_BEGIN;
        it = PyObject_GetIter(b);
	DANGER_END;
        if (it == NULL)
                return _int(-1);
        iternext = *Py_TYPE(it)->tp_iternext;

        /* Try common case of len(sequence) <= LIMIT */
        for (self->num_children = 0; self->num_children < LIMIT;
             self->num_children++) {
                PyObject *item;

                DANGER_BEGIN;
                item = iternext(it);
                DANGER_END;

                if (item == NULL) {
                        self->n = self->num_children;
                        if (PyErr_Occurred()) {
                                if (PyErr_ExceptionMatches(PyExc_StopIteration))
                                        PyErr_Clear();
                                else
                                        goto error;
                        }
                        goto done;
                }

                self->children[self->num_children] = item;
        }

        /* No such luck, build bottom-up instead.  The sequence data
         * so far goes in a leaf node. */

        cur = blist_new();
        if (cur == NULL)
                goto error;
        blist_become_and_consume(cur, self);

        if (forest_init(&forest) == NULL) {
                decref_later(it);
                decref_later((PyObject *) cur);
                return _int(-1);
        }

        if (0 > forest_append(&forest, cur))
                goto error2;

        cur = blist_new();
        if (cur == NULL)
                goto error2;

        while (1) {
                PyObject *item;
                DANGER_BEGIN;
                item = iternext(it);
                DANGER_END;
                if (item == NULL) {
                        if (PyErr_Occurred()) {
                                if (PyErr_ExceptionMatches(PyExc_StopIteration))
                                        PyErr_Clear();
                                else
                                        goto error2;
                        }
                        break;
                }

                if (cur->num_children == LIMIT) {
                        if (forest_append(&forest, cur) < 0) goto error2;
                        cur = blist_new();
                        if (cur == NULL)
                                goto error2;
                }

                cur->children[cur->num_children++] = item;
        }

        if (cur->num_children) {
                if (forest_append(&forest, cur) < 0) goto error2;
                cur->n = cur->num_children;
        } else {
                SAFE_DECREF(cur);
        }

        final = forest_finish(&forest);
        blist_become_and_consume(self, final);
        SAFE_DECREF(final);

 done:
        ext_index_all((PyBListRoot*)self);
        decref_later(it);
        return _int(0);

 error2:
        DANGER_BEGIN;
        Py_XDECREF((PyObject *) cur);
        forest_uninit_now(&forest);
        DANGER_END;
 error:
        DANGER_BEGIN;
        Py_DECREF(it);
        DANGER_END;
        blist_CLEAR(self);
        return _int(-1);
}

/* Utility function for performing repr() */
BLIST_LOCAL(int)
blist_repr_r(PyBList *self)
{
        int i;
        PyObject *s;

        invariants(self, VALID_RW|VALID_PARENT);

        if (self->leaf) {
                for (i = 0; i < self->num_children; i++) {
                        if (Py_EnterRecursiveCall(" while getting the repr of a list"))
                                return _int(-1);
                        DANGER_BEGIN;
                        s = PyObject_Repr(self->children[i]);
                        DANGER_END;
                        Py_LeaveRecursiveCall();
                        if (s == NULL)
                                return _int(-1);
                        Py_DECREF(self->children[i]);
                        self->children[i] = s;
                }
        } else {
                for (i = 0; i < self->num_children; i++) {
                        PyBList *child = blist_PREPARE_WRITE(self, i);
                        int status = blist_repr_r(child);
                        if (status < 0)
                                return _int(status);
                }
        }

        return _int(0);
}

PyObject *
ext_make_clean_set(PyBListRoot *root, Py_ssize_t i, PyObject *v)
{
        PyBList *p = (PyBList *) root;
        PyBList *next;
        int k;
        Py_ssize_t so_far, offset = 0;
        PyObject *old_value;
        int did_mark = 0;

        while (!p->leaf) {
                blist_locate(p, i, (PyObject **) &next, &k, &so_far);
                if (Py_REFCNT(next) <= 1)
                        p = next;
                else {
                        p = blist_PREPARE_WRITE(p, k);
                        if (!did_mark) {
                                ext_mark((PyBList *) root, offset, DIRTY);
                                did_mark = 1;
                        }
                }
                assert(i >= so_far);
                i -= so_far;
                offset += so_far;
        }

        if (!root->leaf)
                ext_mark_clean(root, offset, p, 1);

        old_value = p->children[i];
        p->children[i] = v;
        return old_value;
}

PyObject *
blist_ass_item_return_slow(PyBListRoot *root, Py_ssize_t i, PyObject *v)
{
        Py_ssize_t dirty_offset, ioffset;
        PyObject *rv;
        assert(i >= 0);
        assert(i < root->n);
        invariants(root, VALID_RW);
        ioffset = i / INDEX_FACTOR;

        if (root->leaf || ext_is_dirty(root, i, &dirty_offset)
            || !GET_BIT(root->setclean_list, ioffset)) {
                rv = ext_make_clean_set(root, i, v);
        } else {
                Py_ssize_t offset = root->offset_list[ioffset];
                PyBList *p = root->index_list[ioffset];
                assert(i >= offset);
                assert(p);
                assert(p->leaf);
                if (i < offset + p->n) {
                good:
                        /* If we're here, Py_REFCNT(p) == 1, most likely.
			 * However, we can't assert() it since there are two
			 * exceptions:
			 * 1) The user may have acquired a ref via gc, or
			 * 2) an iterator may have a reference
			 *
			 * If it's an iterator, we can go ahead and make the
			 * change anyway since we're not changing the length
			 * of the list.
			 */
                        rv = p->children[i - offset];
                        p->children[i - offset] = v;
                        if (dirty_offset >= 0)
                                ext_make_clean(root, dirty_offset);
                } else if (ext_is_dirty(root,i + INDEX_FACTOR,&dirty_offset)
                        || !GET_BIT(root->setclean_list, ioffset+1)) {
                        rv = ext_make_clean_set(root, i, v);
                } else {
                        ioffset++;
                        assert(ioffset < root->index_length);
                        offset = root->offset_list[ioffset];
                        p = root->index_list[ioffset];
                        assert(p);
                        assert(p->leaf);
                        assert(i < offset + p->n);

                        goto good;
                }
        }

        return _ob(rv);
}

BLIST_LOCAL_INLINE(PyObject *)
blist_ass_item_return(PyBList *self, Py_ssize_t i, PyObject *v)
{
        Py_INCREF(v);
        if (self->leaf) {
                PyObject *rv = self->children[i];
                self->children[i] = v;
                return rv;
        }

        return blist_ass_item_return2((PyBListRoot*)self, i, v);
}

#ifndef Py_BUILD_CORE
BLIST_LOCAL(PyObject *)
blist_richcompare_list(PyBList *v, PyListObject *w, int op)
{
        int cmp;
        PyObject *ret, *item;
        Py_ssize_t i;
        int v_stopped = 0;
        int w_stopped = 0;

        invariants(v, VALID_RW);

        if (v->n != PyList_GET_SIZE(w) && (op == Py_EQ || op == Py_NE)) {
                /* Shortcut: if the lengths differ, the lists differ */
                PyObject *res;
                if (op == Py_EQ) {
                false:
                        res = Py_False;
                } else {
                true:
                        res = Py_True;
                }
                Py_INCREF(res);
                return _ob(res);
        }

        /* Search for the first index where items are different */
        i = 0;
        ITER(v, item, {
                if (i >= PyList_GET_SIZE(w)) {
                        w_stopped = 1;
                        break;
                }

                DANGER_BEGIN;
                cmp = PyObject_RichCompareBool(item, w->ob_item[i], Py_EQ);
                DANGER_END;

                if (cmp < 0) {
                        ITER_CLEANUP();
                        return _ob(NULL);
                } else if (!cmp) {
                        if (op == Py_EQ) { ITER_CLEANUP(); goto false; }
                        if (op == Py_NE) { ITER_CLEANUP(); goto true; }

                        /* Last RichComparebool may have modified the list */
                        if (i >= PyList_GET_SIZE(w)) {
                                w_stopped = 1;
                                break;
                        }

                        DANGER_BEGIN;
                        ret = PyObject_RichCompare(item, w->ob_item[i], op);
                        DANGER_END;
                        ITER_CLEANUP();
                        return ret;
                }
                i++;
        });

        if (!w_stopped) {
                v_stopped = 1;
                if (i >= PyList_GET_SIZE(w))
                        w_stopped = 1;
        }

        /* No more items to compare -- compare sizes */
        switch (op) {
        case Py_LT: cmp = v_stopped && !w_stopped; break;
        case Py_LE: cmp = v_stopped; break;
        case Py_EQ: cmp = v_stopped == w_stopped; break;
        case Py_NE: cmp = v_stopped != w_stopped; break;
        case Py_GT: cmp = !v_stopped && w_stopped; break;
        case Py_GE: cmp = w_stopped; break;
        default:
                /* cannot happen */
                PyErr_BadInternalCall();
                return _ob(NULL);
        }

        if (cmp) goto true;
        else goto false;
}
#endif

BLIST_LOCAL(PyObject *)
blist_richcompare_item(int c, int op, PyObject *item1, PyObject *item2)
{
        PyObject *ret;

        if (c < 0)
                return NULL;
        if (!c) {
                if (op == Py_EQ)
                        Py_RETURN_FALSE;
                if (op == Py_NE)
                        Py_RETURN_TRUE;
                DANGER_BEGIN;
                ret = PyObject_RichCompare(item1, item2, op);
                DANGER_END;
                return ret;
        }

        /* Impossible to get here */
        assert(0);
        return NULL;
}

static PyObject *blist_richcompare_len(PyBList *v, PyBList *w, int op)
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
        iter_t it1, it2;
        int c;
        PyBList *leaf1, *leaf2;

        iter_init(&it1, v);
        iter_init(&it2, w);

        leaf1 = it1.leaf;
        leaf2 = it2.leaf;
        do {
                if (it1.i < leaf1->num_children) {
                        item1 = leaf1->children[it1.i++];
                } else {
                        item1 = iter_next(&it1);
                        leaf1 = it1.leaf;
                        if (item1 == NULL) {
                        compare_len:
                                iter_cleanup(&it1);
                                iter_cleanup(&it2);
                                return blist_richcompare_len(v, w, op);
                        }
                }

                if (it2.i < leaf2->num_children) {
                        item2 = leaf2->children[it2.i++];
                } else {
                        item2 = iter_next(&it2);
                        leaf2 = it2.leaf;
                        if (item2 == NULL)
                                goto compare_len;
                }

                DANGER_BEGIN;
                c = PyObject_RichCompareBool(item1, item2, Py_EQ);
                DANGER_END;
        } while (c >= 1);

        iter_cleanup(&it1);
        iter_cleanup(&it2);
        return blist_richcompare_item(c, op, item1, item2);
}

BLIST_LOCAL(PyObject *)
blist_richcompare_blist(PyBList *v, PyBList *w, int op)
{
        int i, c;

        if (v->n != w->n) {
                /* Shortcut: if the lengths differ, the lists differ */
                if (op == Py_EQ) {
                        Py_RETURN_FALSE;
                } else if (op == Py_NE) {
                        Py_RETURN_TRUE;
                }
        }

        if (!v->leaf || !w->leaf)
                return blist_richcompare_slow(v, w, op);

        for (i = 0; i < v->num_children && i < w->num_children; i++) {
                DANGER_BEGIN;
                c = PyObject_RichCompareBool(v->children[i],
                                             w->children[i],Py_EQ);
                DANGER_END;
                if (c < 1)
                        return blist_richcompare_item(c, op, v->children[i],
                                                      w->children[i]);
        }
        return blist_richcompare_len(v, w, op);

}

/* Swiped from listobject.c */
/* Reverse a slice of a list in place, from lo up to (exclusive) hi. */
BLIST_LOCAL_INLINE(void)
reverse_slice(PyObject **restrict lo, PyObject **restrict hi)
{
        PyObject *t;
        assert(lo && hi);

        if (lo == hi) return;
        
        /* Use Duff's Device
         * http://en.wikipedia.org/wiki/Duff%27s_device
         */
        
        --hi;
        
        switch ((hi - lo) & 31) {
                case 31: do { t = *lo; *lo++ = *hi; *hi-- = t;
                case 30: case 29: t = *lo; *lo++ = *hi; *hi-- = t;
                case 28: case 27: t = *lo; *lo++ = *hi; *hi-- = t;
                case 26: case 25: t = *lo; *lo++ = *hi; *hi-- = t;
                case 24: case 23: t = *lo; *lo++ = *hi; *hi-- = t;
                case 22: case 21: t = *lo; *lo++ = *hi; *hi-- = t;
                case 20: case 19: t = *lo; *lo++ = *hi; *hi-- = t;
                case 18: case 17: t = *lo; *lo++ = *hi; *hi-- = t;
                case 16: case 15: t = *lo; *lo++ = *hi; *hi-- = t;
                case 14: case 13: t = *lo; *lo++ = *hi; *hi-- = t;
                case 12: case 11: t = *lo; *lo++ = *hi; *hi-- = t;
                case 10: case 9: t = *lo; *lo++ = *hi; *hi-- = t;
                case 8: case 7: t = *lo; *lo++ = *hi; *hi-- = t;
                case 6: case 5: t = *lo; *lo++ = *hi; *hi-- = t;
                case 4: case 3: t = *lo; *lo++ = *hi; *hi-- = t;
                case 2: case 1: t = *lo; *lo++ = *hi; *hi-- = t;
                case 0: ;
                } while (lo < hi);
        }
}

/* self *= 2 */
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

BLIST_LOCAL(int)
blist_extend(PyBList *self, PyObject *other)
{
        int err;
        PyBList *bother = NULL;

        invariants(self, VALID_PARENT|VALID_RW);

        if (PyBList_Check(other)) {
                err = blist_extend_blist(self, (PyBList *) other);
                goto done;
        }

        bother = blist_root_new();
        err = blist_init_from_seq(bother, other);
        if (err < 0)
                goto done;
        err = blist_extend_blist(self, bother);
        ext_mark(self, 0, DIRTY);

 done:
        SAFE_XDECREF(bother);
        return _int(err);
}

BLIST_LOCAL(PyObject *)
blist_repeat(PyBList *self, Py_ssize_t n)
{
        Py_ssize_t mask;
        PyBList *power = NULL, *rv, *remainder = NULL;
        Py_ssize_t remainder_n = 0;

        invariants(self, VALID_PARENT);

        if (n <= 0 || self->n == 0)
                return _ob((PyObject *) blist_root_new());

        if ((self->n * n) / n != self->n)
                return _ob(PyErr_NoMemory());

        rv = blist_root_new();
        if (rv == NULL)
                return _ob(NULL);

        if (n == 1) {
                blist_become(rv, self);
                ext_mark(rv, 0, DIRTY);
                return _ob((PyObject *) rv);
        }

        if (self->num_children > HALF)
                blist_become(rv, self);
        else {
                Py_ssize_t fit, fitn, so_far;

                rv->leaf = self->leaf;
                fit = LIMIT / self->num_children;
                if (fit > n) fit = n;
                fitn = fit * self->num_children;
                xcopyref(rv, 0, self, 0, self->num_children);
                so_far = self->num_children;
                while (so_far*2 < fitn) {
                        xcopyref(rv, so_far, rv, 0, so_far);
                        so_far *= 2;
                }
                xcopyref(rv, so_far, rv, 0, (fitn - so_far));

                rv->num_children = fitn;
                rv->n = self->n * fit;
                check_invariants(rv);

                if (fit == n) {
                        ext_mark(rv, 0, DIRTY);
                        return _ob((PyObject *) rv);
                }

                remainder_n = n % fit;
                n /= fit;

                if (remainder_n) {
                        remainder = blist_root_new();
                        if (remainder == NULL)
                                goto error;
                        remainder->n = self->n * remainder_n;
                        remainder_n *= self->num_children;
                        remainder->leaf = self->leaf;
                        xcopyref(remainder, 0, rv, 0, remainder_n);
                        remainder->num_children = remainder_n;
                        check_invariants(remainder);
                }
        }

        if (n == 0)
                goto do_remainder;

        power = rv;
        rv = blist_root_new();
        if (rv == NULL) {
                SAFE_XDECREF(remainder);
        error:
                SAFE_DECREF(power);
                return _ob(NULL);
        }

        if (n & 1)
                blist_become(rv, power);

        for (mask = 2; mask <= n; mask <<= 1) {
                blist_double(power);
                if (mask & n)
                        blist_extend_blist(rv, power);
        }
        SAFE_DECREF(power);

 do_remainder:

        if (remainder) {
                blist_extend_blist(rv, remainder);
                SAFE_DECREF(remainder);
        }

        check_invariants(rv);
        ext_mark(rv, 0, DIRTY);
        return _ob((PyObject *) rv);
}

BLIST_LOCAL(void)
blist_reverse(PyBList *self)
{
        invariants(self, VALID_PARENT|VALID_RW);

        reverse_slice(self->children,
                      &self->children[self->num_children]);
        if (!self->leaf) {
                int i;
                for (i = 0; i < self->num_children; i++) {
                        PyBList *restrict p = blist_PREPARE_WRITE(self, i);
                        blist_reverse(p);
                }
        }

        _void();
}

BLIST_LOCAL(int)
blist_append(PyBList *self, PyObject *v)
{
        PyBList *overflow;
        PyBList *p;

        invariants(self, VALID_ROOT|VALID_RW);

        if (self->n == PY_SSIZE_T_MAX) {
                PyErr_SetString(PyExc_OverflowError,
                                "cannot add more objects to list");
                return _int(-1);
        }

        for (p = self; !p->leaf; p= (PyBList*)p->children[p->num_children-1]) {
                if (p != self && Py_REFCNT(p) > 1)
                        goto cleanup_and_slow;
                p->n++;
        }

        if (p->num_children == LIMIT || (p != self && Py_REFCNT(p) > 1)) {
                PyBList *p2;
        cleanup_and_slow:
                for (p2 = self; p2 != p;
                     p2 = (PyBList*)p2->children[p2->num_children-1])
                        p2->n--;
                goto slow;
        }
        
        p->children[p->num_children++] = v;
        p->n++;
        Py_INCREF(v);

        if ((self->n-1) % INDEX_FACTOR == 0) 
                ext_mark(self, 0, DIRTY);
#ifdef Py_DEBUG
        else
                ((PyBListRoot*)self)->last_n++;
#endif
        check_invariants(self);
        return _int(0);
        
 slow:
        overflow = ins1(self, self->n, v);
        if (overflow)
                blist_overflow_root(self, overflow);
        ext_mark(self, 0, DIRTY);

        return _int(0);
}

/************************************************************************
 * Sorting code
 *
 * Bits and pieces swiped from Python's listobject.c
 *
 * Invariant: In case of an error, any sort function returns the list
 * to a valid state before returning.  "Valid" means that each item
 * originally in the list is still in the list.  No removals, no
 * additions, and no changes to the reference counters.
 *
 ************************************************************************/

/* If COMPARE is NULL, calls PyObject_RichCompareBool with Py_LT, else calls
 * islt.  This avoids a layer of function call in the usual case, and
 * sorting does many comparisons.
 * Returns -1 on error, 1 if x < y, 0 if x >= y.
 */

#ifndef Py_DEBUG
#define ISLT(X, Y, COMPARE) ((COMPARE) == NULL ?                        \
                             PyObject_RichCompareBool(X, Y, Py_LT) :    \
                             islt(X, Y, COMPARE))
#else
BLIST_LOCAL(int)
_rich_compare_bool(PyObject *x, PyObject *y)
{
        int ret;
        DANGER_BEGIN;
        ret = PyObject_RichCompareBool(x, y, Py_LT);
        DANGER_END;
        return ret;
}

#define ISLT(X, Y, COMPARE) ((COMPARE) == NULL ?                        \
                             _rich_compare_bool(X, Y) :    \
                             islt(X, Y, COMPARE))
#endif

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
                DANGER_BEGIN;
                x = PyObject_CallFunctionObjArgs(compare->keyfunc, x, NULL);
                DANGER_END;
                if (x == NULL) return -1;
                DANGER_BEGIN;
                y = PyObject_CallFunctionObjArgs(compare->keyfunc, y, NULL);
                DANGER_END;
                if (y == NULL) {
                        DANGER_BEGIN;
                        Py_DECREF(x);
                        DANGER_END;
                        return -1;
                }
        } else {
                Py_INCREF(x);
                Py_INCREF(y);
        }

        if (compare->compare == NULL) {
                DANGER_BEGIN;
                i = PyObject_RichCompareBool(x, y, Py_LT);
                Py_DECREF(x);
                Py_DECREF(y);
                DANGER_END;
                if (i < 0)
                        return -1;
                return i;
        }

        DANGER_BEGIN;
        args = PyTuple_New(2);
        DANGER_END;

        if (args == NULL) {
                DANGER_BEGIN;
                Py_DECREF(x);
                Py_DECREF(y);
                DANGER_END;
                return -1;
        }

        PyTuple_SET_ITEM(args, 0, x);
        PyTuple_SET_ITEM(args, 1, y);
        DANGER_BEGIN;
        res = PyObject_Call(compare->compare, args, NULL);
        Py_DECREF(args);
        DANGER_END;
        if (res == NULL)
                return -1;
        if (!PyInt_CheckExact(res)) {
                PyErr_Format(PyExc_TypeError,
                             "comparison function must return int, not %.200s",
                             Py_TYPE(res)->tp_name);
                Py_DECREF(res);
                return -1;
        }
        i = PyInt_AsLong(res);
        Py_DECREF(res);
        return i < 0;
}

#define INSERTION_THRESH 0
#define BINARY_THRESH 10

#if 0
/* Compare X to Y via "<".  Goto "fail" if the comparison raises an
   error.  Else "k" is set to true iff X<Y, and an "if (k)" block is
   started.  It makes more sense in context <wink>.  X and Y are PyObject*s.
*/
#define IFLT(X, Y) if ((k = ISLT(X, Y, compare)) < 0) goto fail;  \
                   if (k)

#define SWAP(x, y) {PyObject *_tmp = x; x = y; y = _tmp;}
#define TESTSWAP(x, y) IFLT(y, x) SWAP(x, y)

BLIST_LOCAL(int)
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

#if 0
static int insertion_sort(PyObject **array, int n, const compare_t *compare)
{
        int i, j;
        PyObject *tmp;
        for (i = 1; i < n; i++) {
                tmp = array[i];
                for (j = i; j >= 1; j--) {
                        int c = ISLT(tmp, array[j-1], compare);
                        if (c < 0) {
                                array[j] = tmp;
                                return -1;
                        }
                        if (c == 0)
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

                c = ISLT(tmp, array[i-1], compare);
                if (c < 0)
                        return -1;
                if (c == 0)
                        continue;

                low = 0;
                high = i-1;

                while (low < high) {
                        mid = low + (high - low)/2;
                        c = ISLT(tmp, array[mid], compare);
                        if (c < 0)
                                return -1;
                        if (c == 0)
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
#endif
#endif

BLIST_LOCAL(int)
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
                c = ISLT(*right, *left, compare);
                if (c < 0)
                        return -1;
                if (c)
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
                c = ISLT(*right, *left, compare);
                if (c < 0) {
                        ret = -1;
                        goto done;
                }
                if (c == 0)
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

BLIST_LOCAL(int)
gallop_sort(PyObject **array, int n, const compare_t *compare)
{
        int i, j;
        int run_start = 0, run_dir = 2;
        PyObject **runs[LIMIT/RUN_THRESH+2];
        int ns[LIMIT/RUN_THRESH+2];
        int num_runs = 0;
        PyObject **run = array;

        if (n < 2) return 0;

        for (i = 1; i < n; i++) {
                int c = ISLT(array[i], array[i-1], compare);
                assert(c < 0 || c == 0 || c == 1);
                if (c == run_dir)
                        continue;
                if (c < 0)
                        return -1;
                if (run_start == i-1)
                        run_dir = c;
                else if (i - run_start >= RUN_THRESH) {
                        if (run_dir > 0)
                                reverse_slice(run, &array[i]);
                        runs[num_runs] = run;
                        ns[num_runs++] = i - run_start;
                        run = &array[i];
                        run_start = i;
                        run_dir = 2;
                } else {
                        int low = run - array;
                        int high = i-1;
                        int mid;
                        PyObject *tmp = array[i];

                        /* XXX: Is this a stable sort? */
                        /* XXX: In both directions? */

                        while (low < high) {
                                mid = low + (high - low)/2;
                                c = ISLT(tmp, array[mid], compare);
                                assert(c < 0 || c == 0 || c == 1);
                                if (c == run_dir)
                                        low = mid+1;
                                else if (c < 0)
                                        return -1;
                                else
                                        high = mid;
                        }

                        for (j = i; j > low; j--)
                                array[j] = array[j-1];

                        array[low] = tmp;
                }
        }

        if (run_dir > 0)
                reverse_slice(run, &array[i]);
        runs[num_runs] = run;
        ns[num_runs++] = i - run_start;

        while(num_runs > 1) {
                for (i = 0; i < num_runs/2; i++) {
                        int total = ns[2*i] + ns[2*i+1];
                        if (0 > mini_merge(runs[2*i], ns[2*i], total,
                                           compare)) {
                                /* List valid due to invariants */
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

#if 0
BLIST_LOCAL(int)
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
#endif

#if PY_MAJOR_VERSION < 3
BLIST_LOCAL(int)
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
#endif

BLIST_LOCAL(void)
do_fast_merge(PyBList **restrict out, PyBList **in1, PyBList **in2,
              int n1, int n2)
{
        memcpy(out, in1, sizeof(PyBList *) * n1);
        memcpy(&out[n1], in2, sizeof(PyBList *) * n2);
}

BLIST_LOCAL(int)
try_fast_merge(PyBList **restrict out, PyBList **in1, PyBList **in2,
               Py_ssize_t n1, Py_ssize_t n2,
               const compare_t *compare, int *err)
{
        int c;
        PyBList *end;

        end = in1[n1-1];

        c = ISLT(end->children[end->num_children-1],
                 in2[0]->children[0], compare);

        if (c < 0) {
        error:
                *err = -1;
                do_fast_merge(out, in1, in2, n1, n2);
                return 1;
        } else if (c) {
                do_fast_merge(out, in1, in2, n1, n2);
                return 1;
        }

        end = in2[n2-1];
        
        c = ISLT(end->children[end->num_children-1],
                 in1[0]->children[0], compare);
        if (c < 0)
                goto error;
        else if (c) {
                do_fast_merge(out, in2, in1, n2, n1);
                return 1;
        }

        return 0;                
}

/* Merge two sorted forests.  Each forest must contain on leafs.  The
 * forests will be deleted at the end. */
BLIST_LOCAL(int)
sub_merge(PyBList **restrict out, PyBList **in1, PyBList **in2, 
	  Py_ssize_t n1, Py_ssize_t n2,
          const compare_t *compare, int *err)
{
        int c;
	Py_ssize_t i, j;
        PyBList *restrict leaf1, *restrict leaf2, *restrict output;
        int leaf1_i = 0, leaf2_i = 0;
        Py_ssize_t nout = 0;

        if (try_fast_merge(out, in1, in2, n1, n2, compare, err))
                return n1 + n2;
        
        leaf1 = in1[leaf1_i++];
        leaf2 = in2[leaf2_i++];

        i = 0; /* Index into leaf 1 */
        j = 0; /* Index into leaf 2 */

        output = blist_new();

        while ((leaf1_i < n1 || i < leaf1->num_children)
               && (leaf2_i < n2 || j < leaf2->num_children)) {

                /* Check if we need to get a new input leaf node */
                if (i == leaf1->num_children) {
                        leaf1->num_children = 0;
                        SAFE_DECREF(leaf1);
                        leaf1 = in1[leaf1_i++];
                        i = 0;
                }

                if (j == leaf2->num_children) {
                        leaf2->num_children = 0;
                        SAFE_DECREF(leaf2);
                        leaf2 = in2[leaf2_i++];
                        j = 0;
                }

                assert (i < leaf1->num_children);
                assert (j < leaf2->num_children);
                
                /* Check if we have filled up an output leaf node */
                if (output->n == LIMIT) {
                        out[nout++] = output;
                        output = blist_new();
                }

                /* Figure out which input leaf has the lower element */
                c = ISLT(leaf2->children[j], leaf1->children[i], compare);
                if (c < 0) {
                        *err = -1;
                        goto done;
                }
                if (c == 0) {
                        output->children[output->num_children++]
                                = leaf1->children[i++];
                } else {
                        output->children[output->num_children++]
                                = leaf2->children[j++];
                }

                output->n++;
        }

 done:
        /* Append our partially-complete output leaf node to the forest */
        nout = append_and_squish(out, nout, output);

        /* Append a partially-consumed input leaf node, if one exists */
        if (i < leaf1->num_children) {
                shift_left(leaf1, i, i);
                leaf1->num_children -= i;
                leaf1->n -= i;
                nout = append_and_squish(out, nout, leaf1);
        } else {
                leaf1->num_children = 0;
                SAFE_DECREF(leaf1);
        }

        if (j < leaf2->num_children) {
                shift_left(leaf2, j, j);
                leaf2->num_children -= j;
                leaf2->n -= j;
                nout = append_and_squish(out, nout, leaf2);
        } else {
                leaf2->num_children = 0;
                SAFE_DECREF(leaf2);
        }
                
        nout = balance_last_2(out, nout);

        if (leaf1_i < n1) {
                /* Append the rest of any input forests still has nodes. */
                memcpy(&out[nout], &in1[leaf1_i],
                       sizeof(PyBList *) * (n1 - leaf1_i));
                nout += n1 - leaf1_i;
        }
        
        if (leaf2_i < n2) {
                memcpy(&out[nout], &in2[leaf2_i],
                       sizeof(PyBList *) * (n2 - leaf2_i));
                nout += n2 - leaf2_i;
        }

        for (i = nout; i < n1+n2; i++)
                out[i] = NULL;

        assert(nout <= n1+n2);

        return nout;
}

BLIST_LOCAL(Py_ssize_t)
sub_sort(PyBList **restrict scratch, PyBList **in, const compare_t *compare,
         Py_ssize_t n, int *err)
{
        Py_ssize_t half, n1, n2;

        if (!n) return 0;
        if (n == 1) {
                *scratch = *in;
                return 1;
        }

        half = n / 2;

        n1 = sub_sort(scratch, in, compare, half, err);
        n2 = sub_sort(&scratch[half], &in[half], compare, n-half, err);

        n = sub_merge(scratch, in, &in[half], n1, n2, compare, err);
        memcpy(in, scratch, sizeof(PyBList *) * n);
        return n;
}

BLIST_LOCAL(Py_ssize_t)
sort(PyBList *self, const compare_t *compare) 
{
        Forest builder, forest_tmp;
        PyBList *other, *leaf;
        PyBList **leafs, **scratch;
        int err=0;
	Py_ssize_t i, leafs_n = 0;

        leafs = PyMem_New(PyBList *, self->n / HALF + 1);
        if (!leafs)
                return -1;
        scratch = PyMem_New(PyBList *, self->n / HALF + 1);
        if (!scratch) {
                PyMem_Free(leafs);
                return -1;
        }
        
        forest_init(&forest_tmp);
        Py_INCREF(self); /* forest_append steals a reference */
        forest_append(&forest_tmp, self);
        while (forest_tmp.num_trees) {
                leaf = forest_get_leaf(&forest_tmp);
                leafs[leafs_n++] = leaf;
        }
        forest_uninit(&forest_tmp);
        
        for (i = 0; i < leafs_n && !err; i++) {
                leaf = leafs[i];
                err |= gallop_sort(leaf->children, leaf->num_children,compare);
        }
        
        leafs_n = sub_sort(scratch, leafs, compare, leafs_n, &err);
        PyMem_Free(scratch);
        
        forest_init(&builder);
        for (i = 0; i < leafs_n && leafs[i]; i++) {
                leafs[i]->n = leafs[i]->num_children;
                forest_append(&builder, leafs[i]);
        }
        PyMem_Free(leafs);
        other = forest_finish(&builder);
        assert(self->num_children == 0);
        blist_become_and_consume(self, other);
        SAFE_DECREF(other);
        return err;
}

/************************************************************************
 * Section for functions callable directly by the interpreter.
 *
 * Each of these functions are marked with VALID_USER for debug mode.
 *
 * If they, or any function they call, makes calls to decref_later,
 * they must call decref_flush() just before returning.
 *
 * These functions must not be called directly by other blist
 * functions.  They should *only* be called by the interpreter, to
 * ensure that decref_flush() is the last thing called before
 * returning to the interpreter.
 */

BLIST_PYAPI(PyObject *)
py_blist_root_tp_new(PyTypeObject *subtype, PyObject *args, PyObject *kwds)
{
        PyBList *self;

        if (subtype == &PyRootBList_Type)
                return (PyObject *) blist_root_new();

        self = (PyBList *) subtype->tp_alloc(subtype, 0);
        if (self == NULL)
                return NULL;
        self->children = PyMem_New(PyObject *, LIMIT);
        if (self->children == NULL) {
                subtype->tp_free(self);
                return NULL;
        }

        self->leaf = 1;
        ext_init((PyBListRoot *)self);

        return (PyObject *) self;
}

/* Should only be used by the unpickler */
BLIST_PYAPI(PyObject *)
py_blist_internal_tp_new(PyTypeObject *subtype, PyObject *args, PyObject *kwds)
{
        assert (subtype == &PyBList_Type);
        return (PyObject *) blist_new();
}

/* Should only be used by the unpickler */
BLIST_PYAPI(int)
py_blist_internal_init(PyObject *oself, PyObject *args, PyObject *kw)
{
        return 0;
}

BLIST_PYAPI(int)
py_blist_init(PyObject *oself, PyObject *args, PyObject *kw)
{
        int ret;
        PyObject *arg = NULL;
        static char *kwlist[] = {"sequence", 0};
        int err;
        PyBList *self;

        invariants(oself, VALID_USER|VALID_DECREF);
        self = (PyBList *) oself;

        DANGER_BEGIN;
        err = PyArg_ParseTupleAndKeywords(args, kw, "|O:list", kwlist, &arg);
        DANGER_END;
        if (!err)
                return _int(-1);

        if (self->n) {
                blist_CLEAR(self);
                ext_dealloc((PyBListRoot *) self);
        }

        if (arg == NULL)
                return _int(0);

        ret = blist_init_from_seq(self, arg);

        decref_flush(); /* Needed due to blist_CLEAR() call */
        return _int(ret);
}

BLIST_PYAPI(PyObject *)
py_blist_richcompare(PyObject *v, PyObject *w, int op)
{
        PyObject *rv;

        if (!PyRootBList_Check(v)) {
        not_implemented:
                Py_INCREF(Py_NotImplemented);
                return Py_NotImplemented;
        }

        invariants((PyBList *) v, VALID_USER|VALID_DECREF);
        if (PyRootBList_Check(w)) {
                rv = blist_richcompare_blist((PyBList *)v, (PyBList *)w, op);
                decref_flush();
                return _ob(rv);
        }
#ifndef Py_BUILD_CORE
        if (PyList_Check(w)) {
                rv = blist_richcompare_list((PyBList*)v, (PyListObject*)w, op);
                decref_flush();
                return _ob(rv);
        }
#endif
        _void();
        goto not_implemented;
}

BLIST_PYAPI(int)
py_blist_traverse(PyObject *oself, visitproc visit, void *arg)
{
        PyBList *self;
        int i;

        assert(PyBList_Check(oself));
        self = (PyBList *) oself;

        for (i = 0; i < self->num_children; i++) {
                if (self->children[i] != NULL)
                        Py_VISIT(self->children[i]);
        }
        return 0;
}

BLIST_PYAPI(int)
py_blist_clear(PyObject *oself)
{
        PyBList *self;

        invariants(oself, VALID_USER|VALID_RW|VALID_DECREF);
        self = (PyBList *) oself;

        blist_forget_children(self);
        self->n = 0;
        self->leaf = 1;
        ext_dealloc((PyBListRoot *) self);

        decref_flush();
        return _int(0);
}

#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION >= 6 || PY_MAJOR_VERSION >= 3
#define py_blist_nohash PyObject_HashNotImplemented
#else
BLIST_PYAPI(long)
py_blist_nohash(PyObject *self)
{
        PyErr_SetString(PyExc_TypeError, "list objects are unhashable");
        return -1;
}
#endif

BLIST_PYAPI(void)
py_blist_dealloc(PyObject *oself)
{
        int i;
        PyBList *self;

        assert(PyBList_Check(oself));
        self = (PyBList *) oself;

        PyObject_GC_UnTrack(self);

        Py_TRASHCAN_SAFE_BEGIN(self)

        for (i = 0; i < self->num_children; i++)
                Py_XDECREF(self->children[i]);

        if (PyRootBList_Check(self)) {
                ext_dealloc((PyBListRoot *) self);
                if (PyRootBList_CheckExact(self)
                    && num_free_ulists < MAXFREELISTS)
                        free_ulists[num_free_ulists++] = self;
                else
                        goto free_blist;
        } else if (Py_TYPE(self) == &PyBList_Type
                   && num_free_lists < MAXFREELISTS)
                free_lists[num_free_lists++] = self;
        else {
        free_blist:
                PyMem_Free(self->children);
                Py_TYPE(self)->tp_free((PyObject *)self);
        }

        Py_TRASHCAN_SAFE_END(self);
}

BLIST_PYAPI(int)
py_blist_ass_item(PyObject *oself, Py_ssize_t i, PyObject *v)
{
        PyObject *old_value;
        PyBList *self;

        invariants(oself, VALID_USER|VALID_RW|VALID_DECREF);

        self = (PyBList *) oself;

        if (i >= self->n || i < 0) {
                set_index_error();
                return _int(-1);
        }

        if (v == NULL) {
                blist_delitem(self, i);
                ext_mark(self, 0, DIRTY);
                decref_flush();
                return _int(0);
        }

        old_value = blist_ass_item_return(self, i, v);
        Py_XDECREF(old_value);
        return _int(0);
}

BLIST_PYAPI(int)
py_blist_ass_slice(PyObject *oself, Py_ssize_t ilow, Py_ssize_t ihigh,
                   PyObject *v)
{
        Py_ssize_t net;
        PyBList *other, *left, *right, *self;

        invariants(oself, VALID_RW|VALID_USER|VALID_DECREF);

        self = (PyBList *) oself;

        if (ilow < 0) ilow = 0;
        else if (ilow > self->n) ilow = self->n;
        if (ihigh < ilow) ihigh = ilow;
        else if (ihigh > self->n) ihigh = self->n;

        if (!v) {
                blist_delslice(self, ilow, ihigh);
                ext_mark(self, 0, DIRTY);
                decref_flush();
                return _int(0);
        }

        if (PyRootBList_Check(v) && (PyObject *) self != v) {
                other = (PyBList *) v;
                Py_INCREF(other);
                ext_mark_set_dirty_all(other);
        } else {
                other = blist_root_new();
                if (v) {
                        int err = blist_init_from_seq(other, v);
                        if (err < 0) {
                                decref_later((PyObject *) other);
                                decref_flush();
                                return _int(-1);
                        }
                }
        }

        net = other->n - (ihigh - ilow);

        /* Special case small lists */
        if (self->leaf && other->leaf && (self->n + net <= LIMIT))
        {
                Py_ssize_t i;

                for (i = ilow; i < ihigh; i++)
                        decref_later(self->children[i]);

                if (net >= 0)
                        shift_right(self, ihigh, net);
                else
                        shift_left(self, ihigh, -net);
                self->num_children += net;
                copyref(self, ilow, other, 0, other->n);
                SAFE_DECREF(other);
                blist_adjust_n(self);
                decref_flush();
                return _int(0);
        }

        left = self;
        right = blist_root_copy(self);
        blist_delslice(left, ilow, left->n);
        blist_delslice(right, 0, ihigh);
        blist_extend_blist(left, other); /* XXX check return values */
        blist_extend_blist(left, right);

        ext_mark(self, 0, DIRTY);

        SAFE_DECREF(other);
        SAFE_DECREF(right);

        decref_flush();

        return _int(0);
}

BLIST_PYAPI(int)
py_blist_ass_subscript(PyObject *oself, PyObject *item, PyObject *value)
{
        PyBList *self;

        invariants(oself, VALID_USER|VALID_RW|VALID_DECREF);

        self = (PyBList *) oself;

        if (PyIndex_Check(item)) {
                Py_ssize_t i;
                PyObject *old_value;

                if (PyLong_CheckExact(item)) {
                        i = PyInt_AsSsize_t(item);
                        if (i == -1 && PyErr_Occurred()) {
                                PyErr_Clear();
                                goto number;
                        }
                } else {
                number:
                        i = PyNumber_AsSsize_t(item, PyExc_IndexError);
                        if (i == -1 && PyErr_Occurred())
                                return _int(-1);
                }
                if (i < 0)
                        i += self->n;

                if (i >= self->n || i < 0) {
                        set_index_error();
                        return _int(-1);
                }

                if (self->leaf) {
                        /* Speed up common cases */

                        old_value = self->children[i];
                        if (value == NULL) {
                                shift_left(self, i+1, 1);
                                self->num_children--;
                                self->n--;
                        } else {
                                self->children[i] = value;
                                Py_INCREF(value);
                        }
                        DANGER_BEGIN;
                        Py_DECREF(old_value);
                        DANGER_END;
                        return _int(0);
                }

                if (value == NULL) {
                        blist_delitem(self, i);
                        ext_mark(self, 0, DIRTY);
                        decref_flush();
                        return _int(0);
                }

                Py_INCREF(value);
                old_value = blist_ass_item_return2((PyBListRoot*)self,i,value);
                DANGER_BEGIN;
                Py_DECREF(old_value);
                DANGER_END;
                return _int(0);
        } else if (PySlice_Check(item)) {
                Py_ssize_t start, stop, step, slicelength;

                ext_mark(self, 0, DIRTY);

                if (PySlice_GetIndicesEx((PySliceObject*)item, self->n,
                                         &start, &stop,&step,&slicelength)<0)
                        return _int(-1);

                /* treat L[slice(a,b)] = v _exactly_ like L[a:b] = v */
                if (step == 1 && ((PySliceObject*)item)->step == Py_None)
                        return _redir(py_blist_ass_slice(oself,start,stop,value));

                if (value == NULL) {
                        /* Delete back-to-front */
                        Py_ssize_t i, cur;

                        if (slicelength <= 0)
                                return _int(0);

                        if (step > 0) {
                                stop = start - 1;
                                start = start + step*(slicelength-1);
                                step = -step;
                        }

                        for (cur = start, i = 0; i < slicelength;
                             cur += step, i++) {
                                PyObject *ob = blist_delitem_return(self, cur);
                                decref_later(ob);
                        }

                        decref_flush();
                        ext_mark(self, 0, DIRTY);

                        return _int(0);
                } else { /* assign slice */
                        PyObject *ins, *seq;
                        Py_ssize_t cur, i;

                        DANGER_BEGIN;
                        seq = PySequence_Fast(value,
                                  "Must assign iterable to extended slice");
                        DANGER_END;
                        if (!seq)
                                return _int(-1);

                        if (seq == (PyObject *) self) {
                                Py_DECREF(seq);
                                seq = (PyObject *) blist_root_copy(self);
                        }

                        if (PySequence_Fast_GET_SIZE(seq) != slicelength) {
                                PyErr_Format(PyExc_ValueError,
                                             "attempt to assign sequence of size %zd to extended slice of size %zd",
                                             PySequence_Fast_GET_SIZE(seq),
                                             slicelength);
                                Py_DECREF(seq);
                                return _int(-1);
                        }

                        if (!slicelength) {
                                Py_DECREF(seq);
                                return _int(0);
                        }

                        for (cur = start, i = 0; i < slicelength;
                             cur += step, i++) {
                                PyObject *ob;
                                ins = PySequence_Fast_GET_ITEM(seq, i);
                                ob = blist_ass_item_return(self, cur, ins);
                                decref_later(ob);
                        }

                        Py_DECREF(seq);

                        decref_flush();

                        return _int(0);
                }
        } else {
                PyErr_SetString(PyExc_TypeError,
                                "list indices must be integers");
                return _int(-1);
        }
}

BLIST_PYAPI(Py_ssize_t)
py_blist_length(PyObject *ob)
{
        assert(PyRootBList_Check(ob));
        return ((PyBList *) ob)->n;
}

BLIST_PYAPI(PyObject *)
py_blist_repeat(PyObject *oself, Py_ssize_t n)
{
        PyObject *ret;
        PyBList *self;

        invariants(oself, VALID_USER|VALID_DECREF);

        self = (PyBList *) oself;

        ret = blist_repeat(self, n);
        decref_flush();
        ext_mark_set_dirty_all(self);

        return _ob(ret);
}

BLIST_PYAPI(PyObject *)
py_blist_inplace_repeat(PyObject *oself, Py_ssize_t n)
{
        PyBList *tmp, *self;

        invariants(oself, VALID_USER|VALID_RW|VALID_DECREF);

        self = (PyBList *) oself;

        tmp = (PyBList *) blist_repeat(self, n);
        if (tmp == NULL)
                return (PyObject *) _blist(NULL);
        blist_become_and_consume(self, tmp);
        Py_INCREF(self);
        SAFE_DECREF(tmp);

        decref_flush();

        ext_mark(self, 0, DIRTY);

        return (PyObject *) _blist(self);
}

BLIST_PYAPI(PyObject *)
py_blist_extend(PyBList *self, PyObject *other)
{
        int err;

        invariants(self, VALID_USER|VALID_RW|VALID_DECREF);

        err = blist_extend(self, other);
        decref_flush();
        ext_mark(self, 0, DIRTY);
        if (PyBList_Check(other))
                ext_mark_set_dirty_all((PyBList *) other);

        if (err < 0)
                return _ob(NULL);
        Py_RETURN_NONE;
}

BLIST_PYAPI(PyObject *)
py_blist_inplace_concat(PyObject *oself, PyObject *other)
{
        int err;
        PyBList *self;

        invariants(oself, VALID_RW|VALID_USER|VALID_DECREF);

        self = (PyBList *) oself;

        err = blist_extend(self, other);
        decref_flush();
        ext_mark(self, 0, DIRTY);
        if (PyBList_Check(other))
                ext_mark_set_dirty_all((PyBList*) other);

        if (err < 0)
                return _ob(NULL);

        Py_INCREF(self);
        return _ob((PyObject *)self);
}

BLIST_PYAPI(int)
py_blist_contains(PyObject *oself, PyObject *el)
{
        int c, ret = 0;
        PyObject *item;
        PyBList *self;

        invariants(oself, VALID_USER | VALID_DECREF);

        self = (PyBList *) oself;

        ITER(self, item, {
                DANGER_BEGIN;
                c = PyObject_RichCompareBool(el, item, Py_EQ);
                DANGER_END;
                if (c < 0) {
                        ret = -1;
                        break;
                }
                if (c > 0) {
                        ret = 1;
                        break;
                }
        });

        decref_flush();
        return _int(ret);
}

BLIST_PYAPI(PyObject *)
py_blist_get_slice(PyObject *oself, Py_ssize_t ilow, Py_ssize_t ihigh)
{
        PyBList *rv, *self;

        invariants(oself, VALID_USER | VALID_DECREF);

        self = (PyBList *) oself;

        if (ilow < 0) ilow = 0;
        else if (ilow > self->n) ilow = self->n;
        if (ihigh < ilow) ihigh = ilow;
        else if (ihigh > self->n) ihigh = self->n;

        rv = blist_root_new();
        if (rv == NULL)
                return (PyObject *) _blist(NULL);

        if (ihigh <= ilow || ilow >= self->n)
                return (PyObject *) _blist(rv);

        if (self->leaf) {
                Py_ssize_t delta = ihigh - ilow;

                copyref(rv, 0, self, ilow, delta);
                rv->num_children = delta;
                rv->n = delta;
                return (PyObject *) _blist(rv);
        }

        blist_become(rv, self);
        blist_delslice(rv, ihigh, self->n);
        blist_delslice(rv, 0, ilow);

        ext_mark(rv, 0, DIRTY);
        ext_mark_set_dirty(self, ilow, ihigh);
        decref_flush();

        return (PyObject *) _blist(rv);
}

/* This should only be called by _PyBList_GET_ITEM_FAST2() */
PyObject *_PyBList_GetItemFast3(PyBListRoot *root, Py_ssize_t i)
{
        PyObject *rv;
        Py_ssize_t dirty_offset = -1;

        invariants(root, VALID_PARENT);
        assert(!root->leaf);
        assert(root->dirty_root != CLEAN);
        assert(i >= 0);
        assert(i < root->n);

        if (ext_is_dirty(root, i, &dirty_offset)){
                rv = ext_make_clean(root, i);
        } else {
                Py_ssize_t ioffset = i / INDEX_FACTOR;
                Py_ssize_t offset = root->offset_list[ioffset];
                PyBList *p = root->index_list[ioffset];
                assert(i >= offset);
                assert(p);
                assert(p->leaf);
                if (i < offset + p->n) {
                        rv = p->children[i - offset];
                        if (dirty_offset >= 0)
                                ext_make_clean(root, dirty_offset);
                } else if (ext_is_dirty(root,i + INDEX_FACTOR,&dirty_offset)){
                        rv = ext_make_clean(root, i);
                } else {
                        ioffset++;
                        assert(ioffset < root->index_length);
                        offset = root->offset_list[ioffset];
                        p = root->index_list[ioffset];
                        rv = p->children[i - offset];
                        assert(p);
                        assert(p->leaf);
                        assert(i < offset + p->n);
                        if (dirty_offset >= 0)
                                ext_make_clean(root, dirty_offset);
                }
        }

        assert(rv == blist_get1((PyBList *)root, i));

        return _ob(rv);
}

BLIST_PYAPI(PyObject *)
py_blist_get_item(PyObject *oself, Py_ssize_t i)
{
        PyBList *self = (PyBList *) oself;
        PyObject *ret;

        invariants(self, VALID_USER);

        if (i < 0 || i >= self->n) {
                set_index_error();
                return _ob(NULL);
        }

        if (self->leaf)
                ret = self->children[i];
        else
                ret = _PyBList_GET_ITEM_FAST2((PyBListRoot*)self, i);
        Py_INCREF(ret);
        return _ob(ret);
}

BLIST_PYAPI(PyObject *)
py_blist_concat(PyObject *oself, PyObject *oother)
{
        PyBList *other, *rv, *self;

        invariants((PyBList *) oself, VALID_USER|VALID_RW|VALID_DECREF);

        self = (PyBList *) oself;

        if (!PyRootBList_Check(oother)) {
                PyErr_Format(PyExc_TypeError,
                        "can only concatenate blist (not \"%.200s\") to blist",
                         Py_TYPE(oother)->tp_name);
                return _ob(NULL);
        }

        other = (PyBList *) oother;

        rv = blist_root_copy(self);
        blist_extend_blist(rv, other);
        ext_mark(rv, 0, DIRTY);
        ext_mark_set_dirty_all(other);

        decref_flush();
        return (PyObject *) _blist(rv);
}

/* User-visible repr() */
BLIST_PYAPI(PyObject *)
py_blist_repr(PyObject *oself)
{
        /* Basic approach: Clone self in O(1) time, then walk through
         * the clone, changing each element to repr() of the element,
         * in O(n) time.  Finally, enclose it in square brackets and
         * call join.
         */

        Py_ssize_t i;
        PyBList *pieces = NULL, *self;
        PyObject *result = NULL;
        PyObject *s, *tmp, *tmp2;

        invariants(oself, VALID_USER);
        self = (PyBList *) oself;

        DANGER_BEGIN;
        i = Py_ReprEnter((PyObject *) self);
        DANGER_END;
        if (i) {
                return i > 0 ? _ob(PyUnicode_FromString("[...]")) : _ob(NULL);
        }

        if (self->n == 0) {
#ifdef Py_BUILD_CORE
                result = PyUnicode_FromString("[]");
#else
                result = PyUnicode_FromString("blist([])");
#endif
                goto Done;
        }

        pieces = blist_root_copy(self);
        if (pieces == NULL)
                goto Done;

        if (blist_repr_r(pieces) < 0)
                goto Done;

#ifdef Py_BUILD_CORE
        s = PyUnicode_FromString("[");
#else
        s = PyUnicode_FromString("blist([");
#endif
        if (s == NULL)
                goto Done;
        tmp = blist_get1(pieces, 0);
        tmp2 = PyUnicode_Concat(s, tmp);
        Py_DECREF(s);
        s = tmp2;
        DANGER_BEGIN;
        py_blist_ass_item((PyObject *) pieces, 0, s);
        DANGER_END;
        Py_DECREF(s);

#ifdef Py_BUILD_CORE
        s = PyUnicode_FromString("]");
#else
        s = PyUnicode_FromString("])");
#endif
        if (s == NULL)
                goto Done;
        tmp = blist_get1(pieces, pieces->n-1);
        tmp2 = PyUnicode_Concat(tmp, s);
        Py_DECREF(s);
        tmp = tmp2;
        DANGER_BEGIN;
        py_blist_ass_item((PyObject *) pieces, pieces->n-1, tmp);
        DANGER_END;
        Py_DECREF(tmp);

        s = PyUnicode_FromString(", ");
        if (s == NULL)
                goto Done;
        result = PyUnicode_Join(s, (PyObject *) pieces);
        Py_DECREF(s);

 Done:
        DANGER_BEGIN;
        /* Only deallocating strings, so this is safe */
        Py_XDECREF(pieces);
        DANGER_END;

        DANGER_BEGIN;
        Py_ReprLeave((PyObject *) self);
        DANGER_END;
        return _ob(result);
}

#if defined(Py_DEBUG) && !defined(BLIST_IN_PYTHON)
/* Return a string that shows the internal structure of the BList */
BLIST_PYAPI(PyObject *)
blist_debug(PyBList *self, PyObject *indent)
{
        PyObject *result, *s, *nl_indent, *comma, *indent2, *tmp, *tmp2;

        invariants(self, VALID_PARENT);

        comma = PyUnicode_FromString(", ");

        if (indent == NULL)
                indent = PyUnicode_FromString("");
        else
                Py_INCREF(indent);

        tmp = PyUnicode_FromString("  ");
        indent2 = PyUnicode_Concat(indent, tmp);
        Py_DECREF(tmp);

        if (!self->leaf) {
                int i;

                nl_indent = indent2;
                tmp = PyUnicode_FromString("\n");
                nl_indent = PyUnicode_Concat(indent2, tmp);
                Py_DECREF(tmp);

                result = PyUnicode_FromFormat("blist(leaf=%d, n=%d, r=%d, ",
                                             self->leaf, self->n,
                                             Py_REFCNT(self));
                /* PyUnicode_Concat(&result, nl_indent); */

                for (i = 0; i < self->num_children; i++) {
                        s = blist_debug((PyBList *)self->children[i], indent2);
                        tmp = PyUnicode_Concat(result, nl_indent);
                        Py_DECREF(result);
                        result = tmp;
                        tmp = PyUnicode_Concat(result, s);
                        Py_DECREF(result);
                        result = tmp;
                        Py_DECREF(s);
                }

                tmp = PyUnicode_FromString(")");
                tmp2 = PyUnicode_Concat(result, tmp);
                Py_DECREF(result);
                Py_DECREF(tmp);
                result = tmp2;
        } else {
                int i;

                result = PyUnicode_FromFormat("blist(leaf=%d, n=%d, r=%d, ",
                                             self->leaf, self->n,
                                             Py_REFCNT(self));
                for (i = 0; i < self->num_children; i++) {
                        s = PyObject_Str(self->children[i]);
                        tmp = PyUnicode_Concat(result, s);
                        Py_DECREF(result);
                        result = tmp;
                        Py_DECREF(s);
                        tmp = PyUnicode_Concat(result, comma);
                        Py_DECREF(result);
                        result = tmp;
                }
        }

        s = indent;
        tmp = PyUnicode_Concat(s, result);
        Py_DECREF(result);
        result = tmp;

        Py_DECREF(comma);
        Py_DECREF(indent);
        check_invariants(self);
        return _ob(result);
}

BLIST_PYAPI(PyObject *)
py_blist_debug(PyBList *self)
{
        invariants(self, VALID_USER);
        return _ob(blist_debug(self, NULL));
}
#endif

BLIST_PYAPI(PyObject *)
py_blist_sort(PyBList *self, PyObject *args, PyObject *kwds)
{
        compare_t compare = {NULL, NULL};
        compare_t *pcompare = &compare;
#if PY_MAJOR_VERSION < 3
        static char *kwlist[] = {"cmp", "key", "reverse", 0};
#else
        static char *kwlist[] = {"key", "reverse", 0};
#endif
        int reverse = 0;
        int ret;
        PyBListRoot saved;
        PyObject *result = NULL;
        static PyObject **extra_list = NULL;

        invariants(self, VALID_USER|VALID_RW | VALID_DECREF);

        if (args != NULL) {
                int err;
                DANGER_BEGIN;
#if PY_MAJOR_VERSION < 3
                err = PyArg_ParseTupleAndKeywords(args, kwds, "|OOi:sort",
                                                  kwlist, &compare.compare,
                                                  &compare.keyfunc,
                                                  &reverse);
#else
                err = PyArg_ParseTupleAndKeywords(args, kwds, "|Oi:sort",
                                                  kwlist, &compare.keyfunc,
                                                  &reverse);
                if (Py_SIZE(args) > 0) {
                        PyErr_SetString(PyExc_TypeError,
                                "must use keyword argument for key function");
                        return NULL;
                }
#endif
                DANGER_END;
                if (!err)
                        return _ob(NULL);
        }

        if (self->n < 2)
                Py_RETURN_NONE;

#if PY_MAJOR_VERSION < 3
        if (is_default_cmp(compare.compare))
                compare.compare = NULL;
#endif
        if (compare.keyfunc == Py_None)
                compare.keyfunc = NULL;
        if (compare.compare == NULL && compare.keyfunc == NULL)
                pcompare = NULL;

        saved.children = self->children;
        Py_TYPE(&saved) = &PyRootBList_Type; /* Make validations happy */
        Py_REFCNT(&saved) = 1;               /* Make valgrind happy */
        saved.n = self->n;
        saved.num_children = self->num_children;
        saved.leaf = self->leaf;

        if (extra_list != NULL) {
                self->children = extra_list;
                extra_list = NULL;
        } else {
                self->children = PyMem_New(PyObject *, LIMIT);
                if (self->children == NULL) {
                        PyErr_NoMemory();
                        goto err;
                }
        }
        self->n = 0;
        self->num_children = 0;
        self->leaf = 1;

        /* Reverse sort stability achieved by initially reversing the list,
           applying a stable forward sort, then reversing the final result. */
        if (reverse)
                blist_reverse((PyBList*)&saved);

        if (saved.leaf)
                ret = gallop_sort(saved.children,saved.num_children,pcompare);
        else
                ret = sort((PyBList*)&saved, pcompare);

        if (reverse)
                blist_reverse((PyBList*)&saved);

        if (ret >= 0)
                result = Py_None;

        if (self->n && saved.n) {
                DANGER_BEGIN;
                /* An error may also have been raised by a comparison
                 * function.  Since may decref that traceback, it can
                 * execute arbitrary python code */
                PyErr_SetString(PyExc_ValueError, "list modified during sort");
                DANGER_END;
                result = NULL;
                blist_CLEAR(self);
        }

        if (extra_list == NULL)
                extra_list = self->children;
        else
                PyMem_Free(self->children);
        assert(!self->n);
 err:
        self->n = saved.n;
        self->num_children = saved.num_children;
        self->leaf = saved.leaf;
        self->children = saved.children;

        Py_XINCREF(result);

        decref_flush();

        ext_index_set_all((PyBListRoot *) self);
        return _ob(result);
}

BLIST_PYAPI(PyObject *)
py_blist_reverse(PyBList *self)
{
        invariants(self, VALID_USER|VALID_RW);

        blist_reverse(self);
        ext_index_set_all((PyBListRoot *)self);

        Py_RETURN_NONE;
}

BLIST_PYAPI(PyObject *)
py_blist_count(PyBList *self, PyObject *v)
{
        Py_ssize_t count = 0;
        PyObject *item;
        int c;

        invariants(self, VALID_USER | VALID_DECREF);

        ITER(self, item, {
                DANGER_BEGIN;
                c = PyObject_RichCompareBool(item, v, Py_EQ);
                DANGER_END;
                if (c > 0)
                        count++;
                else if (c < 0) {
                        ITER_CLEANUP();
                        decref_flush();
                        return _ob(NULL);
                }
        })

        decref_flush();
        return _ob(PyInt_FromSsize_t(count));
}

BLIST_PYAPI(PyObject *)
py_blist_index(PyBList *self, PyObject *args)
{
        Py_ssize_t i, start=0, stop=self->n;
        PyObject *v;
        int c, err;
        PyObject *item;

        invariants(self, VALID_USER|VALID_DECREF);

        DANGER_BEGIN;
        err = PyArg_ParseTuple(args, "O|O&O&:index", &v,
                               _PyEval_SliceIndex, &start,
                               _PyEval_SliceIndex, &stop);
        DANGER_END;
        if (!err)
                return _ob(NULL);
        if (start < 0) {
                start += self->n;
                if (start < 0)
                        start = 0;
        } else if (start > self->n)
	  start = self->n;
        if (stop < 0) {
                stop += self->n;
                if (stop < 0)
                        stop = 0;
        } else if (stop > self->n)
	  stop = self->n;

        i = start;
        ITER2(self, item, start, stop, {
                DANGER_BEGIN;
                c = PyObject_RichCompareBool(item, v, Py_EQ);
                DANGER_END;
                if (c > 0) {
                        ITER_CLEANUP();
                        decref_flush();
                        return _ob(PyInt_FromSsize_t(i));
                } else if (c < 0) {
                        ITER_CLEANUP();
                        decref_flush();
                        return _ob(NULL);
                }
                i++;
        })

        decref_flush();
        PyErr_SetString(PyExc_ValueError, "list.index(x): x not in list");
        return _ob(NULL);
}

BLIST_PYAPI(PyObject *)
py_blist_remove(PyBList *self, PyObject *v)
{
        Py_ssize_t i;
        int c;
        PyObject *item;

        invariants(self, VALID_USER|VALID_RW|VALID_DECREF);

        i = 0;
        ITER(self, item, {
                DANGER_BEGIN;
                c = PyObject_RichCompareBool(item, v, Py_EQ);
                DANGER_END;
                if (c > 0) {
                        ITER_CLEANUP();
                        blist_delitem(self, i);
                        decref_flush();
                        ext_mark(self, 0, DIRTY);
                        Py_RETURN_NONE;
                } else if (c < 0) {
                        ITER_CLEANUP();
                        decref_flush();
                        return _ob(NULL);
                }
                i++;
        })

        decref_flush();
        PyErr_SetString(PyExc_ValueError, "list.remove(x): x not in list");
        return _ob(NULL);
}

BLIST_PYAPI(PyObject *)
py_blist_pop(PyBList *self, PyObject *args)
{
        Py_ssize_t i = -1;
        PyObject *v;
        int err;

        invariants(self, VALID_USER|VALID_RW|VALID_DECREF);

        DANGER_BEGIN;
        err = PyArg_ParseTuple(args, "|n:pop", &i);
        DANGER_END;
        if (!err)
                return _ob(NULL);

        if (self->n == 0) {
                /* Special-case most common failure cause */
                PyErr_SetString(PyExc_IndexError, "pop from empty list");
                return _ob(NULL);
        }

        if (i == -1 || i == self->n-1) {
                v = blist_pop_last_fast(self);
                if (v)
                        return _ob(v);
        }

        if (i < 0)
                i += self->n;
        if (i < 0 || i >= self->n) {
                PyErr_SetString(PyExc_IndexError, "pop index out of range");
                return _ob(NULL);
        }

        v = blist_delitem_return(self, i);
        ext_mark(self, 0, DIRTY);

        decref_flush(); /* Remove any deleted BList nodes */

        return _ob(v); /* the caller now owns the reference the list had */
}

BLIST_PYAPI(PyObject *)
py_blist_insert(PyBList *self, PyObject *args)
{
        Py_ssize_t i;
        PyObject *v;
        PyBList *overflow;
        int err;

        invariants(self, VALID_USER|VALID_RW);

        DANGER_BEGIN;
        err = PyArg_ParseTuple(args, "nO:insert", &i, &v);
        DANGER_END;
        if (!err)
                return _ob(NULL);

        if (self->n == PY_SSIZE_T_MAX) {
                PyErr_SetString(PyExc_OverflowError,
                                "cannot add more objects to list");
                return _ob(NULL);
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
        ext_mark(self, 0, DIRTY);
        Py_RETURN_NONE;
}

BLIST_PYAPI(PyObject *)
py_blist_append(PyBList *self, PyObject *v)
{
        int err;

        invariants(self, VALID_USER|VALID_RW);

        err = blist_append(self, v);

        if (err < 0)
                return _ob(NULL);

        Py_RETURN_NONE;
}

BLIST_PYAPI(PyObject *)
py_blist_subscript(PyObject *oself, PyObject *item)
{
        PyBList *self;

        invariants(oself, VALID_USER);

        self = (PyBList *) oself;

        if (PyIndex_Check(item)) {
                Py_ssize_t i;
                PyObject *ret;

                if (PyLong_CheckExact(item)) {
                        i = PyInt_AsSsize_t(item);
                        if (i == -1 && PyErr_Occurred()) {
                                PyErr_Clear();
                                goto number;
                        }
                } else {
                number:
                        i = PyNumber_AsSsize_t(item, PyExc_IndexError);
                        if (i == -1 && PyErr_Occurred())
                                return _ob(NULL);
                }

                if (i < 0)
                        i += self->n;

                if (i < 0 || i >= self->n) {
                        set_index_error();
                        return _ob(NULL);
                }

                if (self->leaf)
                        ret = self->children[i];
                else
                        ret = _PyBList_GET_ITEM_FAST2((PyBListRoot*)self, i);
                Py_INCREF(ret);

                return _ob(ret);
        } else if (PySlice_Check(item)) {
                Py_ssize_t start, stop, step, slicelength, cur, i;
                PyBList* result;
                PyObject* it;

                if (PySlice_GetIndicesEx((PySliceObject*)item, self->n,
                                         &start, &stop,&step,&slicelength)<0) {
                        return _ob(NULL);
                }

                if (step == 1)
                        return _redir((PyObject *)
                                      py_blist_get_slice((PyObject *) self, start, stop));

                result = blist_root_new();

                if (slicelength <= 0)
                        return _ob((PyObject *) result);

                /* This could be made slightly faster by using forests */
                /* Also, by special-casing small trees */
                for (cur = start, i = 0; i < slicelength; cur += step, i++) {
                        int err;

                        it = blist_get1(self, cur);
                        err = blist_append(result, it);
                        if (err < 0) {
                                Py_DECREF(result);
                                return _ob(NULL);
                        }
                }

                ext_mark(result, 0, DIRTY);
                return _ob((PyObject *) result);
        } else {
                PyErr_SetString(PyExc_TypeError,
                                "list indices must be integers");
                return _ob(NULL);
        }
}

#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION >= 6 || PY_MAJOR_VERSION >= 3
static PyObject *
py_blist_root_sizeof(PyBListRoot *root)
{
        Py_ssize_t res;
        res = sizeof(PyBListRoot)
                + LIMIT * sizeof(PyObject *)
                + root->index_length * (sizeof (PyBList *) +sizeof(Py_ssize_t))
                + root->dirty_length * sizeof(Py_ssize_t)
                + (root->index_length ?
                   SETCLEAN_LEN(root->index_length) * sizeof(unsigned): 0);
        return PyLong_FromSsize_t(res);
}

static PyObject *
py_blist_sizeof(PyBList *self)
{
        Py_ssize_t res;
        res = sizeof(PyBList)
                + LIMIT * sizeof(PyObject *);
        return PyLong_FromSsize_t(res);
}

PyDoc_STRVAR(sizeof_doc,
"L.__sizeof__() -- size of L in memory, in bytes");
#endif

/************************************************************************
 * Routines for supporting pickling
 */

#ifndef BLIST_IN_PYTHON
BLIST_PYAPI(PyObject *)
blist_getstate(PyBList *self)
{
        PyObject *lst;
        int i;
        
        invariants(self, VALID_PARENT);

        lst = PyList_New(self->num_children);
        for (i = 0; i < self->num_children; i++) {
                PyList_SET_ITEM(lst, i, self->children[i]);
                Py_INCREF(PyList_GET_ITEM(lst, i));
        }

        if (PyRootBList_CheckExact(self))
                ext_mark_set_dirty_all(self);

        return _ob(lst);        
}

BLIST_PYAPI(PyObject *)
py_blist_setstate(PyBList *self, PyObject *state)
{
        Py_ssize_t i;
        
        invariants(self, VALID_PARENT);

        if (!PyList_CheckExact(state) || PyList_GET_SIZE(state) > LIMIT) {
                PyErr_SetString(PyExc_TypeError, "invalid state");
                return _ob(NULL);
        }

        for (self->n = i = 0; i < PyList_GET_SIZE(state); i++) {
                PyObject *child = PyList_GET_ITEM(state, i);
                if (Py_TYPE(child) == &PyBList_Type) {
                        self->leaf = 0;
                        self->n += ((PyBList*)child)->n;
                } else
                        self->n++;
                self->children[i] = child;
                Py_INCREF(child);
        }

        self->num_children = PyList_GET_SIZE(state);

        if (PyRootBList_CheckExact(self)) 
                ext_index_all((PyBListRoot*)self);

        Py_RETURN_NONE;
}

BLIST_PYAPI(PyObject *)
py_blist_reduce(PyBList *self)
{
        PyObject *rv, *args, *type;

        invariants(self, VALID_PARENT);

        type = (PyObject *) Py_TYPE(self);
        args = PyTuple_New(0);
        rv = PyTuple_New(3);
        PyTuple_SET_ITEM(rv, 0, type);
        Py_INCREF(type);
        PyTuple_SET_ITEM(rv, 1, args);
        PyTuple_SET_ITEM(rv, 2, blist_getstate(self));        

        return _ob(rv);
}
#endif

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
        {"__getitem__", (PyCFunction)py_blist_subscript, METH_O|METH_COEXIST, getitem_doc},
        {"__reversed__",(PyCFunction)py_blist_reversed, METH_NOARGS, reversed_doc},
#ifndef BLIST_IN_PYTHON
        {"__reduce__",  (PyCFunction)py_blist_reduce, METH_NOARGS, NULL},
        {"__setstate__",(PyCFunction)py_blist_setstate, METH_O, NULL},
#endif
        {"append",      (PyCFunction)py_blist_append,  METH_O, append_doc},
        {"insert",      (PyCFunction)py_blist_insert,  METH_VARARGS, insert_doc},
        {"extend",      (PyCFunction)py_blist_extend,  METH_O, extend_doc},
        {"pop",         (PyCFunction)py_blist_pop,     METH_VARARGS, pop_doc},
        {"remove",      (PyCFunction)py_blist_remove,  METH_O, remove_doc},
        {"index",       (PyCFunction)py_blist_index,   METH_VARARGS, index_doc},
        {"count",       (PyCFunction)py_blist_count,   METH_O, count_doc},
        {"reverse",     (PyCFunction)py_blist_reverse, METH_NOARGS, reverse_doc},
        {"sort",        (PyCFunction)py_blist_sort,    METH_VARARGS | METH_KEYWORDS, sort_doc},
#if defined(Py_DEBUG) && !defined(BLIST_IN_PYTHON)
        {"debug",       (PyCFunction)py_blist_debug,   METH_NOARGS, NULL},
#endif
#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION >= 6 || PY_MAJOR_VERSION >= 3
        {"__sizeof__",  (PyCFunction)py_blist_root_sizeof, METH_NOARGS, sizeof_doc},
#endif
        {NULL,          NULL}           /* sentinel */
};

static PyMethodDef blist_internal_methods[] = {
#ifndef BLIST_IN_PYTHON
        {"__reduce__",  (PyCFunction)py_blist_reduce, METH_NOARGS, NULL},
        {"__setstate__",(PyCFunction)py_blist_setstate, METH_O, NULL},
#endif
#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION >= 6 || PY_MAJOR_VERSION >= 3
        {"__sizeof__",  (PyCFunction)py_blist_sizeof, METH_NOARGS, sizeof_doc},
#endif
        {NULL,          NULL}           /* sentinel */
};

static PySequenceMethods blist_as_sequence = {
        py_blist_length,                   /* sq_length */
        py_blist_concat,                /* sq_concat */
        py_blist_repeat,              /* sq_repeat */
        py_blist_get_item,            /* sq_item */
        py_blist_get_slice,      /* sq_slice */
        py_blist_ass_item,         /* sq_ass_item */
        py_blist_ass_slice,   /* sq_ass_slice */
        py_blist_contains,              /* sq_contains */
        py_blist_inplace_concat,        /* sq_inplace_concat */
        py_blist_inplace_repeat,      /* sq_inplace_repeat */
};

PyDoc_STRVAR(blist_doc,
"blist() -> new list\n"
"blist(sequence) -> new list initialized from sequence's items");

static PyMappingMethods blist_as_mapping = {
        py_blist_length,
        py_blist_subscript,
        py_blist_ass_subscript
};

PyTypeObject PyBList_Type = {
        PyVarObject_HEAD_INIT(NULL, 0)
#ifdef BLIST_IN_PYTHON
        "__internal_blist",
#else
        "blist.__internal_blist",
#endif
        sizeof(PyBList),
        0,
        py_blist_dealloc,                       /* tp_dealloc */
        0,                                      /* tp_print */
        0,                                      /* tp_getattr */
        0,                                      /* tp_setattr */
        0,                                      /* tp_compare */
        0,                                      /* tp_repr */
        0,                                      /* tp_as_number */
        0,                                      /* tp_as_sequence */
        0,                                      /* tp_as_mapping */
        py_blist_nohash,                        /* tp_hash */
        0,                                      /* tp_call */
        0,                                      /* tp_str */
        PyObject_GenericGetAttr,                /* tp_getattro */
        0,                                      /* tp_setattro */
        0,                                      /* tp_as_buffer */
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
                Py_TPFLAGS_BASETYPE,            /* tp_flags */
        blist_doc,                              /* tp_doc */
        py_blist_traverse,                      /* tp_traverse */
        0,                                      /* tp_clear */
        0,                                      /* tp_richcompare */
        0,                                      /* tp_weaklistoffset */
        0,                                      /* tp_iter */
        0,                                      /* tp_iternext */
        blist_internal_methods,                 /* tp_methods */
        0,                                      /* tp_members */
        0,                                      /* tp_getset */
        0,                                      /* tp_base */
        0,                                      /* tp_dict */
        0,                                      /* tp_descr_get */
        0,                                      /* tp_descr_set */
        0,                                      /* tp_dictoffset */
        py_blist_internal_init,                 /* tp_init */
        0,                                      /* tp_alloc */
        py_blist_internal_tp_new,               /* tp_new */
        PyObject_GC_Del,                        /* tp_free */
};

PyTypeObject PyRootBList_Type = {
        PyVarObject_HEAD_INIT(NULL, 0)
#ifdef BLIST_IN_PYTHON
        "list",
#else
        "blist.blist",
#endif
        sizeof(PyBListRoot),
        0,
        py_blist_dealloc,                       /* tp_dealloc */
        0,                                      /* tp_print */
        0,                                      /* tp_getattr */
        0,                                      /* tp_setattr */
        0,                                      /* tp_compare */
        py_blist_repr,                          /* tp_repr */
        0,                                      /* tp_as_number */
        &blist_as_sequence,                     /* tp_as_sequence */
        &blist_as_mapping,                      /* tp_as_mapping */
        py_blist_nohash,                        /* tp_hash */
        0,                                      /* tp_call */
        0,                                      /* tp_str */
        PyObject_GenericGetAttr,                /* tp_getattro */
        0,                                      /* tp_setattro */
        0,                                      /* tp_as_buffer */
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
                Py_TPFLAGS_BASETYPE             /* tp_flags */
#if (PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION >= 6 || PY_MAJOR_VERSION >= 3) && defined(BLIST_IN_PYTHON)
        | Py_TPFLAGS_LIST_SUBCLASS
#endif
        ,
        blist_doc,                              /* tp_doc */
        py_blist_traverse,                      /* tp_traverse */
        py_blist_clear,                         /* tp_clear */
        py_blist_richcompare,                   /* tp_richcompare */
        0,                                      /* tp_weaklistoffset */
        py_blist_iter,                          /* tp_iter */
        0,                                      /* tp_iternext */
        blist_methods,                          /* tp_methods */
        0,                                      /* tp_members */
        0,                                      /* tp_getset */
        0,                                      /* tp_base */
        0,                                      /* tp_dict */
        0,                                      /* tp_descr_get */
        0,                                      /* tp_descr_set */
        0,                                      /* tp_dictoffset */
        py_blist_init,                          /* tp_init */
        PyType_GenericAlloc,                    /* tp_alloc */
        py_blist_root_tp_new,                   /* tp_new */
        PyObject_GC_Del,                        /* tp_free */
};

static PyMethodDef module_methods[] = { { NULL } };

BLIST_LOCAL(int)
init_blist_types1(void)
{
        decref_init();
        highest_set_bit_init();

        Py_TYPE(&PyBList_Type) = &PyType_Type;
        Py_TYPE(&PyRootBList_Type) = &PyType_Type;
        Py_TYPE(&PyBListIter_Type) = &PyType_Type;
        Py_TYPE(&PyBListReverseIter_Type) = &PyType_Type;

        Py_INCREF(&PyBList_Type);
        Py_INCREF(&PyRootBList_Type);
        Py_INCREF(&PyBListIter_Type);
        Py_INCREF(&PyBListReverseIter_Type);

        return 0;
}

BLIST_LOCAL(int)
init_blist_types2(void)
{        
        if (PyType_Ready(&PyRootBList_Type) < 0) return -1;
        if (PyType_Ready(&PyBList_Type) < 0) return -1;
        if (PyType_Ready(&PyBListIter_Type) < 0) return -1;
        if (PyType_Ready(&PyBListReverseIter_Type) < 0) return -1;

        return 0;
}

#if PY_MAJOR_VERSION < 3
PyMODINIT_FUNC
initblist(void)
{
        PyObject *m;
        PyObject *limit = PyInt_FromLong(LIMIT);

        init_blist_types1();
        init_blist_types2();

        m = Py_InitModule3("blist", module_methods, "blist");

        PyModule_AddObject(m, "blist", (PyObject *) &PyRootBList_Type);
        PyModule_AddObject(m, "_limit", limit);
        PyModule_AddObject(m, "__internal_blist", (PyObject *)
                &PyBList_Type);
}
#else

static struct PyModuleDef blist_module = {
        PyModuleDef_HEAD_INIT,
        "blist",
        NULL,
        -1,
        module_methods,
        NULL,
        NULL,
        NULL,
        NULL,
};

PyMODINIT_FUNC
PyInit_blist(void)
{
        PyObject *m;
        PyObject *limit = PyInt_FromLong(LIMIT);

        if (init_blist_types1() < 0)
                return NULL;
        if (init_blist_types2() < 0)
                return NULL;
        
        m = PyModule_Create(&blist_module);

        PyModule_AddObject(m, "blist", (PyObject *) &PyRootBList_Type);
        PyModule_AddObject(m, "_limit", limit);
        PyModule_AddObject(m, "__internal_blist", (PyObject *)
                &PyBList_Type);

        return m;
}
#endif

/************************************************************************
 * The List C API, for building BList into the Python core
 */

#ifdef Py_BUILD_CORE
int
PyList_Init1(void)
{
        return init_blist_types1();
}

int
PyList_Init2(void)
{
        return init_blist_types2();
}

PyObject *PyList_New(Py_ssize_t size)
{
        PyBList *self = blist_root_new();
        PyObject *tmp;

        if (self == NULL)
                return NULL;

        if (size <= LIMIT) {
                self->n = size;
                self->num_children = size;
                memset(self->children, 0, sizeof(PyObject *) * size);
                check_invariants(self);
                return (PyObject *) self;
        }

        self->n = 1;
        self->num_children = 1;
        self->children[0] = NULL;

        tmp = blist_repeat(self, size);
        check_invariants((PyBList *) tmp);
        SAFE_DECREF(self);
        _decref_flush();
        check_invariants((PyBList *) tmp);
        assert(((PyBList *)tmp)->n == size);
        ext_dealloc((PyBListRoot *) tmp);
        return tmp;
}

Py_ssize_t PyList_Size(PyObject *ob)
{
        if (ob == NULL || !PyList_Check(ob)) {
                PyErr_BadInternalCall();
                return -1;
        }

        return py_blist_length(ob);
}

PyObject *PyList_GetItem(PyObject *ob, Py_ssize_t i)
{
        PyBList *self = (PyBList *) ob;
        PyObject *ret;

        if (ob == NULL || !PyList_Check(ob)) {
                PyErr_BadInternalCall();
                return NULL;
        }

        assert(i >= 0 && i < self->n); /* XXX Remove */

        if (i < 0 || i >= self->n) {
                set_index_error();
                return NULL;
        }

        ret = blist_get1((PyBList *) ob, i);
        assert(ret != NULL);
        return ret;
}

int PyList_SetItem(PyObject *ob, Py_ssize_t i, PyObject *item)
{
        int ret;

        if (ob == NULL || !PyList_Check(ob)) {
                PyErr_BadInternalCall();
                Py_XDECREF(item);
                return -1;
        }

        assert(i >= 0 && i < ((PyBList *)ob)->n); /* XXX Remove */
        ret = py_blist_ass_item(ob, i, item);
        assert(Py_REFCNT(item) > 1);
        Py_XDECREF(item);
        return ret;
}

int PyList_Insert(PyObject *ob, Py_ssize_t i, PyObject *v)
{
        PyBList *overflow;
        PyBList *self = (PyBList *) ob;

        if (ob == NULL || !PyList_Check(ob)) {
                PyErr_BadInternalCall();
                return -1;
        }

        invariants(self, VALID_USER|VALID_RW);

        if (self->n == PY_SSIZE_T_MAX) {
                PyErr_SetString(PyExc_OverflowError,
                                "cannot add more objects to list");
                return _int(0);
        }

        if (i < 0) {
                i += self->n;
                if (i < 0)
                        i = 0;
        } else if (i > self->n)
                i = self->n;

        if (self->leaf && self->num_children < LIMIT) {
                Py_INCREF(v);

                shift_right(self, i, 1);
                self->num_children++;
                self->n++;
                self->children[i] = v;
                return _int(0);
        }

        overflow = ins1(self, i, v);
        if (overflow)
                blist_overflow_root(self, overflow);
        ext_mark(self, 0, DIRTY);

        return _int(0);
}

int PyList_Append(PyObject *ob, PyObject *item)
{
        if (ob == NULL || !PyList_Check(ob)) {
                PyErr_BadInternalCall();
                return -1;
        }

        return blist_append((PyBList *) ob, item);
}

PyObject *PyList_GetSlice(PyObject *ob, Py_ssize_t i, Py_ssize_t j)
{
        if (ob == NULL || !PyList_Check(ob)) {
                PyErr_BadInternalCall();
                return NULL;
        }

        return py_blist_get_slice(ob, i, j);
}

int PyList_SetSlice(PyObject *ob, Py_ssize_t i, Py_ssize_t j, PyObject *lst)
{
        if (ob == NULL || !PyList_Check(ob)) {
                PyErr_BadInternalCall();
                return -1;
        }

        return py_blist_ass_slice(ob, i, j, lst);
}

int PyList_Sort(PyObject *ob)
{
        PyObject *ret;

        if (ob == NULL || !PyList_Check(ob)) {
                PyErr_BadInternalCall();
                return -1;
        }

        ret = py_blist_sort((PyBList *) ob, NULL, NULL);
        if (ret == NULL)
                return -1;

        Py_DECREF(ret);
        return 0;
}

int PyList_Reverse(PyObject *ob)
{
        if (ob == NULL || !PyList_Check(ob)) {
                PyErr_BadInternalCall();
                return -1;
        }

        invariants((PyBList *) ob, VALID_USER|VALID_RW);

        blist_reverse((PyBList *) ob);
        ext_index_set_all((PyBListRoot *) ob);

        return _int(0);
}

PyObject *PyList_AsTuple(PyObject *ob)
{
        PyBList *self = (PyBList *) ob;
        PyObject *item;
        PyTupleObject *tuple;
        int i;

        if (ob == NULL || !PyList_Check(ob)) {
                PyErr_BadInternalCall();
                return NULL;
        }

        invariants(self, VALID_USER | VALID_DECREF);

        DANGER_BEGIN;
        tuple = (PyTupleObject *) PyTuple_New(self->n);
        DANGER_END;
        if (tuple == NULL)
                return _ob(NULL);

        i = 0;
        ITER(self, item, {
                tuple->ob_item[i++] = item;
                Py_INCREF(item);
        })

        assert(i == self->n);

        decref_flush();

        return _ob((PyObject *) tuple);

}

PyObject *
_PyList_Extend(PyBListRoot *ob, PyObject *b)
{
        return py_blist_extend((PyBList *) ob, b);
}

#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION >= 6 || PY_MAJOR_VERSION >= 3
void
PyList_Fini(void)
{
        /* XXX free statically allocated memory here */
}
#endif

#endif
