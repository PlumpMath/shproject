CC = gcc
LD = gcc

CFLAGS = -std=c99 -O2 -MMD -MP -Wall
CPPFLAGS = -I.

LDFLAGS =


#
# Platform dependent options
#
UNAME := $(shell uname -s)
ifeq ($(UNAME), Linux)
	CFLAGS += -pthread -DUSE_EPOLL -D_XOPEN_SOURCE=700 -D_GNU_SOURCE
	LDFLAGS += -pthread
	LIBSRC += platform/loop_epoll.c
endif


.PHONY: clean all

TESTSRC=$(wildcard test/*.c)
TESTS=$(TESTSRC:.c=)

BIN=$(TESTS)

all : $(BIN)


#
# Library files
#
LIBSRC += async.c coro.c util/heap.c
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
