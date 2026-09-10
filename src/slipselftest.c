/*
 * slipselftest.c - invoke the SunSlip in-driver RFC1055 codec self-test.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

#define SUNSLIP_IOC_SELFTEST 0x53534c54U

int
main(void)
{
    int fd;

    fd = open("/dev/sunslip0", O_RDWR);
    if (fd < 0) {
        perror("open /dev/sunslip0");
        return (1);
    }

    if (ioctl(fd, SUNSLIP_IOC_SELFTEST, 0) < 0) {
        fprintf(stderr, "SunSlip RFC1055 self-test FAILED: %s\n",
            strerror(errno));
        close(fd);
        return (1);
    }

    close(fd);
    printf("SunSlip RFC1055 self-test PASS\n");
    return (0);
}
