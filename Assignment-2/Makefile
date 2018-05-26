# Makefile for Esterel v5_92 programs
# 
# Author: Jan Lukoschus
#
# 2002-06-10 jlu - first version
# 2002-06-12 jlu - remove intermediate files for xes
# 2002-06-17 jlu - EFLAGS4X added
# 2002-06-18 jlu - export ESTEREL
# 2002-06-20 jlu - disable some implicit rules
# 2002-06-20 jlu - keep some intermediate with .PRECIOUS
# 2002-06-20 jlu - link with -lpthread
# 2003-03-24 jlu - v5_92
# 2004-01-22 jlu - create empty .h _data.c
# 2005-02-02 jlu - ctype.c: fix for XES with glibc-2.3.3
# 2005-02-02 jlu - use 32-bit build on x86_64


default:
	@echo "Usage:"
	@echo "  'make file.c'    - Compile Esterel code 'file.strl' into C Code in 'file.c'"
	@echo "  'make file'      - Compile and link Esterel code in 'file.strl' with C code"
	@echo "                     in 'file_data.c' and build executable 'file'"
	@echo "  'make file.xes'  - Compile and link Esterel code in 'file.strl' with xes debugger"
	@echo "                     and build executable 'file.exe' and shell wrapper 'file.xes'"

# Force 32-bit build on Athlon64/Opteron systems
ifeq ($(shell uname -p), x86_64)
  GCC32=-m32 
endif

export ESTEREL=/opt/esterelv6_01

CC       = gcc
CFLAGS   = $(GCC32) -Wall -L$(ESTEREL)/lib -I$(ESTEREL)/include
LDFLAGS  = $(GCC32) -L$(ESTEREL)/lib -I$(ESTEREL)/include -lrt -lpthread

#CFG      = /home/esterel/etc/v5_92-env
#ESTEREL := $(shell sed -n -e "s/ESTEREL=\(.*\)/\1/p" $(CFG))

ESTEREL2C= $(ESTEREL)/bin/esterel
EFLAGS   =
EFLAGS4X = -simul -I

XES      = $(ESTEREL)/bin/xes
XESFLAGS = -cc "$(CC)" $(foreach t,$(GCC32),-Xcomp $(t))

XEVE     = $(ESTEREL)/bin/xeve

# Fix for XES on a Linux system with glibc-2.3.3:
#    ": undefined reference to `__ctype_b'"
# see: http://newweb.ices.utexas.edu/misc/ctype.c
#
ifeq ($(shell uname -s), Linux)
  XESCTYPEFIX=ctype.o
endif


# intermediate files to keep after compiling
.PRECIOUS: %.c %.o %_data.o

# disable some implicit rules 
%.o: %.c
%:   %.c
%:   %.o

ctype.o: ctype.c
	@echo " *** C COMPILE XES/glibc-2.3.3 fix $^  --->  $@"
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	@echo " *** C COMPILE $^  --->  $@"
	$(CC) $(CFLAGS) -c $< -o $@

%.c: %.strl %.h
	@echo " *** Esterel COMPILE $<  --->   $@"
	$(ESTEREL2C) $(EFLAGS) $<

%: %.c %_data.o
	$(MAKE) $*.o
	@echo " *** C LINK $*.o  --->  $@"
	$(CC) $(LDFLAGS) $*.o $*_data.o -o $@

%.xes: %.strl %.h $(XESCTYPEFIX) %_data.o
	@echo " *** Esterel COMPILE $<  --->   $*.c"
	$(ESTEREL2C) $(EFLAGS) $(EFLAGS4X) $<
	@echo " *** C COMPILE $*.c  --->  $*.o"
	$(CC) $(CFLAGS) -c $*.c -o $*.o
	rm -f $*.c
	@echo " *** XES LINK $<  --->   $*.exe"
	$(XES) $(XESFLAGS) $*.o $*_data.o $(XESCTYPEFIX) -o $*
	rm -f $*.o
	rm -f $*_data.o
	echo "#!/bin/sh"           > $@
	echo "ESTEREL=$(ESTEREL)" >> $@
	echo "export ESTEREL"     >> $@
	echo "exec $(XES) $*.exe"      >> $@
	chmod 755 $@
	@echo " #"
	@echo " # Use $@ to execute $*.exe"
	@echo " #"

%.xev: %.strl
	@echo " * Esterel Verification $<  --->   $*.blif"
	$(ESTEREL2C) -Lblif:-soft $<
	echo "#!/bin/sh"           > $@
	echo "ESTEREL=$(ESTEREL)" >> $@
	echo "export ESTEREL"     >> $@
	echo "exec $(XEVE) $*.blif"        >> $@
	chmod 755 $@
	@echo " #"
	@echo " # Use $@ to verify $*.blif file"
	@echo " #"


%.h %_data.o:
	@if [ ! -e $*_data.c ]; then echo " *** Create empty file  --->  $*_data.c"; touch $*_data.c; fi
	$(CC) $(CFLAGS) -c $*_data.c -o $*_data.o

clean:
	rm -f *.o *.exe *.xes *.xev *.blif
	rm -f $(patsubst %.strl,%.c,$(wildcard *.strl))
	rm -f $(patsubst %.strl,%,$(wildcard *.strl))

