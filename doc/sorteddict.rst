.. include:: mymath.txt

sorteddict
==========

.. currentmodule:: blist

.. class:: sorteddict([key,] [arg,] **kw)

   A :class:`sorteddict` provides the same methods as a :class:`dict`.
   Additionally, a :class:`sorteddict` efficiently maintains its keys
   in sorted order.  Consequently, the :ref:`keys <sorteddict.keys>`
   method will return the keys in sorted order, the :ref:`popitem
   <sorteddict.popitem>` method will remove the item with the lowest
   key, etc.

   *key* specifies a function of one argument that is used to extract
   a comparison sort key from each dict key,
   e.g. ``sorteddict(str.lower)``.  If no function is specified, the
   default compares the elements directly.  The *key* function must
   always return the same key for an item or the results are
   unpredictable.  The *key* argument must be provided as a positional
   argument; if provided as a keyword argument, it will be added to
   the dictionary instead.

   An optional *iterable* provides an initial series of items to
   populate the :class:`sorteddict`.  Each item in the series must
   itself contain to items.  The first is used as a key in the new
   dictionary, and the second as the key's value. If a given key is
   seen more than once, the last value associated with it is retained
   in the new dictionary.

   If keyword arguments are given, the keywords themselves with their
   associated values are added as items to the dictionary. If a key is
   specified both in the positional argument and as a keyword argument,
   the value associated with the keyword is retained in the
   dictionary. For example, these all return a dictionary equal to
   ``{"one": 2, "two": 3}``:

   * ``sorteddict(one=2, two=3)``
   * ``sorteddict({'one': 2, 'two': 3})``
   * ``sorteddict(zip(('one', 'two'), (2, 3)))``
   * ``sorteddict([['two', 3], ['one', 2]])``

   The first example only works for keys that are valid Python
   identifiers; the others work with any valid keys.

   .. method:: x in d

      Returns True if and only if *x* is a key in the dictionary.

      Requires |theta(1)| operations in the average case.

      :rtype: :class:`bool`

   .. _sorteddict.delitem:
   .. method:: del d[key]

      Remove ``d[key]`` from *d*.  Raises a :exc:`KeyError` if *key*
      is not in the dictionary.

      Requires |theta(log n)| comparisons.

   .. method:: d[key]

      Return the item of *d* with key *key*.  Raises a :exc:`KeyError`
      if *key* is not in the dictionary.

      If a subclass of dict defines a method :meth:`__missing__`, if
      the key *key* is not present, the ``d[key]`` operation calls
      that method with the key *key* as argument.  The ``d[key]``
      operation then returns or raises whatever is returned or raised
      by the ``__missing__(key)`` call if the key is not present. No
      other operations or methods invoke :meth:`__missing__`. If
      :meth:`__missing__` is not defined, :exc:`KeyError` is raised.
      :meth:`__missing__` must be a method; it cannot be an instance
      variable.

      Requires |theta(1)| operations in the average case.

      :rtype: value

   .. method:: L == L2, L != L2

      Test two dictionaries for equality (or inequality).
      Dictionaries compare equal if and only if they have the same
      length, if all of the keys of *L* may be found in *L2*, and all
      of the corresponding values compare equal.

      In the worst case, requires |theta(n)| operations and |theta(n)|
      comparisons.

      :rtype: :class:`bool`

   .. method:: iter(d)

      Creates an iterator over the sorted keys of the dictionary.

      Requires |theta(log n)| operations to create the iterator.  Each
      element from the iterator requires |theta(1)| operations to
      retrieve, or |theta(n)| operations to iterate over the entire
      dictionary.

      :rtype: iterator

   .. method:: len(d)

      Returns the number of (key, value) pairs in the dictionary.

      Requires |theta(1)| operations.

      :rtype: :class:`int`

   .. method:: d[key] = value

      Sets `d[key]` to *value*.

      In the average case, requires |theta(log**2 n)| operations and
      |theta(log n)| comparisons.

   .. method:: d.clear()

      Remove all elements from the dictionary.

      Requires |theta(n)| total operations in the worst case.

   .. method:: d.copy()

      Creates a shallow copy of the dictionary.

      Requires |theta(n)| total operations and no comparisons.

      :rtype: :class:`sorteddict`

   .. method:: d.fromkeys(seq[, value[, key]])

      Create a new dictionary with keys from *seq* and values set to
      *value*.  *key* specifies a function of one argument that is
      used to extract a comparison sort key from each dict key.

      :meth:`fromkeys` is a class method that returns a new
      dictionary.  *value* defaults to ``None``.

      :rtype: :class:`sorteddict`

   .. method:: d.get(key[, default])

      Return the value for *key* if *key* is in the dictionary, else
      *default*.  If *default* is not given, it defaults to ``None``,
      so that this method never raises a :exc:`KeyError`.

      Requires |theta(1)| total operations in the average case.

      :rtype: value

   .. method:: d.items()

      In Python 2, returns a blist of the dictionary's items (``(key,
      value)`` pairs).

      In Python 3, returns a new :class:`ItemsView` of the dictionary's
      items.  In addition to the methods provided by the built-in `view
      <http://docs.python.org/release/3.1/library/stdtypes.html#dictionary-view-objects>`_,
      the :class:`ItemsView` is indexable (e.g., ``d.items()[5]``).

      Requires |theta(n)| operations.

      :rtype: :class:`blist` or :class:`ItemsView`

   .. _sorteddict.keys:
   .. method:: d.keys()

      In Python 2, returns a :class:`sortedset` of the dictionary's keys.

      In Python 3, returns a new :class:`KeysView` of the dictionary's
      keys.  In addition to the methods provided by the built-in `view
      <http://docs.python.org/release/3.1/library/stdtypes.html#dictionary-view-objects>`_,
      the :class:`KeysView` is indexable (e.g., ``d.keys()[5]``).

      Requires |theta(1)| operations.

      :rtype: :class:`sortedset` or :class:`KeysView`

   .. method:: d.pop(key[, default])

      If *key* is in the dictionary, remove it and return its value,
      else return *default*. If *default* is not given and *key* is not in
      the dictionary, a :exc:`KeyError` is raised.

      Requires |theta(1)| operations if *key* is not in the
      dictionary.  Otherwise, requires |theta(log n)| comparisons.

      :rtype: value

   .. _sorteddict.popitem:
   .. method:: d.popitem()

      Remove and return the ``(key, value)`` pair with the least *key*
      from the dictionary.

      If the dictionary is empty, calling :meth:`popitem` raises a
      :exc:`KeyError`.

      Requires |theta(log n)| operations and no comparisons.

      :rtype: ``(key, value)`` tuple

   .. method:: d.setdefault(key[, default])

       If *key* is in the dictionary, return its value.  If not,
       insert *key* with a value of *default* and return
       *default*.  *default* defaults to ``None``.

       Requires |theta(1)| operations if *key* is already in the
       dictionary.  Otherwise, requires |theta(log n)| comparisons.

   .. method:: d.update(other, ...)

      Update the dictionary with the key/value pairs from *other*,
      overwriting existing keys.

      :meth:`update` accepts either another dictionary object or an
      iterable of key/value pairs (as a tuple or other iterable of
      length two).  If keyword arguments are specified, the dictionary
      is then updated with those key/value pairs: ``d.update(red=1,
      blue=2)``.

      In the average case, requires |theta(m log**2(n + m))| operations
      and |theta(m log(n + m))| comparisons, where *m* is the combined
      size of all the other sets and *n* is the initial size of *d*.

   .. method:: d.values()

      In Python 2, returns a blist of the dictionary's values.

      In Python 3, returns a new :class:`ValuesView` of the dictionary's
      values.  In addition to the methods provided by the built-in `view
      <http://docs.python.org/release/3.1/library/stdtypes.html#dictionary-view-objects>`_,
      the :class:`ValuesView` is indexable (e.g., ``d.values()[5]``).

      Requires |theta(n)| operations.

      :rtype: :class:`blist` or :class:`ValuesView`

