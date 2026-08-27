# SunSlip 0.1 -- first Solaris 8 prototype

This is the first real Solaris 8/SPARC port prototype, based primarily on the
AMIX SVR4 STREAMS/DLPI `slip.c` architecture and RFC 1055 framing. It is
plain SLIP only: no CSLIP/VJ compression yet.

## What it implements

- one DLPI Style-1 pseudo interface: `/dev/sunslip0`
- `DL_INFO_REQ`, `DL_BIND_REQ`, `DL_UNBIND_REQ`, `DL_UNITDATA_REQ`
- `DL_UNITDATA_IND` for received packets
- `DL_OTHER`, `DL_CLDLS`, DLPI version 2
- a pushable STREAMS tty module named `sunslip`
- RFC 1055 END/ESC framing in both directions
- one serial interface only, intended for `/dev/term/b`
- `sunslipattach` sets ttyb to 19200 8N1 raw + CLOCAL and keeps the pushed
  module alive
- `dltest` tests the DLPI INFO/BIND handshake before Solaris IP is involved

## Important limitation

This has been written against headers collected from the actual Solaris 8
Blade, but has not yet been compiled on Solaris. Expect the first native build
to expose ABI/compiler details that need adjustment. Do not add it to boot
configuration yet.

## Build

    cd prototype
    make

The Makefile targets the Blade's 64-bit kernel with `/opt/sfw/bin/gcc` and
`/usr/ccs/bin/ld`.

If GCC 2.95.3 rejects `-mcmodel=medlow`, remove only that option and retry;
report the exact compiler output before changing anything else.

Verify the result is 64-bit SPARC before installing:

    file sunslip
    elfdump -e sunslip | more

## Temporary test install (root)

First make sure ttyb is not owned by ttymon/getty.

    cp sunslip /tmp/sunslip
    cp sunslip.conf /usr/kernel/drv/sunslip.conf
    mkdir -p /usr/kernel/drv/sparcv9
    ln -s /tmp/sunslip /usr/kernel/drv/sparcv9/sunslip
    add_drv sunslip
    devfsadm -i sunslip

Check:

    modinfo | grep sunslip
    ls -l /dev/sunslip0 /devices/pseudo/sunslip* 2>/dev/null
    tail /var/adm/messages

If the /devices node exists but /dev/sunslip0 does not, use the exact
/devices path shown by ls to make a temporary symlink. Do not guess the path.

## DLPI handshake test

Before touching ttyb, test just the network-facing driver:

    ./dltest

Expected shape:

    DL_INFO_ACK: max_sdu=1006 mac_type=9 state=0 style=1 version=2
    DL_BIND_ACK: sap=0 -- basic DLPI handshake passed

## Serial module test

With ttyb free:

    ./sunslipattach /dev/term/b

Leave that process running. In another shell:

    tail /var/adm/messages

You should see:

    sunslip0: tty module pushed

At this stage the serial side implements real RFC 1055 framing, but we are not
yet claiming that `ifconfig sunslip0 plumb` will work. The immediate gates are:

1. the 64-bit kernel module compiles and links
2. `add_drv` attaches it
3. `/dev/sunslip0` opens and passes `dltest`
4. `I_PUSH "sunslip"` succeeds on `/dev/term/b`

Only after those gates pass should we test Solaris IP plumbing and A/UX ping.

## Uninstall after testing

Stop `sunslipattach` first and close all `/dev/sunslip0` users.

    rem_drv sunslip
    rm -f /usr/kernel/drv/sparcv9/sunslip
    rm -f /usr/kernel/drv/sunslip.conf

The actual test binary remains in `/tmp/sunslip` until removed manually.
