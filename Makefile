CC = gcc
LD = gcc

CFLAGS = -std=c99 -MMD -MP -Wall
CPPFLAGS = -I.


ifeq ($(DEBUG), 1)
	CFLAGS += -g
else
	CFLAGS += -O2
endif


LDFLAGS =


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
# Config for platform dependent options
#
ifeq ($(LOOP_EPOLL), 1)
	LIBSRC += platform/poll_epoll.c
	CFLAGS += -DPOLL_EPOLL
endif

ifeq ($(SCHED_LINUX), 1)
	LIBSRC += platform/sched_linux.c
	CFLAGS += -DSCHED_LINUX
endif



.PHONY: clean all

TESTSRC=$(wildcard test/*.c)
TESTS=$(TESTSRC:.c=)

BIN=$(TESTS)

all : $(BIN)


#
# Library files
#
LIBSRC += async.c context.c sched.c util/heap.c
LIBOBJ += $(LIBSRC:.c=.o)


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

% : %.o
	$(LD) $^ $(LDFLAGS) -o $@


#
# Dependency file generation
#
SRC=$(wildcard *.c) $(wildcard platform/*.c) $(wildcard test/*.c) $(wildcard util/*.c)

OBJ=$(SRC:.c=.o)
DEP=$(OBJ:.o=.d)

-include $(DEP)
