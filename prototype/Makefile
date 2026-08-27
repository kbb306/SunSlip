GCC=/opt/sfw/bin/gcc
LD=/usr/ccs/bin/ld

# Solaris 64-bit SPARC kernel flags for GCC.  -mno-app-regs is important
# on SPARC kernel code so GCC does not use application-reserved globals.
KFLAGS=-D_KERNEL -D__EXTENSIONS__ -m64 -mcpu=v9 -mcmodel=medlow \
       -mno-app-regs -fno-pic -mno-fpu -ffreestanding -nodefaultlibs -O2

all: sunslip sunslipattach dltest

sunslip: sunslip.o
	$(LD) -r -o sunslip sunslip.o

sunslip.o: sunslip.c
	$(GCC) $(KFLAGS) -c sunslip.c

sunslipattach: sunslipattach.c
	$(GCC) -O2 -o sunslipattach sunslipattach.c

dltest: dltest.c
	$(GCC) -O2 -o dltest dltest.c

clean:
	rm -f sunslip.o sunslip sunslipattach dltest
