CC = gcc
LD = gcc
AS = gcc

ASFLAGS = -Wall
CFLAGS = -std=c99 -MMD -MP -Wall
CPPFLAGS = -I.


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


ARCH := $(shell uname -m)
ifeq ($(ARCH), x86_64)
	LIBOBJ += context_gcc_amd64.o
	CFLAGS += -DCONTEXT_GCC_AMD64
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

TESTSRC=$(wildcard test/*.c)
TESTS=$(TESTSRC:.c=)

BIN=$(TESTS)

all : $(BIN)


#
# Library files
#
LIBOBJ += async.o scheduler.o util/heap.o


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
SRC=$(wildcard *.c) $(wildcard platform/*.c) $(wildcard test/*.c) $(wildcard util/*.c)

OBJ=$(SRC:.c=.o)
DEP=$(OBJ:.o=.d)

-include $(DEP)
