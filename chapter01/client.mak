ARCH ?= $(shell uname -m)
MAKEFILE_NAME:=$(shell basename $(MAKEFILE_LIST) .mak)

ifeq ($(ARCH), x86_64)
	# x86_64用のコンパイルオプション
	PROGRAM =       x86_64_$(MAKEFILE_NAME)
	OBJS    =       x86_64_$(MAKEFILE_NAME).o
	SRCS    =       $(OBJS:%.o=%.c)
	CC = gcc
	CFLAGS = -g -Wall
	LDFLAGS =
else ifeq ($(ARCH), arm64)
	# ARM用のコンパイルオプション
	CC = clang
	CFLAGS = -g -Wall
	LDFLAGS =
	PROGRAM =       arm_$(MAKEFILE_NAME)
	OBJS    =       arm_$(MAKEFILE_NAME).o
	SRCS    =       $(OBJS:%.o=%.c)
endif

$(PROGRAM):$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LDLIBS)