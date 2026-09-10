/* Minimal Solaris DLPI handshake test for /dev/sunslip0. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stropts.h>
#include <sys/dlpi.h>

static void die(const char *s) { perror(s); exit(1); }

static int recvctl(int fd, char *buf, int len)
{
    struct strbuf ctl;
    int flags = 0;
    ctl.buf = buf;
    ctl.maxlen = len;
    ctl.len = 0;
    if (getmsg(fd, &ctl, NULL, &flags) < 0) die("getmsg");
    return ctl.len;
}

int main(void)
{
    int fd, n;
    char buf[512];
    struct strbuf ctl;
    dl_info_req_t info;
    dl_bind_req_t bind;
    union DL_primitives *p;

    fd = open("/dev/sunslip0", O_RDWR);
    if (fd < 0) die("open /dev/sunslip0");

    memset(&info, 0, sizeof(info));
    info.dl_primitive = DL_INFO_REQ;
    ctl.buf = (char *)&info;
    ctl.len = ctl.maxlen = sizeof(info);
    if (putmsg(fd, &ctl, NULL, RS_HIPRI) < 0) die("DL_INFO_REQ putmsg");
    n = recvctl(fd, buf, sizeof(buf));
    if (n < (int)sizeof(t_uscalar_t)) {
        fprintf(stderr, "short DL_INFO reply\n");
        return 1;
    }

    p = (union DL_primitives *)buf;
    if (p->dl_primitive != DL_INFO_ACK) {
        fprintf(stderr, "expected DL_INFO_ACK, got 0x%lx\n",
            (unsigned long)p->dl_primitive);
        return 1;
    }

    printf("DL_INFO_ACK: max_sdu=%lu mac_type=%lu state=%lu style=%lu version=%lu\n",
        (unsigned long)p->info_ack.dl_max_sdu,
        (unsigned long)p->info_ack.dl_mac_type,
        (unsigned long)p->info_ack.dl_current_state,
        (unsigned long)p->info_ack.dl_provider_style,
        (unsigned long)p->info_ack.dl_version);

    memset(&bind, 0, sizeof(bind));
    bind.dl_primitive = DL_BIND_REQ;
    bind.dl_sap = 0;
    bind.dl_service_mode = DL_CLDLS;
    ctl.buf = (char *)&bind;
    ctl.len = ctl.maxlen = sizeof(bind);
    if (putmsg(fd, &ctl, NULL, 0) < 0) die("DL_BIND_REQ putmsg");

    n = recvctl(fd, buf, sizeof(buf));
    (void)n;
    p = (union DL_primitives *)buf;
    if (p->dl_primitive == DL_BIND_ACK) {
        printf("DL_BIND_ACK: sap=%lu -- basic DLPI handshake passed\n",
            (unsigned long)p->bind_ack.dl_sap);
    } else if (p->dl_primitive == DL_ERROR_ACK) {
        fprintf(stderr, "DL_ERROR_ACK: dl_errno=%lu unix_errno=%lu\n",
            (unsigned long)p->error_ack.dl_errno,
            (unsigned long)p->error_ack.dl_unix_errno);
        return 1;
    } else {
        fprintf(stderr, "unexpected bind reply 0x%lx\n",
            (unsigned long)p->dl_primitive);
        return 1;
    }

    close(fd);
    return 0;
}
