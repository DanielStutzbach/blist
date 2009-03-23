
/* List object interface */

/*
Another generally useful object type is an list of object pointers.
This is a mutable type: the list items can be changed, and items can be
added or removed.  Out-of-range indices or non-list objects are ignored.

*** WARNING *** PyList_SetItem does not increment the new item's reference
count, but does decrement the reference count of the item it replaces,
if not nil.  It does *decrement* the reference count if it is *not*
inserted in the list.  Similarly, PyList_GetItem does not increment the
returned item's reference count.
*/

/**********************************************************************
 *                                                                    * 
 *        PLEASE READ blist.rst BEFORE MODIFYING THIS CODE            *
 *                                                                    *
 **********************************************************************/

#ifndef Py_BLISTOBJECT_H
#define Py_BLISTOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef LIMIT
#define LIMIT (128)     /* Good performance value */
#if 0
#define LIMIT (8)       /* Maximum size, currently low (for test purposes) */
#endif
#endif
#define HALF  (LIMIT/2) /* Minimum size */
#define MAX_HEIGHT (16) /* ceil(log(PY_SSIZE_T_MAX)/log(HALF)); */
#if LIMIT & 1
#error LIMIT must be divisible by 2
#endif
#if LIMIT < 8
#error LIMIT must be at least 8
#endif
#define INDEX_FACTOR (HALF)

typedef struct PyBList {
        PyObject_HEAD
        Py_ssize_t n;              /* Total # of user-object descendents */
        unsigned num_children;     /* Number of immediate children */
        int leaf;                  /* Boolean value */
        PyObject **children;       /* Immediate children */
} PyBList;

typedef struct PyBListRoot {
        PyObject_HEAD
        Py_ssize_t n;              /* Total # of user-object descendents */
        unsigned num_children;     /* Number of immediate children */
        int leaf;                  /* Boolean value */
        PyObject **children;       /* Immediate children */

        PyBList **index_list;
        int *offset_list;
        unsigned *setclean_list;    /* contains index_length _bits_ */
        int index_length;
        int *dirty;
        int dirty_length;
        int dirty_root;
        int free_root;

#ifdef Py_DEBUG
        int last_n;                 /* For debug */
#endif
} PyBListRoot;

/************************************************************************
 * Code used when building BList into the interpreter
 */
        
#ifdef Py_BUILD_CORE
typedef PyBListRoot PyListObject;
        
//PyAPI_DATA(PyTypeObject) PyList_Type;

PyAPI_DATA(PyTypeObject) PyBList_Type;
PyAPI_DATA(PyTypeObject) PyRootBList_Type;
#define PyList_Type PyRootBList_Type

#define PyList_Check(op) PyObject_TypeCheck(op, &PyRootBList_Type)
#define PyList_CheckExact(op) ((op)->ob_type == &PyRootBList_Type)

PyAPI_FUNC(PyObject *) PyList_New(Py_ssize_t size);
PyAPI_FUNC(Py_ssize_t) PyList_Size(PyObject *);
PyAPI_FUNC(PyObject *) PyList_GetItem(PyObject *, Py_ssize_t);
PyAPI_FUNC(int) PyList_SetItem(PyObject *, Py_ssize_t, PyObject *);
PyAPI_FUNC(int) PyList_Insert(PyObject *, Py_ssize_t, PyObject *);
PyAPI_FUNC(int) PyList_Append(PyObject *, PyObject *);
PyAPI_FUNC(PyObject *) PyList_GetSlice(PyObject *, Py_ssize_t, Py_ssize_t);
PyAPI_FUNC(int) PyList_SetSlice(PyObject *, Py_ssize_t, Py_ssize_t, PyObject *);
PyAPI_FUNC(int) PyList_Sort(PyObject *);
PyAPI_FUNC(int) PyList_Reverse(PyObject *);
PyAPI_FUNC(PyObject *) PyList_AsTuple(PyObject *);
PyAPI_FUNC(PyObject *) _PyList_Extend(PyBListRoot *, PyObject *);

PyAPI_FUNC(void) _PyList_SetItemFast(PyObject *, Py_ssize_t, PyObject *);

