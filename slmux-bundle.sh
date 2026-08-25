#!/bin/sh
# Solaris 8 SPARC STREAMS multiplexor test driver bundle.
# Creates source, Makefile, config, and a small I_PLINK test utility.
# Nothing is installed automatically.

set -e

DIR=slmux-test
mkdir -p "$DIR"
cd "$DIR"

cat > slmux.c <<'EOF'
/*
 * slmux.c - Solaris 8 STREAMS multiplexor proof-of-concept.
 *
 * PURPOSE ONLY:
 *   - load as a pseudo STREAMS driver
 *   - create a character minor node named slmux
 *   - accept one I_LINK/I_PLINK lower STREAM (intended: /dev/term/b)
 *   - pass raw M_DATA from an open upper stream down to the linked tty
 *   - pass raw M_DATA arriving from the linked tty up to an open upper stream
 *
 * This is NOT yet a DLPI provider and NOT yet a SLIP implementation.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/stat.h>
#include <sys/cmn_err.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/sysmacros.h>

static dev_info_t *slmux_dip;
static queue_t *slmux_upper_rq;
static queue_t *slmux_lower_wq;
static int slmux_muxid = -1;

static int slmux_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int slmux_attach(dev_info_t *, ddi_attach_cmd_t);
static int slmux_detach(dev_info_t *, ddi_detach_cmd_t);
static int slmux_open(queue_t *, dev_t *, int, int, cred_t *);
static int slmux_close(queue_t *, int, cred_t *);
static int slmux_uwput(queue_t *, mblk_t *);
static int slmux_lrput(queue_t *, mblk_t *);
static int slmux_lwsrv(queue_t *);

static struct module_info slmux_minfo = {
    0x534c, "slmux", 0, INFPSZ, 65536, 1024
};

static struct qinit slmux_urinit = {
    NULL, NULL, slmux_open, slmux_close, NULL, &slmux_minfo, NULL
};
static struct qinit slmux_uwinit = {
    slmux_uwput, NULL, NULL, NULL, NULL, &slmux_minfo, NULL
};
static struct qinit slmux_lrinit = {
    slmux_lrput, NULL, NULL, NULL, NULL, &slmux_minfo, NULL
};
static struct qinit slmux_lwinit = {
    NULL, slmux_lwsrv, NULL, NULL, NULL, &slmux_minfo, NULL
};
static struct streamtab slmux_strtab = {
    &slmux_urinit, &slmux_uwinit, &slmux_lrinit, &slmux_lwinit
};

static struct cb_ops slmux_cb_ops = {
    nodev, nodev, nodev, nodev, nodev, nodev, nodev, nodev,
    nodev, nodev, nodev, nochpoll, ddi_prop_op, &slmux_strtab,
    D_MP | D_NEW
};

static struct dev_ops slmux_dev_ops = {
    DEVO_REV, 0, slmux_getinfo, nulldev, nulldev, slmux_attach,
    slmux_detach, nodev, &slmux_cb_ops, NULL, NULL
};

static struct modldrv slmux_modldrv = {
    &mod_driverops, "Solaris 8 SLIP mux test 0.1", &slmux_dev_ops
};
static struct modlinkage slmux_modlinkage = {
    MODREV_1, &slmux_modldrv, NULL
};

int _init(void)
{
    int e = mod_install(&slmux_modlinkage);
    if (e == 0) cmn_err(CE_NOTE, "slmux: module installed");
    return e;
}

int _fini(void)
{
    int e;
    if (slmux_lower_wq != NULL) return EBUSY;
    e = mod_remove(&slmux_modlinkage);
    if (e == 0) cmn_err(CE_NOTE, "slmux: module removed");
    return e;
}

int _info(struct modinfo *mip)
{
    return mod_info(&slmux_modlinkage, mip);
}

static int slmux_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd,
    void *arg, void **result)
{
    dev_t dev = (dev_t)arg;
    int instance = getminor(dev);
    (void)dip;
    switch (cmd) {
    case DDI_INFO_DEVT2DEVINFO:
        if (slmux_dip == NULL) return DDI_FAILURE;
        *result = slmux_dip;
        return DDI_SUCCESS;
    case DDI_INFO_DEVT2INSTANCE:
        *result = (void *)instance;
        return DDI_SUCCESS;
    default:
        return DDI_FAILURE;
    }
}

static int slmux_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
    if (cmd != DDI_ATTACH) return DDI_FAILURE;
    if (ddi_get_instance(dip) != 0) return DDI_FAILURE;
    if (ddi_create_minor_node(dip, "slmux", S_IFCHR, 0,
        DDI_PSEUDO, 0) != DDI_SUCCESS) {
        ddi_remove_minor_node(dip, NULL);
        return DDI_FAILURE;
    }
    slmux_dip = dip;
    ddi_report_dev(dip);
    cmn_err(CE_NOTE, "slmux0: attached");
    return DDI_SUCCESS;
}

static int slmux_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
    if (cmd != DDI_DETACH) return DDI_FAILURE;
    if (slmux_lower_wq != NULL || slmux_upper_rq != NULL)
        return DDI_FAILURE;
    ddi_remove_minor_node(dip, NULL);
    slmux_dip = NULL;
    return DDI_SUCCESS;
}

static int slmux_open(queue_t *rq, dev_t *devp, int oflag,
    int sflag, cred_t *crp)
{
    (void)devp; (void)oflag; (void)sflag; (void)crp;
    if (slmux_upper_rq != NULL && slmux_upper_rq != rq) return EBUSY;
    slmux_upper_rq = rq;
    rq->q_ptr = (caddr_t)1;
    WR(rq)->q_ptr = (caddr_t)1;
    qprocson(rq);
    cmn_err(CE_NOTE, "slmux: upper stream opened");
    return 0;
}

static int slmux_close(queue_t *rq, int flag, cred_t *crp)
{
    (void)flag; (void)crp;
    qprocsoff(rq);
    if (slmux_upper_rq == rq) slmux_upper_rq = NULL;
    rq->q_ptr = NULL;
    WR(rq)->q_ptr = NULL;
    cmn_err(CE_NOTE, "slmux: upper stream closed");
    return 0;
}

static void slmux_iocack(queue_t *q, mblk_t *mp, int error)
{
    struct iocblk *iocp = (struct iocblk *)mp->b_rptr;
    if (error == 0) {
        mp->b_datap->db_type = M_IOCACK;
        iocp->ioc_error = 0;
        iocp->ioc_rval = 0;
    } else {
        mp->b_datap->db_type = M_IOCNAK;
        iocp->ioc_error = error;
        iocp->ioc_rval = -1;
    }
    iocp->ioc_count = 0;
    qreply(q, mp);
}

static int slmux_uwput(queue_t *q, mblk_t *mp)
{
    struct iocblk *iocp;
    struct linkblk *lb;
    switch (mp->b_datap->db_type) {
    case M_IOCTL:
        iocp = (struct iocblk *)mp->b_rptr;
        switch (iocp->ioc_cmd) {
        case I_LINK:
        case I_PLINK:
            if (slmux_lower_wq != NULL) {
                slmux_iocack(q, mp, EBUSY); return 0;
            }
            if (mp->b_cont == NULL) {
                slmux_iocack(q, mp, EINVAL); return 0;
            }
            lb = (struct linkblk *)mp->b_cont->b_rptr;
            slmux_lower_wq = lb->l_qbot;
            slmux_muxid = lb->l_index;
            cmn_err(CE_NOTE, "slmux: lower stream linked, muxid=%d",
                slmux_muxid);
            slmux_iocack(q, mp, 0);
            return 0;
        case I_UNLINK:
        case I_PUNLINK:
            cmn_err(CE_NOTE, "slmux: lower stream unlinked, muxid=%d",
                slmux_muxid);
            slmux_lower_wq = NULL;
            slmux_muxid = -1;
            slmux_iocack(q, mp, 0);
            return 0;
        default:
            slmux_iocack(q, mp, EINVAL);
            return 0;
        }
    case M_DATA:
        if (slmux_lower_wq != NULL) putnext(slmux_lower_wq, mp);
        else freemsg(mp);
        return 0;
    case M_FLUSH:
        if (*mp->b_rptr & FLUSHW) flushq(q, FLUSHDATA);
        if (*mp->b_rptr & FLUSHR) {
            *mp->b_rptr &= ~FLUSHW;
            qreply(q, mp);
        } else freemsg(mp);
        return 0;
    default:
        freemsg(mp);
        return 0;
    }
}

static int slmux_lrput(queue_t *q, mblk_t *mp)
{
    (void)q;
    if (mp->b_datap->db_type == M_DATA) {
        if (slmux_upper_rq != NULL) putnext(slmux_upper_rq, mp);
        else freemsg(mp);
        return 0;
    }
    freemsg(mp);
    return 0;
}

static int slmux_lwsrv(queue_t *q)
{
    (void)q;
    return 0;
}
EOF

cat > slmux.conf <<'EOF'
name="slmux" parent="pseudo";
EOF

cat > linktest.c <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <stropts.h>

static void die(const char *s) { perror(s); exit(1); }

int main(int argc, char **argv)
{
    const char *muxdev = "/dev/slmux";
    const char *ttydev = "/dev/term/b";
    int mfd, tfd, id;
    struct termios t;
    if (argc > 1) ttydev = argv[1];
    mfd = open(muxdev, O_RDWR);
    if (mfd < 0) die("open /dev/slmux");
    tfd = open(ttydev, O_RDWR | O_NOCTTY);
    if (tfd < 0) die("open tty");
    if (tcgetattr(tfd, &t) < 0) die("tcgetattr");
    t.c_iflag = 0;
    t.c_oflag = 0;
    t.c_lflag = 0;
    t.c_cflag = CS8 | CREAD | CLOCAL;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (cfsetispeed(&t, B19200) < 0) die("cfsetispeed");
    if (cfsetospeed(&t, B19200) < 0) die("cfsetospeed");
    if (tcsetattr(tfd, TCSANOW, &t) < 0) die("tcsetattr");
    id = ioctl(mfd, I_PLINK, tfd);
    if (id < 0) die("I_PLINK");
    printf("linked %s below %s, muxid=%d\n", ttydev, muxdev, id);
    printf("persistent link remains after this program exits\n");
    close(tfd);
    close(mfd);
    return 0;
}
EOF

cat > unlinktest.c <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stropts.h>
int main(int argc, char **argv)
{
    int fd, id;
    if (argc != 2) {
        fprintf(stderr, "usage: %s muxid\n", argv[0]);
        return 2;
    }
    id = atoi(argv[1]);
    fd = open("/dev/slmux", O_RDWR);
    if (fd < 0) { perror("open /dev/slmux"); return 1; }
    if (ioctl(fd, I_PUNLINK, id) < 0) {
        perror("I_PUNLINK"); close(fd); return 1;
    }
    close(fd);
    printf("unlinked muxid=%d\n", id);
    return 0;
}
EOF

cat > Makefile <<'EOF'
CC=cc
CFLAGS=-D_KERNEL -D__EXTENSIONS__ -xarch=v9 -c
LDFLAGS=-r
all: slmux linktest unlinktest
slmux: slmux.o
	ld $(LDFLAGS) -o slmux slmux.o
slmux.o: slmux.c
	$(CC) $(CFLAGS) slmux.c
linktest: linktest.c
	$(CC) -o linktest linktest.c
unlinktest: unlinktest.c
	$(CC) -o unlinktest unlinktest.c
clean:
	rm -f slmux.o slmux linktest unlinktest
EOF

cat > README.txt <<'EOF'
Solaris 8 STREAMS multiplexor proof-of-concept
==============================================

THIS IS NOT YET SLIP OR DLPI. It is only the first plumbing test.

Build:
    make

If cc is not installed, stop and report the error.

Install as root:
    cp slmux /usr/kernel/drv/sparcv9/slmux
    cp slmux.conf /usr/kernel/drv/slmux.conf
    add_drv slmux
    devfsadm -i slmux

Check:
    modinfo | grep slmux
    ls -l /devices/pseudo/slmux* /dev/slmux 2>/dev/null

If /dev/slmux was not created but the /devices node exists:
    ln -s ../devices/pseudo/slmux@0:slmux /dev/slmux

Link ttyb at 19200 8N1 raw:
    ./linktest

SAVE the muxid it prints.

Kernel messages:
    tail /var/adm/messages

Unlink later:
    ./unlinktest MUXID

Remove after unlinking/closing:
    rem_drv slmux
    rm -f /usr/kernel/drv/sparcv9/slmux /usr/kernel/drv/slmux.conf /dev/slmux

Important:
- Do not run linktest until ttyb is free.
- ttya is untouched.
- This driver currently drops tty-side non-M_DATA STREAMS messages.
EOF

printf '\nCreated %s/ with:\n' "$DIR"
ls -l
printf '\nRead %s/README.txt before installing anything.\n' "$DIR"
