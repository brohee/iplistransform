iplistransform
==============

The command takes as input address lists in the PeerGuardian P2P plaintext
format (see https://en.wikipedia.org/wiki/PeerGuardian#P2P%20plaintext%20format),
where each line is in the following format 

RangeName:aaa.bbb.ccc.ddd-eee.fff.ggg.hhh

where aaa.bbb.ccc.ddd-eee.fff.ggg.hhh represents a continuous range of
IP addresses. Those range are not always describing a CIDR network so we
may have to split it in a set of netmaskable entities.

We convert that to a list of ipaddress/netmask, one per line,
suitable to use with OpenBSD's pfctl.

To build the tool, just issue the command "make iplistransform".
