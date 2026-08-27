# Solaris 8 header findings

These findings are based on headers collected from the target Sun Blade 100.

Target:
- Solaris 8 HW 7/03
- SunOS 5.8 Generic_108528-22
- sun4u / SPARC
- 32-bit sparc and 64-bit sparcv9 application ABIs
- GCC 2.95.3 installed from SFWgcc
- SUNWhea, SUNWarc/SUNWarcx, SUNWtoo/SUNWtoox, SUNWsprot installed

## Important DLPI facts

The target sys/dlpi.h provides:
- DL_VERSION_2
- DL_OTHER
- DL_CLDLS
- DL_STYLE1
- DL_UNBOUND and DL_IDLE
- DL_INFO_REQ / DL_INFO_ACK
- DL_BIND_REQ / DL_BIND_ACK
- DL_UNBIND_REQ
- DL_OK_ACK / DL_ERROR_ACK
- DL_UNITDATA_REQ / DL_UNITDATA_IND

This confirms that a point-to-point SLIP provider can honestly advertise DL_OTHER instead of pretending to be DL_ETHER.

## Differences from the AMIX slip.c donor

AMIX's DL_INFO_ACK initializer is too short for Solaris 8. Solaris 8 adds/uses:
- dl_sap_length
- dl_version
- dl_brdcst_addr_length
- dl_brdcst_addr_offset

Those must be initialized. For SunSlip, likely values are:
- dl_sap_length = 0
- dl_version = DL_VERSION_2
- broadcast address length/offset = 0

AMIX's DL_BIND_ACK code writes dl_growth; Solaris 8 dl_bind_ack_t has dl_xidtest_flg instead. Set it to 0.

AMIX uses DL_ETHER for dl_mac_type. Solaris 8 defines DL_OTHER (0x09), which is the better value for SLIP.

AMIX simply sets state to zero for DL_UNBIND_REQ. Solaris 8 should return a DL_OK_ACK and transition from DL_IDLE to DL_UNBOUND.

## STREAMS / loadable module facts

sys/modctl.h supports both:
- struct modldrv for the DLPI character/STREAMS driver
- struct modlstrmod for the tty-side STREAMS module

A single loadable module can therefore contain both linkages in one struct modlinkage.

sys/conf.h defines fmodsw_t:
- f_name[FMNAMESZ+1]
- struct streamtab *f_str
- int f_flag

So the AMIX two-half design maps cleanly onto Solaris:
1. DLPI style-1 pseudo STREAMS driver (e.g. /dev/sunslip0)
2. pushable tty module (e.g. I_PUSH "sunslip")

## Driver framework facts

Solaris 8 struct cb_ops contains the STREAMS table pointer cb_str plus cb_flag/cb_rev.
struct dev_ops layout matches the existing slmux proof-of-concept approach.

The final module should include sys/ddi.h and sys/sunddi.h after the other Solaris headers.

## Porting direction

Primary donor: hydra-amix usr/sys/amiga/driver/slip.c
Secondary donors:
- cslip-2.7 SunOS 4 for historical SLIP/CSLIP behavior and VJ compression
- hydra-amix A2065/Hydra DLPI drivers
- amix-z3660net for a newer cleaned-up SVR4 DLPI implementation

Keep the AMIX architecture:
- tty STREAMS module performs RFC 1055 framing/unframing
- DLPI style-1 provider talks to Solaris IP
- both halves share per-interface state

Replace AMIX-specific pieces:
- old ifstats linkage
- net/strioc.h and netinet/ip_str.h assumptions
- AMIX ioctl details
- kernel registration / cdevsw plumbing
- obsolete splstr-based serialization if unnecessary under Solaris D_MP design

First bring-up should be plain SLIP only, one interface, no VJ compression.
