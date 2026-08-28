#!/bin/sh
#
# install.sh - rebuild and reinstall the SunSlip prototype on Solaris 8.
#
# Run as root from the repository root:
#
#     ./install.sh
#
# This script:
#   - unplumbs sunslip0 if present
#   - stops sunslipattach if running
#   - removes the old driver/module links
#   - rebuilds prototype/
#   - installs the new module/config
#   - runs add_drv/devfsadm
#   - recreates /dev/sunslip0 manually
#
# It intentionally does NOT start sunslipattach or configure IP, because
# those are still active test steps while the driver is under development.
#

PROTO="./prototype"
BUILDLOG="/tmp/sunslip-build.log"

if [ "`id -u`" != "0" ]; then
    echo "ERROR: run this script as root."
    exit 1
fi

if [ ! -d "$PROTO" ]; then
    echo "ERROR: run install.sh from the SunSlip repository root."
    exit 1
fi

echo "Stopping old SunSlip..."

ifconfig sunslip0 unplumb >/dev/null 2>&1

for pid in `ps -ef | grep '[s]unslipattach' | awk '{print $2}'`
do
    kill "$pid" >/dev/null 2>&1
done

sleep 1

rem_drv sunslip >/dev/null 2>&1

rm -f /dev/sunslip0
rm -f /usr/kernel/drv/sparcv9/sunslip
rm -f /usr/kernel/drv/sunslip.conf
rm -f /tmp/sunslip

echo "Building..."
cd "$PROTO" || exit 1
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

if [ ! -f sunslip ]; then
    echo "ERROR: build produced no sunslip module."
    exit 1
fi

echo "Installing..."
cp sunslip /tmp/sunslip
cp sunslip.conf /usr/kernel/drv/sunslip.conf
mkdir -p /usr/kernel/drv/sparcv9
ln -s /tmp/sunslip /usr/kernel/drv/sparcv9/sunslip

add_drv sunslip
if [ $? -ne 0 ]; then
    echo "ERROR: add_drv failed."
    exit 1
fi

devfsadm -i sunslip

rm -f /dev/sunslip0
ln -s ../devices/pseudo/sunslip@0:sunslip0 /dev/sunslip0

echo "Installed."
modinfo | grep sunslip
ls -l /dev/sunslip0

echo ""
echo "Next:"
echo "  cd prototype"
echo "  ./sunslipattach /dev/term/b"
echo "Then, from another shell:"
echo "  ifconfig sunslip0 plumb"
echo "  ifconfig sunslip0 10.23.24.1 10.23.24.2 up"
