.. include:: mymath.txt

btuple
======

.. currentmodule:: blist

.. class:: btuple(iterable)

    The :class:`btuple` is a drop-in replacement for the Python
    :class:`tuple` that provides better performance for some
    operations on large tuples.  Operations such as concatenation,
    taking a slice, and converting from a :class:`blist` are
    inexpensive.

    The current implementation of the :class:`btuple` is a Python
    wrapper around the :class:`blist`.  Consequently, for small
    tuples the built-in :class:`tuple` always provides better
    performance.  In a future version, the :class:`btuple` may be
    implemented in C; it would then have nearly identical performance
    to the built-in :class:`tuple` for small tuples.

    To use the :class:`btuple`, you simply change code like this:

        >>> items = (5, 6, 2)
        >>> more_items = function_that_returns_a_tuple()

    to:

        >>> from btuple import btuple
        >>> items = btuple((5, 6, 2))
        >>> more_items = btuple(function_that_returns_a_tuple())

    Creating a :class:`btuple` from another :class:`btuple` or a
    :class:`blist` requires |theta(1)| operations.  Creating a
    :class:`btuple` from any other iterable requires |theta(n)| operations.

   .. method:: L + L2, L2 + L

      :type L: btuple
      :type L2: tuple or btuple

      Returns a new btuple by concatenating two tuples.

      If the other tuple is also a btuple, requires |theta(log m + log
      n)| operations.  If it's a built-in :class:`tuple`, requires
      |theta(m + log n)| operations, where *m* is the size of the other
      tuple and *n* is the size of *L*.

      :rtype: :class:`btuple`

   .. method:: x in L

      Returns True if and only if x is an element in the tuple.

      Requires |theta(n)| operations in the worst case.

      :rtype: :class:`bool`

   .. method:: L == L2, L != L2, L < L2, L <= L2, L > L2, L >= L2

      Compares two tuples.  For full details see `Comparisons
      <http://docs.python.org/reference/expressions.html#notin>`_ in
      the Python language reference.

      Requires |theta(n)| operations in the worst case.

      :rtype: :class:`bool`

   .. method:: L[i]

      Returns the element at position *i*.

      Requires |theta(1)| operations in the amortized worst case.

      :rtype: item

   .. method:: L[i:j]

      Returns a new btuple containing the elements from *i* to *j*.

      Requires |theta(log n)| operations.

      :rtype: :class:`btuple`

   .. method:: iter(L)

      Creates an iterator over the tuple.

      Requires |theta(log n)| operations to create the iterator.  Each
      element from the iterator requires |theta(1)| operations to
      retrieve, or |theta(n)| operations to iterate over the entire
      tuple.

      :rtype: iterator

   .. method: len(L)

      Returns the number of elements in the tuple.

      Requires |theta(1)| operations.

      :rtype: :class:`int`

   .. method: L * k or k * L

      Returns a new btuple contained k shallow copies of L concatenated.

      Requires |theta(log k)| operations.

      :rtype: :class:`btuple`

   .. method: reversed(L)

      Creates an iterator to traverse the tuple in reverse.

      Requires |theta(log n)| operations to create the iterator.  Each
      element from the iterator requires |theta(1)| operations to
      retrieve, or |theta(n)| operations to iterate over the entire
      tuple.

      :rtype: iterator

   .. method:: L.count(value)

      Returns the number of occurrences of *value* in the tuple.

      Requires |theta(n)| operations in the worst case.

      :rtype: :class:`int`

   .. method:: L.index(value, [start, [stop]])

      Returns the smallest *k* such that :math:`s[k] == x` and
      :math:`i <= k < j`.  Raises ValueError if *value* is not
      present.  *stop* defaults to the end of the tuple.  *start*
      defaults to the beginning.  Negative indexes are supported, as
      for slice indices.

      Requires |theta(stop-start)| operations in the worst case.

      :rtype: :class:`int`
