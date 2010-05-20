.. include:: mymath.txt

sortedlist
==========

.. currentmodule:: blist

.. class:: sortedlist(iterable=(), key=None)

   A :class:`sortedlist` provides most of the same methods as a
   :class:`blist` (and, hence, a :class:`list`), but keeps the items
   in sorted order.  Methods that would insert an item at a particular
   location are not included (e.g., :ref:`append <blist.append>`,
   :ref:`insert <blist.insert>`).  To add an element to the sortedlist, use
   :ref:`add <sortedlist.add>`.  To add several elements, use
   :ref:`merge <sortedlist.merge>`.  To removal an element, use
   :ref:`discard <sortedlist.discard>`, :ref:`remove <sortedlist.remove>`, or
   :ref:`del L[i] <sortedlist.delitem>`.

   An optional *iterable* provides an initial series of items to
   populate the :class:`sortedlist`.

   *key* specifies a function of one argument that is used to extract
   a comparison key from each list element: ``key=str.lower``. The
   default value is ``None`` (compare the elements directly).  The
   *key* function must always return the same key for an item or the
   results are unpredicable.

   .. method:: x in L

      Returns True if and only if x is an element in the list.

      Requires |theta(log**2 n)| total operations or |theta(log n)|
      comparisons.

      :rtype: :class:`bool`

   .. _sortedlist.delitem:
   .. method:: del L[i]

      Removes the element located at index *i* from the list.

      Requires |theta(log n)| operations and no comparisons.

   .. method:: del L[i:j]

      Removes the elements from *i* to *j* from the list.

      Requires |theta(log n)| operations and no comparisons.

   .. method:: L == L2, L != L2, L < L2, L <= L2, L > L2, L >= L2

      Compares two lists.  For full details see `Comparisons
      <http://docs.python.org/reference/expressions.html#notin>`_ in
      the Python language reference.

      In the worst case, requires |theta(n)| operations and |theta(n)|
      comparisons.

      :rtype: :class:`bool`

   .. method:: L[i]

      Returns the element at position *i*.

      Requires |theta(log n)| operations in the worst case but only
      |theta(1)| operations if the list's size has not be changed
      recently.  Requires no comparisons in any case.

      :rtype: item

   .. method:: L[i:j]

      Returns a new :class:`sortedlist` containing the elements from
      *i* to *j*.

      Requires |theta(log n)| operations and no comparisons.

      :rtype: :class:`sortedlist`

   .. method:: L *= k

      Increase the length of the list by a factor of *k*, by inserting
      *k-1* additional shallow copies of each item in the list.

      Requires |theta(n log(k + n))| operations and no comparisons.

   .. method:: iter(L)

      Creates an iterator over the list.

      Requires |theta(log n)| operations to create the iterator.  Each
      element from the iterator requires |theta(1)| operations to
      retrieve, or |theta(n)| operations to iterate over the entire
      list.

      :rtype: iterator

   .. method: len(L)

      Returns the number of elements in the list.

      Requires |theta(1)| operations.

      :rtype: :class:`int`

   .. method: L * k or k * L

      Returns a new sorted list containing *k* shallow copies of each
      item in L.

      Requires |theta(n log(k + n))| operations and no comparisons.

      :rtype: :class:`sortedlist`

   .. method: reversed(L)

      Creates an iterator to traverse the list in reverse.

      Requires |theta(log n)| operations to create the iterator.  Each
      element from the iterator requires |theta(1)| operations to
      retrieve, or |theta(n)| operations to iterate over the entire
      list.  Requires no comparisons in any case.

      :rtype: iterator

   .. _sortedlist.add:
   .. method:: L.add(value)

      Add the element *value* to the list.

      Requires |theta(log**2 n)| total operations or |theta(log n)|
      comparisons.

   .. method:: L.count(value)

      Returns the number of occurrences of *value* in the list.

      Requires |theta(n)| operations and |theta(n)| comparisons in the
      worst case.

      :rtype: :class:`int`

   .. _sortedlist.discard:
   .. method:: L.discard(value)

      Removes the first occurrence of *value*.  If *value* is not a
      member, does nothing.

      In the worst case, requires |theta(log**2 n)| operations and
      |theta(log n)| comparisons.

   .. method:: L.index(value, [start, [stop]])

      Returns the smallest *k* such that :math:`L[k] == x` and
      :math:`i <= k < j`.  Raises ValueError if *value* is not
      present.  stop defaults to the end of the list.  start defaults
      to the beginning.  Negative indexes are supported, as for slice
      indices.

      In the worst case, requires |theta(stop-start)| operations and
      |theta(stop-start)| comparisons.

      :rtype: :class:`int`

   .. _sortedlist.merge:
   .. method:: L.merge(iterable)

      Grow the list by inserting all elements from the iterable.

      Requires |theta(m log**2(n + m))| operations and |theta(m log(n
      + m))| comparisons, where *m* is the size of the iterable and *n* is
      the size of the list initially.

   .. method:: L.pop([index])

      Removes and return item at index (default last).  Raises
      IndexError if list is empty or index is out of range.  Negative
      indexes are supported, as for slice indices.

      Requires |theta(log n)| operations and no comparisons.

      :rtype: item

   .. _sortedlist.remove:
   .. method:: L.remove(value)

      Remove first occurrence of *value*.  Raises ValueError if
      *value* is not present.

      In the worst case, requires |theta(log**2 n)| operations and
      |theta(log n)| comparisons.
