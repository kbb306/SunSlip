# SunSlip Solaris 8 port plan

This branch is the working port of a plain RFC 1055 SLIP link to Solaris 8/SPARC.

## Architecture chosen

The most useful historical donor is the AMIX/SVR4 `slip.c`: it already separates the driver into a DLPI-facing network stream and a tty-facing STREAMS SLIP module.  `cslip-2.7` remains useful for historical SLIP/CSLIP behavior and, later, Van Jacobson compression.

For Solaris 8 the first implementation will deliberately be simpler:

```
Solaris IP
   |
DLPI Style-1 provider (/dev/sunslip0)
   |
RFC 1055 encode/decode
   |
STREAMS lower link (I_PLINK)
   |
/dev/term/b
```

Only one interface and one serial port are required for initial bring-up.

## Minimum DLPI surface

The initial provider should support only what IPv4 point-to-point operation needs:

* `DL_INFO_REQ` -> `DL_INFO_ACK`
* `DL_BIND_REQ` -> `DL_BIND_ACK`
* `DL_UNBIND_REQ` -> `DL_OK_ACK`
* `DL_UNITDATA_REQ` -> RFC 1055 frame on tty
* received RFC 1055 frame -> `DL_UNITDATA_IND`

Expected properties:

* DLPI version 2
* connectionless service (`DL_CLDLS`)
* Style 1 provider for the first prototype
* zero-length physical/DLSAP address (SLIP has no MAC address)
* point-to-point, no ARP
* MTU initially 1006 to match the historical SVR4 SLIP driver; raise later if both ends permit it

Solaris 8 documentation confirms that network drivers are STREAMS/DLPI providers and that non-IEEE-802 devices may implement DLPI directly instead of GLD.  GLD is therefore not the target for SLIP.

## RFC 1055 framing

Transmit:

* prepend no link-layer header
* byte 0xC0 -> 0xDB 0xDC
* byte 0xDB -> 0xDB 0xDD
* terminate packet with 0xC0

Receive:

* accumulate bytes until 0xC0
* 0xDB 0xDC -> 0xC0
* 0xDB 0xDD -> 0xDB
* empty frames are ignored
* malformed escape sequences drop the current packet
* oversized packets drop the current packet

No CSLIP/VJ compression in the first working version.

## Solaris 8 conversion work

The historical AMIX code cannot simply be compiled because its kernel registration, statistics structures, network ioctl plumbing, and STREAMS conventions are SVR4/AMIX-specific.  The Solaris side should use the already-working `slmux` DDI skeleton:

* `_init`, `_fini`, `_info` via `mod_install`/`mod_remove`
* `dev_ops`/`cb_ops`
* `ddi_create_minor_node`
* Solaris STREAMS `streamtab`
* `qprocson`/`qprocsoff`
* `/usr/kernel/drv/sparcv9` loadable module

The old AMIX `ifstats`, `net/strioc.h`, `netinet/ip_str.h`, `splstr()` assumptions, and direct `slink` integration should not be copied blindly.

## Exact headers to verify on the Blade

The following installed Solaris 8 2/04 headers are canonical for this port:

```
/usr/include/sys/dlpi.h
/usr/include/sys/stream.h
/usr/include/sys/stropts.h
/usr/include/sys/conf.h
/usr/include/sys/devops.h
/usr/include/sys/modctl.h
/usr/include/sys/ddi.h
/usr/include/sys/sunddi.h
/usr/include/sys/socket.h
/usr/include/sys/sockio.h
/usr/include/net/if.h
/usr/include/netinet/in.h
```

Important items to confirm from those exact headers before freezing the source:

* complete field layout of `dl_info_ack_t`
* `DL_VERSION_2`, `DL_STYLE1`, `DL_CLDLS` constants
* `dl_ok_ack_t`, `dl_error_ack_t`, `dl_bind_ack_t`, `dl_unitdata_ind_t`
* `cb_ops` and `dev_ops` initializer layout for this patch level
* `I_PLINK`/`I_PUNLINK` `linkblk` layout
* whether `DL_OTHER` exists and is accepted as `dl_mac_type`; if not, choose the least-wrong Solaris value only for plumbing tests

## Bring-up sequence

1. Build and load the existing raw `slmux` proof-of-concept.
2. Verify `I_PLINK` can persistently link `/dev/term/b` at 19200 8N1 raw with `CLOCAL`.
3. Compile the DLPI/SLIP prototype against the actual Solaris 8 headers.
4. Test DLPI request/ack state transitions without IP configured.
5. Test one manually injected outbound packet and confirm RFC 1055 bytes on ttyb.
6. Loop back/inject one RFC 1055 frame and confirm a `DL_UNITDATA_IND` arrives upstream.
7. Plumb/configure the interface under Solaris IP.
8. Connect the A/UX machine and ping point-to-point.
9. Only after plain SLIP is stable, consider CSLIP/VJ compression from cslip-2.7.

## Safety while testing

`ttya` is the console/teleprinter and must never be opened by SunSlip.  All tests default to `/dev/term/b`.  The driver should refuse a second lower link and should refuse unload while a lower stream is linked.
