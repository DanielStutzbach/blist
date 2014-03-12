.. include:: mymath.txt

sortedset
==========

.. currentmodule:: blist

.. class:: sortedset(iterable=(), key=None)

   A :class:`sortedset` provides the same methods as a :class:`set`.
   Additionally, a :class:`sortedset:` maintains its items in sorted
   order, allowing the :class:`sortedset` to be indexed.

   An optional *iterable* provides an initial series of items to
   populate the :class:`sortedset`.

   *key* specifies a function of one argument that is used to extract
   a comparison key from each set element: ``key=str.lower``. The
   default value is ``None`` (compare the elements directly).  The
   *key* function must always return the same key for an item or the
   results are unpredictable.

   Unlike a :class:`set`, a :class:`sortedset` does not require items
   to be hashable.

   A :class:`sortedset` can be used as an order statistic tree
   (Cormen *et al.*, *Introduction to Algorithms*, ch. 14).

   .. method:: x in S

      Returns True if and only if *x* is an element in the set.

      Requires |theta(log**2 n)| total operations or |theta(log n)|
      comparisons.

      :rtype: :class:`bool`

   .. _sortedset.delitem:
   .. method:: del S[i]

      Removes the element located at index *i* from the set.

      Requires |theta(log n)| operations and no comparisons.

   .. method:: del S[i:j]

      Removes the elements from *i* to *j* from the set.

      Requires |theta(log n)| operations and no comparisons.

   .. method:: S < S2

      Test whether the set is a proper subset of *S2*, that is, ``S <= S2
      and S != other``.

      In the worst case, requires |theta(n)| operations multiplied by
      the cost of *S2*'s `in` operation.

      :rtype: :class:`bool`

   .. method:: S > S2

      Test whether the set is a proper superset of *S2*, that is, ``S
      >= S2 and S != S2``.

      In the worst case, requires |theta(m log**2 n)| operations or
      |theta(m log n)| comparisons, where *m* is the size of *S2* and
      *n* is the size of *S*.

      :rtype: :class:`bool`

   .. method:: S[i]

      Returns the element at position *i*.

      Requires |theta(log n)| operations in the worst case but only
      |theta(1)| operations if the set's size has not been changed
      recently.  Requires no comparisons in any case.

      (Cormen *et al.* call this operation "SELECT".)

      :rtype: item

   .. method:: S[i:j]

      Returns a new sortedset containing the elements from *i* to *j*.

      Requires |theta(log n)| operations and no comparisons.

      :rtype: :class:`sortedset`

   .. method:: S *= k

      Increase the length of the set by a factor of *k*, by inserting
      *k-1* additional shallow copies of each item in the set.

      Requires |theta(n log(k + n))| operations and no comparisons.

   .. method:: iter(S)

      Creates an iterator over the set.

      Requires |theta(log n)| operations to create the iterator.  Each
      element from the iterator requires |theta(1)| operations to
      retrieve, or |theta(n)| operations to iterate over the entire
      set.

      :rtype: iterator

   .. method:: len(S)

      Returns the number of elements in the set.

      Requires |theta(1)| operations.

      :rtype: :class:`int`

   .. method:: S * k or k * S

      Returns a new sorted set containing *k* shallow copies of each
      item in S.

      Requires |theta(n log(k + n))| operations and no comparisons.

      :rtype: :class:`sortedset`

   .. method:: reversed(S)

      Creates an iterator to traverse the set in reverse.

      Requires |theta(log n)| operations to create the iterator.  Each
      element from the iterator requires |theta(1)| operations to
      retrieve, or |theta(n)| operations to iterate over the entire
      set.  Requires no comparisons in any case.

      :rtype: iterator

   .. _sortedset.add:
   .. method:: S.add(value)

      Add the element *value* to the set.

      Requires |theta(log**2 n)| total operations or |theta(log n)|
      comparisons.

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

   .. method:: S.clear()

      Remove all elements from the set.

      Requires |theta(n)| total operations and no comparisons in the
      worst case.

   .. method:: S.copy()

      Creates a shallow copy of the set.

      Requires |theta(1)| total operations and no comparisons.

      :rtype: :class:`sortedset`

   .. method:: S.count(value)

      Returns the number of occurrences of *value* in the set.

      Requires |theta(n)| operations and |theta(n)| comparisons in the
      worst case.

      :rtype: :class:`int`

   .. method:: S.difference(S2, ...)
               S - S2 - ...

      Return a new set with elements in the set that are not in the others.

      In the worst case, requires |theta(m log**2(n + m))| operations
      and |theta(m log(n + m))| comparisons, where *m* is the combined
      size of all the other sets and *n* is the size of *S*.

      :rtype: :class:`sortedset`

   .. method:: S.difference_update(S2, ...)
               S -= S2 | ...

      Update the set, removing elements found in keeping only elements
      found in any of the others.

      In the worst case, requires |theta(m log**2(n + m))| operations
      and |theta(m log(n + m))| comparisons, where *m* is the combined
      size of all the other sets and *n* is the initial size of *S*.

   .. _sortedset.discard:
   .. method:: S.discard(value)

      Removes the first occurrence of *value*.  If *value* is not a
      member, does nothing.

      In the worst case, requires |theta(log**2 n)| operations and
      |theta(log n)| comparisons.

   .. method:: S.index(value, [start, [stop]])

      Returns the smallest *k* such that :math:`S[k] == x` and
      :math:`i <= k < j`.  Raises ValueError if *value* is not
      present.  *stop* defaults to the end of the set.  *start*
      defaults to the beginning.  Negative indexes are supported, as
      for slice indices.

      In the worst case, requires |theta(log**2 m)| operations and
      |theta(log m)| comparisons, where *m* is *stop* - *start*.

      (Cormen *et al.* call this operation "RANK".)

      :rtype: :class:`int`

   .. method:: S.intersection(S2, ...)
               S & S2 & ...

      Return a new set with elements common to the set and all others.

      In the worst case, requires |theta(m log**2(n + m))| operations
      and |theta(m log(n + m))| comparisons, where *m* is the combined
      size of all the other sets and *n* is the size of *S*.

      :rtype: :class:`sortedset`

   .. method:: S.intersection_update(S2, ...)
               S &= S2 & ...

      Update the set, keeping only elements found in it and all
      others.

      In the worst case, requires |theta(m log**2(n + m))| operations
      and |theta(m log(n + m))| comparisons, where *m* is the combined
      size of all the other sets and *n* is the initial size of *S*.

   .. method:: S.isdisjoint(S2)

      Return True if the set has no elements in common with *S2*.
      Sets are disjoint if and only if their intersection is the empty
      set.

      :rtype: :class:`bool`

   .. method:: S.issubset(S2)
               S <= S2

      Test whether every element in the set is in *S2*

      In the worst case, requires |theta(n)| operations multiplied by
      the cost of *S2*'s `in` operation.

      :rtype: :class:`bool`

   .. method:: S.issuperset(S2)
              S >= S2

      Test whether every element in *S2* is in the set.

      In the worst case, requires |theta(m log**2 n)| operations or
      |theta(m log n)| comparisons, where *m* is the size of *S2* and
      *n* is the size of *S*.

      :rtype: :class:`bool`

   .. method:: S.symmetric_difference(S2)
               S ^ S2

      Return a new set with element in either set but not both.

      In the worst case, requires |theta(m log**2(n + m))| operations
      and |theta(m log(n + m))| comparisons, where *m* is the size of
      *S2* and *n* is the size of *S*.

      :rtype: :class:`sortedset`

   .. method:: S.symmetric_difference_update(S2)
               S ^= S2

      Update the set, keeping only elements found in either set, but
      not in both.

      In the worst case, requires |theta(m log**2(n + m))| operations
      and |theta(m log(n + m))| comparisons, where *m* is the size of
      *S2* and *n* is the initial size of *S*.

   .. method:: S.pop([index])

      Removes and return item at index (default last).  Raises
      IndexError if set is empty or index is out of range.  Negative
      indexes are supported, as for slice indices.

      Requires |theta(log n)| operations and no comparisons.

      :rtype: item

   .. _sortedset.remove:
   .. method:: S.remove(value)

      Remove first occurrence of *value*.  Raises ValueError if
      *value* is not present.

      In the worst case, requires |theta(log**2 n)| operations and
      |theta(log n)| comparisons.

   .. method:: S.union(S2, ...)
               S | S2 | ...

      Return a new sortedset with elements from the set and all
      others.  The new sortedset will be sorted according to the key
      of the leftmost set.

      Requires |theta(m log**2(n + m))| operations or |theta(m log(n +
      m))| comparisons, where *m* is the total size of the other sets
      and *n* is the size of *S*.

      :rtype: :class:`sortedset`

   .. method:: S.update(S2, ...)
               S |= S2 | ...

      Update the set, adding elements from all others.

      In the worst case, requires |theta(m log**2(n + m))| operations
      and |theta(m log(n + m))| comparisons, where *m* is the combined
      size of all the other sets and *n* is the initial size of *S*.
