iplistransform
==============

Transform address lists  in the format

Some random junk:aaa.bbb.ccc.ddd-eee.fff.ggg.hhh

where aaa.bbb.ccc.ddd-eee.fff.ggg.hhh represents a continuous range of
IP addresses. Those range are not always a CIDR network so we may have
to split it in a set of netmaskable entities.

We convert that to a list of ipaddress/netmask, one per line,
suitable to use with OpenBSD's pfctl and likely some other packages.
