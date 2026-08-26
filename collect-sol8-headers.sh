#!/bin/sh
# Collect only the Solaris 8 headers and build metadata needed for SunSlip porting.
# Run on the Sun Blade as a normal user; output is written in the current dir.

set -e
OUT=sunslip-sol8-headers
rm -rf "$OUT" "$OUT.tar"
mkdir "$OUT"

FILES='\
/usr/include/sys/dlpi.h \
/usr/include/sys/stream.h \
/usr/include/sys/stropts.h \
/usr/include/sys/conf.h \
/usr/include/sys/devops.h \
/usr/include/sys/modctl.h \
/usr/include/sys/ddi.h \
/usr/include/sys/sunddi.h \
/usr/include/sys/socket.h \
/usr/include/sys/sockio.h \
/usr/include/net/if.h \
/usr/include/netinet/in.h'

for f in $FILES
do
    if test -f "$f"; then
        d=`dirname "$OUT$f"`
        mkdir -p "$d"
        cp "$f" "$OUT$f"
    else
        echo "MISSING: $f"
    fi
done

{
    echo '=== /etc/release ==='
    cat /etc/release 2>/dev/null || true
    echo '=== uname -a ==='
    uname -a
    echo '=== isainfo -v ==='
    isainfo -v 2>/dev/null || true
    echo '=== compiler ==='
    which cc 2>/dev/null || true
    cc -V 2>&1 || true
    echo '=== relevant packages ==='
    pkginfo 2>/dev/null | grep SUNWhea || true
    pkginfo 2>/dev/null | grep SUNWarc || true
    pkginfo 2>/dev/null | grep SUNWtoo || true
    pkginfo 2>/dev/null | grep SUNWsprot || true
} > "$OUT/system-info.txt"

tar cf "$OUT.tar" "$OUT"
echo "Created $OUT.tar"
