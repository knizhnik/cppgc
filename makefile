# -*- makefile -*-
# Makefile for Generic Unix with GCC compiler

DEBUG?=1

# Default install directory
PREFIX ?= /usr/local

# Place where to copy CppGC header files
INCSPATH=$(PREFIX)/include/cppgc

#Place where to copy CppGC library
LIBSPATH=$(PREFIX)/lib

GC_OBJS = gc.o threadctx.o
GC_INCS = gc.h threadctx.h gcclasses.h
GC_LIB = libgc.a
GC_EXAMPLES = testgc mallocbench

TFLAGS = -pthread 

CC = g++
ifeq ($(DEBUG), 1)
OPTIMIZATION=-O0
else
OPTIMIZATION=-O3
endif

CFLAGS = -c -I. -Wall $(OPTIMIZATION) -g -fPIC $(TFLAGS)

LD = $(CC)
LDFLAGS = -g $(TFLAGS)

AR = ar
ARFLAGS = -cru

ifneq (,$(findstring FreeBSD,$(OSTYPE)))
RANLIB = ranlib
else
RANLIB = true
endif

library: $(GC_LIB)

all: library examples

gc.o: gc.cpp $(GC_INCS)
		$(CC) $(CFLAGS) gc.cpp

threadctx.o: threadctx.cpp $(GC_INCS)
		$(CC) $(CFLAGS) threadctx.cpp


$(GC_LIB): $(GC_OBJS)
	rm -f $(GC_LIB)
	$(AR) $(ARFLAGS) $(GC_LIB) $(GC_OBJS)
	$(RANLIB) $(GC_LIB)

examples: $(GC_EXAMPLES) 

testgc: testgc.o $(GC_LIB)
	$(LD) $(LDFLAGS) -o testgc testgc.o $(GC_LIB)

testgc.o: samples/testgc.cpp $(GC_INCS)
	$(CC) $(CFLAGS) samples/testgc.cpp

mallocbench: mallocbench.o $(GC_LIB)
	$(LD) $(LDFLAGS) -std=c++0x -o mallocbench mallocbench.o $(GC_LIB)

mallocbench.o: samples/mallocbench.cpp $(GC_INCS)
	$(CC) $(CFLAGS) -std=c++0x samples/mallocbench.cpp

install: library
	mkdir -p $(INCSPATH)
	cp $(GC_INCS) $(INCSPATH)
	mkdir -p $(LIBSPATH)
	cp $(GC_LIB) $(LIBSPATH)

uninstall:
	rm -fr $(INCSPATH)
	cd $(LIBSPATH); rm -f $(GC_LIB) 

clean: 
	make -C copygc clean
	rm -f *.o *.a *.so *.so.* $(GC_EXAMPLES) 

tgz: clean
	cd ..; tar --exclude=.svn -chvzf cppgc-1.02.tar.gz cppgc

zip: clean
	cd ..; zip -r cppgc.zip cppgc

