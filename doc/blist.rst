.. include:: mymath.txt

blist
=====

.. currentmodule:: blist

.. class:: blist(iterable)

    The :class:`blist` is a drop-in replacement for the Python
    :class:`list` that provides better performance when modifying large
    lists. For small lists, the :class:`blist` and the built-in
    :class:`list` have virtually identical performance.

    To use the :class:`blist`, you simply change code like this:

        >>> items = [5, 6, 2]
        >>> more_items = function_that_returns_a_list()

    to:

        >>> from blist import blist
        >>> items = blist([5, 6, 2])
        >>> more_items = blist(function_that_returns_a_list())

    Python's built-in :class:`list` is a dynamically-sized array; to
    insert or remove an item from the beginning or middle of the
    list, it has to move most of the list in memory, i.e., |theta(n)|
    operations.  The :class:`blist` uses a flexible, hybrid array/tree
    structure and only needs to move a small portion of items in
    memory, specifically using |theta(log n)| operations.

    Creating a :class:`blist` from another :class:`blist` requires
    |theta(1)| operations.  Creating a :class:`blist` from any other
    iterable requires |theta(n)| operations.

   .. method:: L + L2, L2 + L

      :type L: blist
      :type L2: list or blist

      Returns a new blist by concatenating two lists.

      If the other list is also a blist, requires |theta(log m + log
      n)| operations.  If it's a built-in :class:`list`, requires
      |theta(m + log n)| operations, where *m* is the size of the other
      list and *n* is the size of *L*.

      :rtype: :class:`blist`

   .. method:: x in L

      Returns True if and only if x is an element in the list.

      Requires |theta(n)| operations in the worst case.

      :rtype: :class:`bool`

   .. method:: del L[i]

      Removes the element located at index *i* from the list.

      Requires |theta(log n)| operations.

   .. method:: del L[i:j]

      Removes the elements from *i* to *j* from the list.

      Requires |theta(log n)| operations.

   .. method:: L == L2, L != L2, L < L2, L <= L2, L > L2, L >= L2

      Compares two lists.  For full details see `Comparisons
      <http://docs.python.org/reference/expressions.html#notin>`_ in
      the Python language reference.

      Requires |theta(n)| operations in the worst case.

      :rtype: :class:`bool`

   .. method:: L[i]

      Returns the element at position *i*.

      Requires |theta(log n)| operations in the worst case but only
      |theta(1)| operations if the list's size has not been changed
      recently.

      :rtype: item

   .. method:: L[i:j]

      Returns a new blist containing the elements from *i* to *j*.

      Requires |theta(log n)| operations.

      :rtype: :class:`blist`

   .. method:: L += iterable

      The same as ``L.extend(iterable)``.

   .. method:: L *= k

      Increase the length of the list by a factor of *k*, by appending
      *k-1* shallow copies of the list.

      Requires |theta(log k)| operations.

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

      Returns a new blist contained k shallow copies of L concatenated.

      Requires |theta(log k)| operations.

      :rtype: :class:`blist`

   .. method: reversed(L)

      Creates an iterator to traverse the list in reverse.

      Requires |theta(log n)| operations to create the iterator.  Each
      element from the iterator requires |theta(1)| operations to
      retrieve, or |theta(n)| operations to iterate over the entire
      list.

      :rtype: iterator

   .. method: L[i] = value

      Replace the item at index *i* with *value*.

      Requires |theta(log n)| operations in the worst case but only
      |theta(1)| operations if the list's size has not been changed
      recently.

   .. method: L[i:j] = iterable

      Replaces the items at indices *i* through *j* with the items
      from *iterable*.

      If *iterable* is a :class:`blist`, requires |theta(log m + log
      n)| operations.  Otherwise, requires |theta(m + log n)|
      operations, where *k* is the length of *iterable* and *n* is the
      initial length of *L*

   .. _blist.append:
   .. method:: L.append(object)

      Append object to the end of the list.

      Requires amortized |theta(1)| operations.

   .. method:: L.count(value)

      Returns the number of occurrences of *value* in the list.

      Requires |theta(n)| operations in the worst case.

      :rtype: :class:`int`

   .. method:: L.extend(iterable)

      Extend the list by appending all elements from the iterable.

      If iterable is a blist, requires |theta(log m + log n)|
      operations.  Otherwise, requires |theta(m + log n)| operations,
      where *m* is the size of the iterable and *n* is the size of the
      list initially.

   .. method:: L.index(value, [start, [stop]])

      Returns the smallest *k* such that :math:`s[k] == x` and
      :math:`i <= k < j`.  Raises ValueError if *value* is not
      present.  *stop* defaults to the end of the list.  *start*
      defaults to the beginning.  Negative indexes are supported, as
      for slice indices.

      Requires |theta(stop-start)| operations in the worst case.

      :rtype: :class:`int`

   .. _blist.insert:
   .. method:: L.insert(index, object)

      Inserts object before index.  The same as s[i:i] = [x].  Negative
      indexes are supported, as for slice indices.

      Requires |theta(log n)| operations.

   .. method:: L.pop([index])

      Removes and return item at index (default last).  Raises
      IndexError if list is empty or index is out of range.  Negative
      indexes are supported, as for slice indices.

      Requires |theta(log n)| operations.

      :rtype: item

   .. method:: L.remove(value)

      Removes the first occurrence of *value*.  Raises ValueError if
      *value* is not present.

      Requires |theta(n)| operations in the worst case.

   .. method:: L.reverse()

      Reverse the list *in place*.

      Requires |theta(n)| operations.

   .. method:: L.sort(cmp=None, key=None, reverse=False)

      Stable sort *in place*.

      *cmp* is suppoted in Python 2 for compatibility with Python
      2's list.sort.  All users are encouraged to migrate to using the
      `key` parameter, which is more efficient.

      *key* specifies a function of one argument that is used to
      extract a comparison key from each list element:
      ``key=str.lower``. The default value is ``None`` (compare the
      elements directly).

      *reverse* is a boolean value. If set to ``True``, then the list
      elements are sorted as if each comparison were reversed.

      Requires |theta(n log n)| operations in the worst and average
      case and |theta(n)| operation in the best case.
