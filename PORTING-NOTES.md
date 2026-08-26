# SunSlip: cslip-2.7 -> Solaris 8 porting notes

## Donor examined

The uploaded `cslip-2.7.tar.Z` contains the SunOS 4 STREAMS implementation we wanted:

- `sunos4/net/if_sl.c`
- `sunos4/net/if_slvar.h`
- `common/net/slcompress.c`
- `common/net/slcompress.h`
- `common/net/slip.h`
- `tools/slattach/slattach.c`

The SunOS 4 driver is already a pushable STREAMS module named `slip`.  Its `slattach` opens the tty, pops existing STREAMS modules, sets raw termios, then `I_PUSH`es `slip` and asks the module for a unit number with `SLIOGUNIT`.

## Important architectural finding

The serial/STREAMS half of the old driver is close to what Solaris 8 wants.  The network half is not.

SunOS 4 `if_sl.c` attaches each line directly to the old BSD network stack with `struct ifnet`, `if_attach()`, `if_output`, `if_ioctl`, mbufs, `ipintrq`, and `schednetisr(NETISR_IP)`.

Solaris 8 network drivers instead present DLPI to the STREAMS IP stack.  Solaris 8's GLD framework is not a good fit because GLD only supports Ethernet, Token Ring, and FDDI media; SLIP therefore needs a small full DLPI provider rather than GLD.

Therefore the port should preserve the old serial/framing logic but replace the `ifnet`/mbuf/IP-queue side with Solaris 8 DLPI message handling.

## Code that can be reused almost directly

### RFC 1055 framing constants

- END = `0xc0`
- ESC = `0xdb`
- ESC_END = `0xdc`
- ESC_ESC = `0xdd`

### Receive state machine

The byte-by-byte logic in `slinput()` is reusable in structure:

- remember whether the previous byte was ESC
- translate ESC_END / ESC_ESC
- END closes the current packet
- ignore undersized/empty frames
- reset receive state after each complete/error frame

The storage must change from SunOS 4 mbufs to Solaris STREAMS `mblk_t` chains or a kernel buffer.

### Transmit framing

The framing loop in `sl_wsrv()` is reusable in structure:

- prepend END to flush garbage
- escape END and ESC bytes
- append END
- send the resulting `M_DATA` downstream to the tty

The input source must change from an mbuf chain to the `M_DATA` payload accompanying a DLPI transmit primitive.

### STREAMS tty attachment idea

The 1993 SunOS 4 utility proves that a period-correct Sun implementation used a pushed STREAMS module directly on the serial tty.  This is preferable to inventing a tty line discipline.

For the first Solaris 8 test we are keeping the existing `slmux` I_PLINK prototype because it isolates driver plumbing from DLPI.  Once that works, the final design can either retain the mux or move closer to the original I_PUSH arrangement if Solaris 8 IP/DLPI attachment makes that cleaner.

## Code that cannot be carried over unchanged

The following SunOS 4 pieces are obsolete on Solaris 8 and must be replaced:

- `struct ifnet`
- `struct ifqueue`
- `struct mbuf`
- `if_attach()`
- `if_down()` / `if_rtdelete()`
- `if_output` and `if_ioctl`
- `ipintrq`
- `schednetisr(NETISR_IP)`
- BSD socket-side interface ioctls used as the driver's internal API
- `splimp()`-style networking synchronization
- direct kernel configuration via `pseudo-device slN init slattach`

## Solaris 8 target architecture

```
Solaris IP
   |
   | DLPI v2 (M_PROTO/M_PCPROTO + M_DATA)
   v
SunSlip DLPI provider
   |
   | RFC 1055 frame/unframe
   v
STREAMS tty path
   |
   v
/dev/term/b  <---- null modem ---->  A/UX SLIP
```

The DLPI provider should be Style 1 initially, one PPA / one SLIP link, point-to-point, IPv4 only.

Minimum primitives likely needed for an IPv4 bring-up are:

- `DL_INFO_REQ` -> `DL_INFO_ACK`
- `DL_BIND_REQ` -> `DL_BIND_ACK`
- `DL_UNBIND_REQ` -> `DL_OK_ACK`
- `DL_UNITDATA_REQ` for outbound packets
- `DL_UNITDATA_IND` for inbound packets

We should add others only when Solaris `ifconfig`/IP demonstrates they are required.

## CSLIP / Van Jacobson compression

Do **not** port compression first.

`common/net/slcompress.c` is valuable donor code, but it is tightly written around BSD mbufs.  Plain RFC 1055 SLIP should be proven first.  Once packets pass both ways, either adapt `slcompress.c` to contiguous packet buffers/mblk chains or keep compression disabled permanently if A/UX interoperability does not require it.

## Immediate development order

1. Compile/load the current `slmux` prototype on Solaris 8/SPARC and prove `/dev/term/b` can be linked beneath it.
2. Add plain RFC 1055 framing/unframing while still using raw upper `M_DATA`, so framing can be tested independently of IP.
3. Replace the raw upper interface with the minimum DLPI provider.
4. Attach Solaris IPv4, create the `sl0`-like interface, and ping the A/UX peer.
5. Only after plain SLIP works, consider CSLIP compression and statistics.

## Historical compatibility goal

The wire format will be ordinary RFC 1055 SLIP.  No Solaris-specific protocol is introduced, so A/UX 3.1 should see the same serial framing it expects from any other SLIP peer.