/* Macro, trading safety for speed */
#define PyList_GET_ITEM(op, i) (((PyBList *)(op))->leaf ? (((PyBList *)(op))->children[(i)]) : _PyBList_GET_ITEM_FAST2((PyBListRoot*) (op), (i)))
//#define PyList_SET_ITEM(op, i, v) (((PyBList *)(op))->leaf ? (void) (((PyBList *)(op))->children[(i)] = (v)) : (void) _PyList_SetItemFast((PyObject *) (op), (i), (v)))
#define PyList_SET_ITEM(self, i, v) (((PyBList *)self)->leaf ? (void) (((PyBList*)self)->children[(i)] = (v)) : (void) blist_ass_item_return2((PyBListRoot*)(self), (i), (v)))

//#define PyList_GET_ITEM(op, i) PyList_GetItem((PyObject*) (op), (i))
//#define PyList_SET_ITEM(op, i, v) _PyList_SetItemFast((PyObject *) (op), (i), (v))
#define PyList_GET_SIZE(op) ({ assert(PyList_Check(op)); (((PyBList *)(op))->n); })

#define PyList_IS_LEAF(op) ({ assert(PyList_Check(op)); (((PyBList *) (op))->leaf); })

#endif

PyAPI_FUNC(PyObject *) _PyBList_GetItemFast3(PyBListRoot *, Py_ssize_t);

PyAPI_FUNC(PyObject *) blist_ass_item_return_slow(PyBListRoot *root, Py_ssize_t i, PyObject *v);
PyAPI_FUNC(PyObject *) ext_make_clean_set(PyBListRoot *root, Py_ssize_t i, PyObject *v);

#define INDEX_FACTOR (HALF)

/* This should only be called if we know the root is not a leaf */
/* inlining a common case for speed */
static inline PyObject *
_PyBList_GET_ITEM_FAST2(PyBListRoot *root, Py_ssize_t i)
{
        assert(!root->leaf);

        if (root->dirty_root != -1 /* "CLEAN" */)
                return _PyBList_GetItemFast3(root, i);

        int ioffset = i / INDEX_FACTOR;
        Py_ssize_t offset = root->offset_list[ioffset];
        PyBList *p = root->index_list[ioffset];

        if (i < offset + p->n)
                return p->children[i - offset];
        ioffset++;
        offset = root->offset_list[ioffset];
        p = root->index_list[ioffset];
        return p->children[i - offset];
}

#define SETCLEAN_LEN(index_length) ((((index_length)-1) / sizeof(unsigned))+1)
#if SIZEOF_INT == 4
#define SETCLEAN_SHIFT (5u)
#define SETCLEAN_MASK (0x1fu)
#elif SIZEOF_INT == 8
#define SETCLEAN_SHIFT (6u)
#define SETCLEAN_MASK (0x3fu)
#else
#error Unknown sizeof(unsigned)
#endif

#define SET_BIT(setclean_list, i) (setclean_list[(i) >> SETCLEAN_SHIFT] |= (1u << ((i) & SETCLEAN_MASK)))
#define CLEAR_BIT(setclean_list, i) (setclean_list[(i) >> SETCLEAN_SHIFT] &= ~(1u << ((i) & SETCLEAN_MASK)))
#define GET_BIT(setclean_list, i) (setclean_list[(i) >> SETCLEAN_SHIFT] & (1u << ((i) & SETCLEAN_MASK)))
        
static inline PyObject *
blist_ass_item_return2(PyBListRoot *root, Py_ssize_t i, PyObject *v)
{
        PyObject *rv;
        int ioffset = i / INDEX_FACTOR;

        assert(i >= 0);
        assert(i < root->n);
        assert(!root->leaf);
        
        if (root->dirty_root != -1 /* CLEAN */
            || !GET_BIT(root->setclean_list, ioffset))
                return blist_ass_item_return_slow(root, i, v);
        
        Py_ssize_t offset = root->offset_list[ioffset];
        PyBList *p = root->index_list[ioffset];
        assert(i >= offset);
        assert(p);
        assert(p->leaf);
        if (i < offset + p->n) {
        good:
                rv = p->children[i - offset];
                p->children[i - offset] = v;
        } else if (!GET_BIT(root->setclean_list, ioffset+1)) {
                return ext_make_clean_set(root, i, v);
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

        return rv;
}

#ifdef __cplusplus
}
#endif
#endif /* !Py_BLISTOBJECT_H */