.. class:: KeysView

   A KeysView object is a dynamic view of the dictionary's keys, which
   means that when the dictionary's keys change, the view reflects
   those changes.

   The KeysView class implements the Set_ and
   Sequence_ Abstract Base Classes.

   .. method:: len(keysview)

      Returns the number of entries in the dictionary.

      Requires |theta(1)| operations.

      :rtype: :class:`int`

   .. method:: iter(keysview)

      Returns an iterator over the keys in the dictionary.  Keys are
      iterated over in their sorted order.

      Iterating views while adding or deleting entries in the dictionary
      may raise a :exc:`RuntimeError` or fail to iterate over all
      entries.

      Requires |theta(log n)| operations to create the iterator.  Each
      element from the iterator requires |theta(1)| operations to
      retrieve, or |theta(n)| operations to iterate over the entire
      dictionary.

      :rtype: iterator

   .. method:: x in keysview

      Returns ``True`` iff *x* is one of the underlying dictionary's
      keys.

      Requires |theta(1)| operations.

      :rtype: :class:`bool`

   .. method:: keysview[i]

      Returns the key at position *i*.

      Requires |theta(1)| operations if the dictionary's size has not
      been changed recently.  Otherwise, requires |theta(log n)|
      operations.

      :rtype: value

   .. method:: keysview & other

      Returns the intersection of the keys and the other object as
      a new set.

      Requires |theta(m log**2(n + m))| operations and |theta(m
      log(n + m))| comparisons, where *m* is the size of *other* and
      *n* is the size of *keysview*.

      :rtype: :class:`sortedset`

   .. method:: keysview | other

      Returns the union of the keys and the other object as a new set.

      Requires |theta(m log**2(n + m))| operations or |theta(m log(n +
      m))| comparisons, where *m* is the size of *other* 
      and *n* is the size of *S*.

      :rtype: :class:`sortedset`

   .. describe:: keysview - other

      Return the difference between the keys and the other object (all
      keys that aren't in *other*) as a new set.

      Requires |theta(m log**2(n + m))| operations and |theta(m
      log(n + m))| comparisons, where *m* is the size of *other* and
      *n* is the size of *keysview*.

      :rtype: :class:`sortedset`

   .. describe:: keysview ^ other

      Return the symmetric difference (all elements either in the keys
      or *other*, but not in both) of the keys and the other object as
      a new set.

      Requires |theta(m log**2(n + m))| operations and |theta(m
      log(n + m))| comparisons, where *m* is the size of *other* and
      *n* is the size of *keysview*.

      :rtype: :class:`sortedset`

   .. _sortedlist.bisect_left:
   .. method:: L.bisect_left(value)

      Similarly to the ``bisect`` module in the standard library, this
      returns an appropriate index to insert *value* in *L*. If *value* is
      already present in *L*, the insertion point will be before (to the
      left of) any existing entries.

      Requires |theta(log**2 n)| total operations or |theta(log n)|
      comparisons.

   .. method:: L.bisect(value)

      Same as :ref:`bisect_left <sortedlist.bisect_right>`.

   .. method:: L.bisect_right(value)

      Same thing as :ref:`bisect_left <sortedlist.bisect_left>`, but if
      *value* is already present in *L*, the insertion point will be after
      (to the right of) any existing entries.

   .. method:: keysview.count(key)

      Returns the number of occurrences of *key* in the set.

      Requires |theta(1)| operations.

      :rtype: :class:`int`

   .. method:: keysview.index(key, [start, [stop]])

      Returns the smallest *k* such that :math:`keysview[k] == x` and
      :math:`i <= k < j`.  Raises :exc:`KeyError` if *key* is not
      present.  *stop* defaults to the end of the set.  *start*
      defaults to the beginning.  Negative indexes are supported, as
      for slice indices.

      Requires |theta(1)| operations.

      :rtype: :class:`int`

.. class:: ValuesView

   A ValuesView object is a dynamic view of the dictionary's values,
   which means that when the dictionary's values change, the view
   reflects those changes.

   The ValuesView class implements the Sequence_ Abstract Base
   Class.

   .. method:: len(valuesview)

      Returns the number of entries in the dictionary.

      Requires |theta(1)| operations.

      :rtype: :class:`int`

   .. method:: iter(valuesview)

      Returns an iterator over the values in the dictionary.  Values are
      iterated over in sorted order of the keys.

      Iterating views while adding or deleting entries in the dictionary
      may raise a :exc:`RuntimeError` or fail to iterate over all
      entries.

      Requires |theta(log n)| operations to create the iterator.  Each
      element from the iterator requires |theta(1)| operations to
      retrieve, or |theta(n)| operations to iterate over the entire
      dictionary.

      :rtype: iterator

   .. method:: x in valuesview

      Returns ``True`` iff *x* is one of the underlying dictionary's
      values.

      Requires |theta(n)| operations.

      :rtype: :class:`bool`

   .. method:: valuesview[i]

      Returns the value at position *i*.

      Requires |theta(1)| operations if the dictionary's size has not
      been changed recently.  Otherwise, requires |theta(log n)|
      operations.

      :rtype: value

   .. method:: valuesview.count(value)

      Returns the number of occurrences of *value* in the set.

      Requires |theta(n)| operations and |theta(n)| comparisons in the
      worst case.

      :rtype: :class:`int`

   .. method:: valuesview.index(value, [start, [stop]])

      Returns the smallest *k* such that :math:`valuesview[k] == x`
      and :math:`i <= k < j`.  Raises :exc:`KeyError` if *value* is not
      present.  *stop* defaults to the end of the set.  *start*
      defaults to the beginning.  Negative indexes are supported, as
      for slice indices.

      In the worst case, requires |theta(stop-start)| operations and
      |theta(stop-start)| comparisons.

      :rtype: :class:`int`

.. class:: ItemsView

   A ItemsView object is a dynamic view of the dictionary's ``(key,
   value)`` pairs, which means that when the dictionary changes, the
   view reflects those changes.

   The ItemsView class implements the Set_ and
   Sequence_ Abstract Base Classes.  However, the set-like
   operations (``&``, ``|``, ``-``, ``^``) will only operate correctly
   if all of the dictionary's values are hashable.

   .. method:: len(itemsview)

      Returns the number of entries in the dictionary.

      Requires |theta(1)| operations.

      :rtype: :class:`int`

   .. method:: iter(itemsview)

      Returns an iterator over the items in the dictionary.  Items are
      iterated over in sorted order of the keys.

      Iterating views while adding or deleting entries in the dictionary
      may raise a :exc:`RuntimeError` or fail to iterate over all
      entries.

      Requires |theta(log n)| operations to create the iterator.  Each
      element from the iterator requires |theta(1)| operations to
      retrieve, or |theta(n)| operations to iterate over the entire
      dictionary.

      :rtype: iterator

   .. method:: x in itemsview

      Returns ``True`` iff *x* is one of the underlying dictionary's
      items.

      Requires |theta(1)| operations.

      :rtype: :class:`bool`

   .. method:: itemsview[i]

      Returns the ``(key, value)`` pair at position *i*.

      Requires |theta(1)| operations if the dictionary's size has not
      been changed recently.  Otherwise, requires |theta(log n)|
      operations.

      :rtype: item

   .. method:: itemsview & other

      Returns the intersection of the items and the other object as
      a new set.

      Requires |theta(m log**2(n + m))| operations and |theta(m
      log(n + m))| comparisons, where *m* is the size of *other* and
      *n* is the size of *itemsview*.

      :rtype: :class:`sortedset`

   .. method:: itemsview | other

      Returns the union of the items and the other object as a new set.

      Requires |theta(m log**2(n + m))| operations or |theta(m log(n +
      m))| comparisons, where *m* is the size of *other* 
      and *n* is the size of *S*.

      :rtype: :class:`sortedset`

   .. describe:: itemsview - other

      Return the difference between the items and the other object (all
      items that aren't in *other*) as a new set.

      Requires |theta(m log**2(n + m))| operations and |theta(m
      log(n + m))| comparisons, where *m* is the size of *other* and
      *n* is the size of *itemsview*.

      :rtype: :class:`sortedset`

   .. describe:: itemsview ^ other

      Return the symmetric difference (all elements either in the items
      or *other*, but not in both) of the items and the other object as
      a new set.

      Requires |theta(m log**2(n + m))| operations and |theta(m
      log(n + m))| comparisons, where *m* is the size of *other* and
      *n* is the size of *itemsview*.

      :rtype: :class:`sortedset`

   .. method:: itemsview.count(item)

      Returns the number of occurrences of *item* in the set.

      Requires |theta(1)| operations.

      :rtype: :class:`int`

   .. method:: itemsview.index(item, [start, [stop]])

      Returns the smallest *k* such that :math:`itemsview[k] == x` and
      :math:`i <= k < j`.  Raises :exc:`KeyError` if *item* is not
      present.  *stop* defaults to the end of the set.  *start*
      defaults to the beginning.  Negative indexes are supported, as
      for slice indices.

      Requires |theta(1)| operations.

      :rtype: :class:`int`

.. _Set: http://docs.python.org/release/3.1/library/collections.html#abcs-abstract-base-classes
.. _Sequence: http://docs.python.org/release/3.1/library/collections.html#abcs-abstract-base-classes
