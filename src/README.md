# SunSlip source

This directory contains the Solaris 8/SPARC SunSlip driver, attach helper, and
diagnostic utilities.

SunSlip provides a plain RFC 1055 SLIP interface using a Solaris DLPI Style-1
pseudo-driver on the network side and a pushable STREAMS module on the serial
side.

## Components

- `sunslip.c` — 64-bit SPARC kernel driver and STREAMS tty module
- `sunslipattach.c` — attaches the tty-side module to a serial device
- `dltest.c` — basic DLPI INFO/BIND diagnostic
- `slipselftest.c` — invokes the in-driver RFC 1055 codec self-test
- `ptysliptest.c` — end-to-end PTY diagnostic for the DLPI/STREAMS path
- `iocvalues.c` — prints Solaris networking ioctl values from local headers
- `sunslip.conf` — Solaris driver configuration
- `Makefile` — Sun Studio / Solaris 8 build rules

## Build

From the repository root:

    cd src
    /usr/ccs/bin/make

The kernel module is compiled for the 64-bit SPARC V9 kernel with Sun Studio
`cc` and linked with `/usr/ccs/bin/ld`.

For a persistent installation, use the repository-level installer instead of
copying these files manually:

    cd ..
    ./install.sh

See `PERSISTENT-INSTALL.md` for the installed layout and service commands.
