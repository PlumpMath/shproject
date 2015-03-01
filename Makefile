CC = gcc
LD = gcc
AS = gcc


ASFLAGS += -Wall
CFLAGS += -std=c99 -MMD -MP -Wall
CPPFLAGS += -I. -I./bench


ifeq ($(DEBUG), 1)
	CFLAGS += -g
else
	CFLAGS += -O2
endif


LDFLAGS =


# Override for Windows
LOCK_POSIX = 1

#
# Platform dependent options
#
UNAME := $(shell uname -s)
ifeq ($(UNAME), Linux)
	CFLAGS += -pthread -D_XOPEN_SOURCE=700 -D_GNU_SOURCE
	LDFLAGS += -pthread -lrt
	LOOP_EPOLL = 1
	SCHED_LINUX = 1
endif

#
# Allow architecture to be overriden by environment variables.
#
ifndef ARCH
	ARCH := $(shell uname -m)
endif

ifeq ($(ARCH), x86_64)
	LIBOBJ += arch/context_gcc_amd64.o
	CFLAGS += -DCONTEXT_GCC_AMD64
else ifeq ($(ARCH), x86)
	LIBOBJ += arch/context_gcc_x86.o
	CFLAGS += -DCONTEXT_GCC_X86
endif


#
# Config for platform dependent options
#
ifeq ($(LOOP_EPOLL), 1)
	LIBOBJ += platform/poll_epoll.o
	CFLAGS += -DPOLL_EPOLL
endif

ifeq ($(SCHED_LINUX), 1)
	LIBOBJ += platform/sched_linux.o
	CFLAGS += -DSCHED_LINUX
endif

ifeq ($(LOCK_POSIX), 1)
	CFLAGS += -DLOCK_POSIX
endif



.PHONY: clean all


#
# Benchmark targets
#

BENCHMARKS =	bench/webserver/coroserv \
				bench/webserver/threadserv \
				bench/webserver/eventserv

BIN=$(TESTS) $(BENCHMARKS)
all : $(BIN)

bench/webserver/coroserv.o : bench/webserver/main.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) -DWEBSERVER_COROUTINES $< -o $@

bench/webserver/threadserv.o : bench/webserver/main.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) -DWEBSERVER_THREADS $< -o $@

bench/webserver/eventserv.o : bench/webserver/eventserv.c

bench/webserver/eventserv : bench/webserver/eventlib.o


$(BENCHMARKS) : bench/http-parser/http_parser.o


TESTSRC=$(wildcard test/*.c)
TESTS=$(TESTSRC:.c=)


#
# Library files
#
LIBOBJ += async.o scheduler.o util/heap.o


bench/webserver/coroserv : $(LIBOBJ)


#
# Tests
#
$(TESTS): $(LIBOBJ)


#
# Clean
#
clean:
	rm -f $(BIN) $(OBJ) $(DEP)


#
# Basic compile rules
#
%.o : %.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

%.o : %.S
	$(AS) -c $(ASFLAGS) $(CPPFLAGS) $< -o $@

% : %.o
	$(LD) $^ $(LDFLAGS) -o $@


#
# Dependency file generation
#
SRC=$(wildcard *.c) $(wildcard platform/*.c) $(wildcard test/*.c) $(wildcard util/*.c) \
	$(wildcard bench/webserver/*.c) $(wildcard bench/http-parser/*.c)

OBJ=$(SRC:.c=.o)
DEP=$(OBJ:.o=.d)

-include $(DEP)
