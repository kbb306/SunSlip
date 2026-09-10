# Persistent Solaris 8 installation

This branch adds a persistent installation layout for real SunSlip use.

## Installed locations

- 64-bit driver: `/usr/kernel/drv/sparcv9/sunslip`
- driver config: `/usr/kernel/drv/sunslip.conf`
- attach helper: `/usr/local/sbin/sunslipattach`
- service script: `/etc/init.d/sunslip`
- service defaults: `/etc/default/sunslip`
- optional compatibility override: `/etc/sunslip.default`
- start link: `/etc/rc3.d/S99sunslip`
- stop links: `/etc/rc0.d/K20sunslip`, `/etc/rc1.d/K20sunslip`,
  `/etc/rc2.d/K20sunslip`

The installer still creates `/dev/sunslip0` explicitly because Solaris 8
`devfsadm` does not currently create that friendly link for the SunSlip
driver.

## Install

Run as root from the repository root:

    ./install.sh

If the old prototype installation is detected (for example the kernel module
is still a symlink to `/tmp/sunslip`), the installer asks before removing it.

The installer builds the code, installs the driver into the real SPARC V9
driver directory, runs the RFC1055 codec self-test and PTY end-to-end
diagnostic, and installs the Solaris rc service.

The service is deliberately not started automatically by the installer.
Review:

    cat /etc/default/sunslip

Default configuration:

    TTY=/dev/term/b
    SPEED=19200
    LOCAL_ADDR=10.23.24.1
    REMOTE_ADDR=10.23.24.2

`SPEED` is passed to `sunslipattach` and controls both input and output baud.
The attach helper validates the requested value and rejects unsupported
termios rates instead of silently falling back to another speed.

For convenience, `/etc/sunslip.default` is also read if present.  Values in
that file override `/etc/default/sunslip`, so an installation using the
alternate filename can set, for example:

    SPEED=9600

Then start:

    /etc/init.d/sunslip start

Other commands:

    /etc/init.d/sunslip status
    /etc/init.d/sunslip stop
    /etc/init.d/sunslip restart

The service starts `sunslipattach`, plumbs `sunslip0`, assigns the
point-to-point addresses, and brings the interface up.

## Uninstall

Run:

    ./uninstall.sh

The uninstaller stops the service, removes the driver and rc links, and leaves
`/etc/default/sunslip` in place so local addressing and serial choices are
preserved.
