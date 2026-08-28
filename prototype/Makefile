CC=/opt/SUNWspro/bin/cc
LD=/usr/ccs/bin/ld

# Solaris 8 / Forte Developer 6 Update 2, 64-bit SPARC kernel module.
# Sun's driver documentation recommends -D_KERNEL and -xarch=v9.
# Keep optimization conservative for the first prototype build.
KFLAGS=-D_KERNEL -xarch=v9

all: sunslip sunslipattach dltest iocvalues slipselftest ptysliptest

sunslip: sunslip.o
	$(LD) -r -o sunslip sunslip.o

sunslip.o: sunslip.c
	$(CC) $(KFLAGS) -c sunslip.c

sunslipattach: sunslipattach.c
	$(CC) -O -o sunslipattach sunslipattach.c

dltest: dltest.c
	$(CC) -O -o dltest dltest.c

iocvalues: iocvalues.c
	$(CC) -O -o iocvalues iocvalues.c

slipselftest: slipselftest.c
	$(CC) -O -o slipselftest slipselftest.c

ptysliptest: ptysliptest.c
	$(CC) -O -o ptysliptest ptysliptest.c

clean:
	rm -f sunslip.o sunslip sunslipattach dltest iocvalues slipselftest ptysliptest
