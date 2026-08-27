/* Keep /dev/term/b open with the sunslip STREAMS module pushed on it. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <stropts.h>
#include <sys/ioctl.h>

static volatile sig_atomic_t done;
static void stop(int sig) { (void)sig; done = 1; }
static void die(const char *s) { perror(s); exit(1); }

int main(int argc, char **argv)
{
    const char *dev = "/dev/term/b";
    int fd;
    struct termios t;

    if (argc > 1) dev = argv[1];
    fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) die("open tty");
    if (tcgetattr(fd, &t) < 0) die("tcgetattr");

    t.c_iflag = 0;
    t.c_oflag = 0;
    t.c_lflag = 0;
    t.c_cflag = CS8 | CREAD | CLOCAL;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (cfsetispeed(&t, B19200) < 0) die("cfsetispeed");
    if (cfsetospeed(&t, B19200) < 0) die("cfsetospeed");
    if (tcsetattr(fd, TCSANOW, &t) < 0) die("tcsetattr");

    if (ioctl(fd, I_PUSH, "sunslip") < 0) die("I_PUSH sunslip");

    printf("SunSlip attached to %s at 19200 8N1; pid=%ld\n",
        dev, (long)getpid());
    printf("Leave this process running; interrupt it to detach.\n");
    fflush(stdout);

    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    signal(SIGHUP, stop);
    while (!done) pause();

    (void)ioctl(fd, I_POP, 0);
    close(fd);
    return 0;
}
