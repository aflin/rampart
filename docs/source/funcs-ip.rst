Internet/IP address functions
-----------------------------

The following functions manipulate IP network and/or host addresses;
most take ``inet`` style argument(s). This is an IPv4 address string,
optionally followed by a netmask.

For IPv4, the format is dotted-decimal, i.e.
:math:`N`\ [.\ :math:`N`\ [.\ :math:`[N`\ .\ :math:`N`]]] where
:math:`N` is a decimal, octal or hexadecimal integer from 0 to 255. If
:math:`x < 4` values of :math:`N` are given, the last :math:`N` is taken
as the last :math:`5-x` bytes instead of 1 byte, with missing bytes
padded to the right. E.g. 192.258 is valid and equivalent to 192.1.2.0:
the last :math:`N` is 2 bytes in size, and covers 5 - 2 = 3 needed
bytes, including 1 zero pad to the right. Conversely, 192.168.4.1027 is
not valid: the last :math:`N` is too large.

An IPv4 address may optionally be followed by a netmask, either of the
form /\ :math:`B` or :\ :math:`IPv4`, where :math:`B` is a decimal,
octal or hexadecimal netmask integer from 0 to 32, and :math:`IPv4` is a
dotted-decimal IPv4 address of the same format described above. If an
:\ :math:`IPv4` netmask is given, only the largest contiguous set of
most-significant 1 bits are used (because netmasks are contiguous). If
no netmask is given, it will be calculated from standard IPv4 class
A/B/C/D/E rules, but will be large enough to include all given bytes of
the IP. E.g. 1.2.3.4 is Class A which has a netmask of 8, but the
netmask will be extended to 32 to include all 4 given bytes.

-  | ``inetabbrev(inet)``
   | Returns a possibly shorter-than-canonical representation of
     ``$inet``, where trailing zero byte(s) of an IPv4 address may be
     omitted. All bytes of the network, and leading non-zero bytes of
     the host, will be included. E.g. returns 192.100.0/24. The
     /\ :math:`B` netmask is included, except if the network is host-only
     (i.e.netmask is the full size of the IP address). Empty string is
     returned on error.

-  | ``inetcanon(inet)``
   | Returns canonical representation of ``$inet``. For IPv4, this is
     dotted-decimal with all 4 bytes. The /\ :math:`B` netmask is
     included, except if
     the network is host-only (i.e. netmask is the full size of the IP
     address). Empty string is returned on error.

-  | ``inetnetwork(inet)``
   | Returns string IP address with the network bits of ``inet``, and
     the host bits set to 0. Empty string is returned on error.

-  | ``inethost(inet)``
   | Returns string IP address with the host bits of ``inet``, and the
     network bits set to 0. Empty string is returned on error.

-  | ``inetbroadcast(inet)``
   | Returns string IP broadcast address for ``inet``, i.e. with the
     network bits, and host bits set to 1. Empty string is returned on
     error.

-  | ``inetnetmask(inet)``
   | Returns string IP netmask for ``inet``, i.e. with the network bits
     set to 1, and host bits set to 0. Empty string is returned on
     error.

-  | ``inetnetmasklen(inet)``
   | Returns integer netmask length of ``inet``. -1 is returned on
     error.

-  | ``inetcontains(inetA, inetB)``
   | Returns 1 if ``inetA`` contains ``inetB``, i.e. every address in
     ``inetB`` occurs within the ``inetA`` network. 0 is returned if
     not, or -1 on error.

-  | ``inetclass(inet)``
   | Returns class of ``inet``, e.g. A, B, C, D, E or classless if a
     different netmask is used (or the address is IPv6). Empty string is
     returned on error.

-  | ``inet2int(inet)``
   | Returns integer representation of IP network/host bits of ``$inet``
     (i.e. without netmask); useful for compact storage of address as
     integer(s) instead of string. Returns -1 is returned on error (note
     that -1 may also be returned for an all-ones IP address, e.g.
     255.255.255.255).

-  | ``int2inet(i)``
   | Returns ``inet`` string for 1- or 4-value varint ``$i`` taken as an
     IP address. Since no netmask can be stored in the integer form of
     an IP address, the returned IP string will not have a netmask.
     Empty string is returned on error.

urlcanonicalize
~~~~~~~~~~~~~~~

Canonicalize a URL. Usage:

::

       urlcanonicalize(url[, flags])

Returns a copy of ``url``, canonicalized according to case-insensitive
comma-separated ``flags``, which are zero or more of:

-  | ``lowerProtocol``
   | Lower-cases the protocol.

-  | ``lowerHost``
   | Lower-cases the hostname.

-  | ``removeTrailingDot``
   | Removes trailing dot(s) in hostname.

-  | ``reverseHost``
   | Reverse the host/domains in the hostname. E.g.
     http://host.example.com/ becomes http://com.example.host/. This can
     be used to put the most-significant part of the hostname leftmost.

-  | ``removeStandardPort``
   | Remove the port number if it is the standard port for the protocol.

-  | ``decodeSafeBytes``
   | URL-decode safe bytes, where semantics are unlikely to change. E.g.
     “``%41``” becomes “``A``”, but “``%2F``” remains encoded, because
     it would decode to “``/``”.

-  | ``upperEncoded``
   | Upper-case the hex characters of encoded bytes.

-  | ``lowerPath``
   | Lower-case the (non-encoded) characters in the path. May be used
     for URLs known to point to case-insensitive filesystems, e.g.
     Windows.

-  | ``addTrailingSlash``
   | Adds a trailing slash to the path, if no path is present.

Default flags are all but ``reverseHost``, ``lowerPath``. A flag may be
prefixed with the operator ``+`` to append the flag to existing flags;
``-`` to remove the flag from existing flags; or ``=`` (default) to
clear existing flags first and then set the flag. Operators remain in
effect for subsequent flags until the next operator (if any) is used.
