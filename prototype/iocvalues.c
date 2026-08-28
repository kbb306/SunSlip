/*
 * iocvalues.c - print Solaris networking ioctl values relevant to SunSlip.
 * Build and run on the target Solaris system so values come from its headers.
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/sockio.h>
#include <sys/stropts.h>

#define P(x) printf("%-20s 0x%08lx\n", #x, (unsigned long)(unsigned int)(x))

int
main(void)
{
#ifdef SIOCSLIFNAME
    P(SIOCSLIFNAME);
#endif
#ifdef SIOCGLIFNAME
    P(SIOCGLIFNAME);
#endif
#ifdef SIOCGLIFFLAGS
    P(SIOCGLIFFLAGS);
#endif
#ifdef SIOCSLIFFLAGS
    P(SIOCSLIFFLAGS);
#endif
#ifdef SIOCGLIFMTU
    P(SIOCGLIFMTU);
#endif
#ifdef SIOCSLIFMTU
    P(SIOCSLIFMTU);
#endif
#ifdef SIOCGLIFINDEX
    P(SIOCGLIFINDEX);
#endif
#ifdef SIOCSLIFINDEX
    P(SIOCSLIFINDEX);
#endif
#ifdef SIOCGLIFMUXID
    P(SIOCGLIFMUXID);
#endif
#ifdef SIOCSLIFMUXID
    P(SIOCSLIFMUXID);
#endif
#ifdef SIOCGLIFLNKINFO
    P(SIOCGLIFLNKINFO);
#endif
#ifdef SIOCSLIFLNKINFO
    P(SIOCSLIFLNKINFO);
#endif
#ifdef SIOCGLIFDSTADDR
    P(SIOCGLIFDSTADDR);
#endif
#ifdef SIOCSLIFDSTADDR
    P(SIOCSLIFDSTADDR);
#endif
#ifdef SIOCGLIFNETMASK
    P(SIOCGLIFNETMASK);
#endif
#ifdef SIOCGLIFMETRIC
    P(SIOCGLIFMETRIC);
#endif
#ifdef SIOCGLIFCONF
    P(SIOCGLIFCONF);
#endif
#ifdef SIOCGLIFNUM
    P(SIOCGLIFNUM);
#endif
#ifdef IF_UNITSEL
    P(IF_UNITSEL);
#endif
    printf("target               0x40506993\n");
    return (0);
}
