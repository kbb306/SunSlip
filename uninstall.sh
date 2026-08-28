#!/bin/sh
#
# uninstall.sh - remove the persistent SunSlip Solaris 8 installation.
#

INIT=/etc/init.d/sunslip

id | grep '^uid=0(' >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: run this script as root."
    exit 1
fi

echo "Remove persistent SunSlip installation? [y/N]"
read answer
case "$answer" in
y|Y|yes|YES)
    ;;
*)
    echo "Cancelled."
    exit 0
    ;;
esac

if [ -x "$INIT" ]; then
    "$INIT" stop >/dev/null 2>&1
else
    ifconfig sunslip0 unplumb >/dev/null 2>&1
fi

sleep 1
rem_drv sunslip >/dev/null 2>&1

rm -f /etc/rc3.d/S99sunslip
rm -f /etc/rc0.d/K20sunslip
rm -f /etc/rc1.d/K20sunslip
rm -f /etc/rc2.d/K20sunslip
rm -f /etc/init.d/sunslip
rm -f /usr/local/sbin/sunslipattach
rm -f /dev/sunslip0
rm -f /usr/kernel/drv/sparcv9/sunslip
rm -f /usr/kernel/drv/sunslip.conf

echo "SunSlip removed."
echo "Kept /etc/default/sunslip so your network settings are preserved."
echo "Remove it manually if no longer wanted."
