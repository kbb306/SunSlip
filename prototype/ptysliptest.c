/*
 * ptysliptest.c - end-to-end SunSlip test using a Solaris pseudo-terminal.
 *
 * Exercises:
 *   DLPI bind -> SunSlip RFC1055 encoder -> pushed tty module -> PTY master
 *   PTY master -> pushed tty module -> RFC1055 decoder -> DL_UNITDATA_IND
 *
 * No physical serial port or peer machine is required.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stropts.h>
#include <sys/dlpi.h>

static void
die(const char *s)
{
    perror(s);
    exit(1);
}

static int
wait_readable(int fd, int ms)
{
    struct pollfd p;

    p.fd = fd;
    /*
     * DL_BIND_ACK and other DLPI acknowledgements may arrive as
     * high-priority M_PCPROTO messages, which STREAMS poll reports
     * as POLLPRI rather than POLLIN.  Accept either here.
     */
    p.events = POLLIN | POLLPRI;
    p.revents = 0;
    return (poll(&p, 1, ms) > 0 &&
        (p.revents & (POLLIN | POLLPRI)));
}

static int
dl_bind_ipv4(int fd)
{
    dl_bind_req_t req;
    struct strbuf ctl;
    char buf[256];
    int flags = 0;
    union DL_primitives *p;

    memset(&req, 0, sizeof (req));
    req.dl_primitive = DL_BIND_REQ;
    req.dl_sap = 0x0800;
    req.dl_service_mode = DL_CLDLS;

    ctl.buf = (char *)&req;
    ctl.len = ctl.maxlen = sizeof (req);
    if (putmsg(fd, &ctl, NULL, 0) < 0)
        return (-1);

    if (!wait_readable(fd, 3000)) {
        errno = ETIME;
        return (-1);
    }

    ctl.buf = buf;
    ctl.maxlen = sizeof (buf);
    ctl.len = 0;
    if (getmsg(fd, &ctl, NULL, &flags) < 0)
        return (-1);

    p = (union DL_primitives *)buf;
    if (p->dl_primitive != DL_BIND_ACK) {
        errno = EPROTO;
        return (-1);
    }
    return (0);
}

static int
send_dlpi_packet(int fd, const unsigned char *data, int len)
{
    dl_unitdata_req_t req;
    struct strbuf ctl, dat;

    memset(&req, 0, sizeof (req));
    req.dl_primitive = DL_UNITDATA_REQ;
    req.dl_dest_addr_length = 0;
    req.dl_dest_addr_offset = 0;

    ctl.buf = (char *)&req;
    ctl.len = ctl.maxlen = sizeof (req);

    dat.buf = (char *)data;
    dat.len = dat.maxlen = len;

    return (putmsg(fd, &ctl, &dat, 0));
}

static int
recv_dlpi_packet(int fd, unsigned char *data, int maxlen)
{
    char cbuf[256];
    struct strbuf ctl, dat;
    int flags = 0;
    union DL_primitives *p;

    if (!wait_readable(fd, 3000)) {
        errno = ETIME;
        return (-1);
    }

    ctl.buf = cbuf;
    ctl.maxlen = sizeof (cbuf);
    ctl.len = 0;
    dat.buf = (char *)data;
    dat.maxlen = maxlen;
    dat.len = 0;

    if (getmsg(fd, &ctl, &dat, &flags) < 0)
        return (-1);

    if (ctl.len < (int)sizeof (t_uscalar_t)) {
        errno = EPROTO;
        return (-1);
    }

    p = (union DL_primitives *)cbuf;
    if (p->dl_primitive != DL_UNITDATA_IND) {
        errno = EPROTO;
        return (-1);
    }

    return (dat.len);
}

int
main(void)
{
    static const unsigned char plain[] = {
        0x45, 0xc0, 0xdb, 0x00, 0xff
    };
    static const unsigned char wire[] = {
        0x45, 0xdb, 0xdc, 0xdb, 0xdd, 0x00, 0xff, 0xc0
    };

    int mfd = -1, dfd = -1;
    char *slave;
    pid_t child = -1;
    unsigned char buf[64];
    int n, got, status;
    int rc = 1;

    mfd = open("/dev/ptmx", O_RDWR | O_NOCTTY);
    if (mfd < 0)
        die("open /dev/ptmx");

    if (grantpt(mfd) < 0)
        die("grantpt");
    if (unlockpt(mfd) < 0)
        die("unlockpt");

    slave = ptsname(mfd);
    if (slave == NULL)
        die("ptsname");

    printf("PTY slave: %s\n", slave);
    fflush(stdout);

    child = fork();
    if (child < 0)
        die("fork");

    if (child == 0) {
        execl("./sunslipattach", "sunslipattach", slave, (char *)0);
        perror("exec sunslipattach");
        _exit(127);
    }

    sleep(1);
    if (waitpid(child, &status, WNOHANG) == child) {
        fprintf(stderr, "PTY test: sunslipattach exited early\n");
        child = -1;
        goto out;
    }

    dfd = open("/dev/sunslip0", O_RDWR);
    if (dfd < 0) {
        perror("open /dev/sunslip0");
        goto out;
    }

    if (dl_bind_ipv4(dfd) < 0) {
        perror("DL_BIND_REQ");
        goto out;
    }

    if (send_dlpi_packet(dfd, plain, sizeof (plain)) < 0) {
        perror("DL_UNITDATA_REQ");
        goto out;
    }

    got = 0;
    while (got < (int)sizeof (wire)) {
        if (!wait_readable(mfd, 3000)) {
            fprintf(stderr, "PTY test: timeout waiting for encoded bytes\n");
            goto out;
        }
        n = read(mfd, buf + got, sizeof (buf) - got);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("read PTY master");
            goto out;
        }
        got += n;
    }

    if (got != (int)sizeof (wire) ||
        memcmp(buf, wire, sizeof (wire)) != 0) {
        fprintf(stderr, "PTY test: outbound RFC1055 bytes mismatch\n");
        goto out;
    }

    n = write(mfd, wire, sizeof (wire));
    if (n != (int)sizeof (wire)) {
        if (n < 0)
            perror("write PTY master");
        else
            fprintf(stderr, "PTY test: short write to PTY master\n");
        goto out;
    }

    memset(buf, 0, sizeof (buf));
    n = recv_dlpi_packet(dfd, buf, sizeof (buf));
    if (n < 0) {
        perror("DL_UNITDATA_IND");
        goto out;
    }

    if (n != (int)sizeof (plain) ||
        memcmp(buf, plain, sizeof (plain)) != 0) {
        fprintf(stderr, "PTY test: inbound decoded packet mismatch\n");
        goto out;
    }

    printf("SunSlip PTY end-to-end test PASS\n");
    rc = 0;

out:
    if (dfd >= 0)
        close(dfd);

    if (child > 0) {
        kill(child, SIGTERM);
        (void)waitpid(child, &status, 0);
    }

    if (mfd >= 0)
        close(mfd);

    return (rc);
}
