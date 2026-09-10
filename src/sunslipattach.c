/* Keep a serial tty open with the sunslip STREAMS module pushed on it. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <stropts.h>
#include <sys/ioctl.h>

static volatile sig_atomic_t done;
static void stop(int sig) { (void)sig; done = 1; }
static void die(const char *s) { perror(s); exit(1); }

static int
parse_speed(const char *s, speed_t *speedp)
{
    long baud;
    char *end;

    errno = 0;
    baud = strtol(s, &end, 10);
    if (errno != 0 || s == end || *end != '\0')
        return (-1);

    switch (baud) {
    case 50:    *speedp = B50; break;
    case 75:    *speedp = B75; break;
    case 110:   *speedp = B110; break;
    case 134:   *speedp = B134; break;
    case 150:   *speedp = B150; break;
    case 200:   *speedp = B200; break;
    case 300:   *speedp = B300; break;
    case 600:   *speedp = B600; break;
    case 1200:  *speedp = B1200; break;
    case 1800:  *speedp = B1800; break;
    case 2400:  *speedp = B2400; break;
    case 4800:  *speedp = B4800; break;
    case 9600:  *speedp = B9600; break;
    case 19200: *speedp = B19200; break;
    case 38400: *speedp = B38400; break;
#ifdef B57600
    case 57600: *speedp = B57600; break;
#endif
#ifdef B76800
    case 76800: *speedp = B76800; break;
#endif
#ifdef B115200
    case 115200: *speedp = B115200; break;
#endif
    default:
        return (-1);
    }
    return (0);
}

int main(int argc, char **argv)
{
    const char *dev = "/dev/term/b";
    const char *readyfile = NULL;
    const char *speedstr = "19200";
    FILE *readyfp;
    int fd;
    int flags;
    speed_t speed;
    struct termios t;

    if (argc > 1) dev = argv[1];
    if (argc > 2) readyfile = argv[2];
    if (argc > 3) speedstr = argv[3];

    if (parse_speed(speedstr, &speed) < 0) {
        fprintf(stderr, "Unsupported serial speed: %s\n", speedstr);
        fprintf(stderr, "Use a standard termios baud rate such as 300, 1200, 2400, 4800, 9600, 19200, or 38400.\n");
        return (2);
    }

    /*
     * Establish signal behavior before opening the tty.  This closes the
     * startup window in which an init-shell SIGHUP could terminate us.
     */
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    signal(SIGHUP, SIG_IGN);
    /*
     * A real serial port may block open(2) until carrier detect is asserted.
     * Open it nonblocking so CLOCAL can be established before waiting on the
     * stream.  Restore normal blocking operation after configuring the tty.
     */
    fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) die("open tty");
    if (tcgetattr(fd, &t) < 0) {
        /*
         * On pre-Solaris-11.4 /dev/pts slaves opened by a non-XPG4
         * program may not have terminal-emulation modules pushed
         * automatically.  ptem supplies the terminal ioctls and ldterm
         * supplies the line discipline.  Add them only for PTY test
         * endpoints, never for real /dev/term/* hardware.
         */
        if (strncmp(dev, "/dev/pts/", 9) == 0 && errno == EINVAL) {
            if (ioctl(fd, I_FIND, "ptem") == 0 &&
                ioctl(fd, I_PUSH, "ptem") < 0)
                die("I_PUSH ptem");
            if (ioctl(fd, I_FIND, "ldterm") == 0 &&
                ioctl(fd, I_PUSH, "ldterm") < 0)
                die("I_PUSH ldterm");
            if (tcgetattr(fd, &t) < 0)
                die("tcgetattr after PTY setup");
        } else {
            die("tcgetattr");
        }
    }

    t.c_iflag = 0;
    t.c_oflag = 0;
    t.c_lflag = 0;
    t.c_cflag = CS8 | CREAD | CLOCAL;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (cfsetispeed(&t, speed) < 0) die("cfsetispeed");
    if (cfsetospeed(&t, speed) < 0) die("cfsetospeed");
    if (tcsetattr(fd, TCSANOW, &t) < 0) die("tcsetattr");

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) die("fcntl F_GETFL");
    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0)
        die("fcntl F_SETFL");

    if (ioctl(fd, I_PUSH, "sunslip") < 0) die("I_PUSH sunslip");

    /*
     * Tell the service script that open, termios setup, and I_PUSH all
     * completed.  Merely observing a live process is not sufficient because
     * a serial open can block while waiting for modem-control state.
     */
    if (readyfile != NULL) {
        readyfp = fopen(readyfile, "w");
        if (readyfp == NULL) die("create ready file");
        if (fprintf(readyfp, "%ld\n", (long)getpid()) < 0) {
            (void)fclose(readyfp);
            die("write ready file");
        }
        if (fclose(readyfp) != 0) die("close ready file");
    }

    printf("SunSlip attached to %s at %s 8N1; pid=%ld\n",
        dev, speedstr, (long)getpid());
    printf("Leave this process running; interrupt it to detach.\n");
    fflush(stdout);

    while (!done) pause();

    (void)ioctl(fd, I_POP, 0);
    close(fd);
    return 0;
}
