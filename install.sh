#!/bin/sh
#
# install.sh - install SunSlip as a persistent Solaris 8 driver/service.
#
# Run as root from the repository root:
#
#     ./install.sh
#

SRC="./src"
PKGDIR="./packaging/sunos8"
BUILDLOG="/tmp/sunslip-build.log"
DRV64="/usr/kernel/drv/sparcv9/sunslip"
DRVCONF="/usr/kernel/drv/sunslip.conf"
ATTACH="/usr/local/sbin/sunslipattach"
INIT="/etc/init.d/sunslip"
DEFAULTS="/etc/default/sunslip"

id | grep '^uid=0(' >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: run this script as root."
    exit 1
fi

if [ ! -d "$SRC" ] || [ ! -f "$PKGDIR/sunslip.init" ]; then
    echo "ERROR: run install.sh from the SunSlip repository root."
    exit 1
fi

is_prototype=0
if [ -L "$DRV64" ]; then
    ls -l "$DRV64" 2>/dev/null | grep '/tmp/sunslip' >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        is_prototype=1
    fi
fi
if [ -f /tmp/sunslip ]; then
    is_prototype=1
fi

if [ "$is_prototype" -eq 1 ]; then
    echo "Prototype SunSlip installation detected."
    echo "It must be removed before the persistent driver is installed."
    echo "Remove prototype installation now? [y/N]"
    read answer
    case "$answer" in
    y|Y|yes|YES)
        ;;
    *)
        echo "Installation cancelled."
        exit 1
        ;;
    esac
fi

echo "Stopping any existing SunSlip instance..."
if [ -x "$INIT" ]; then
    "$INIT" stop >/dev/null 2>&1
else
    ifconfig sunslip0 unplumb >/dev/null 2>&1
    for pid in `ps -ef | grep '[s]unslipattach' | awk '{print $2}'`
    do
        kill "$pid" >/dev/null 2>&1
    done
fi
sleep 1

rem_drv sunslip >/dev/null 2>&1

rm -f /dev/sunslip0
rm -f /usr/kernel/drv/sparcv9/sunslip
rm -f /usr/kernel/drv/sunslip.conf
rm -f /tmp/sunslip

echo "Building..."
cd "$SRC" || exit 1
rm -f "$BUILDLOG"

/usr/ccs/bin/make clean >"$BUILDLOG" 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: make clean failed:"
    tail -20 "$BUILDLOG"
    exit 1
fi

/usr/ccs/bin/make >>"$BUILDLOG" 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: build failed:"
    tail -20 "$BUILDLOG"
    exit 1
fi

if [ ! -f sunslip ] || [ ! -f sunslipattach ]; then
    echo "ERROR: build did not produce required files."
    exit 1
fi

echo "Running RFC1055 codec self-test against temporary load..."
mkdir -p /usr/kernel/drv/sparcv9
cp sunslip "$DRV64"
cp sunslip.conf "$DRVCONF"
chmod 755 "$DRV64"
chmod 644 "$DRVCONF"

add_drv sunslip
if [ $? -ne 0 ]; then
    echo "ERROR: add_drv failed."
    exit 1
fi

devfsadm -i sunslip

rm -f /dev/sunslip0
ln -s ../devices/pseudo/sunslip@0:sunslip0 /dev/sunslip0

./slipselftest
if [ $? -ne 0 ]; then
    echo "ERROR: RFC1055 self-test failed."
    exit 1
fi

echo "Running PTY end-to-end diagnostic..."
./ptysliptest
if [ $? -ne 0 ]; then
    echo "WARNING: PTY end-to-end diagnostic failed."
    echo "Driver remains installed for inspection."
else
    echo "PTY end-to-end diagnostic passed."
fi

echo "Installing service..."
mkdir -p /usr/local/sbin
cp sunslipattach "$ATTACH"
chmod 755 "$ATTACH"

cd .. || exit 1
cp "$PKGDIR/sunslip.init" "$INIT"
chmod 755 "$INIT"

if [ ! -f "$DEFAULTS" ]; then
    cp "$PKGDIR/sunslip.default" "$DEFAULTS"
    chmod 644 "$DEFAULTS"
else
    echo "Keeping existing $DEFAULTS"
fi

rm -f /etc/rc3.d/S99sunslip
ln -s ../init.d/sunslip /etc/rc3.d/S99sunslip

rm -f /etc/rc0.d/K20sunslip
ln -s ../init.d/sunslip /etc/rc0.d/K20sunslip

rm -f /etc/rc1.d/K20sunslip
ln -s ../init.d/sunslip /etc/rc1.d/K20sunslip

rm -f /etc/rc2.d/K20sunslip
ln -s ../init.d/sunslip /etc/rc2.d/K20sunslip

echo ""
echo "Persistent SunSlip installation complete."
echo "Driver:  $DRV64"
echo "Config:  $DRVCONF"
echo "Attach:  $ATTACH"
echo "Service: $INIT"
echo "Network: /etc/default/sunslip"
echo ""
echo "The service has NOT been started automatically by this installer."
echo "Review /etc/default/sunslip, then run:"
echo "  /etc/init.d/sunslip start"
echo ""
echo "Status:"
echo "  /etc/init.d/sunslip status"
echo "Stop:"
echo "  /etc/init.d/sunslip stop"
