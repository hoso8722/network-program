ARCH ?= $(shell uname -m)

ifeq ($(ARCH), x86_64)
	# x86_64用のコンパイルオプション
	PROGRAM =       x86_64_server
	OBJS    =       server.o
	SRCS    =       $(OBJS:%.o=%.c)
	CC = gcc
	CFLAGS = -g -Wall
	LDFLAGS =
else ifeq ($(ARCH), arm64)
	# ARM用のコンパイルオプション
	PROGRAM =       arm_server
	OBJS    =       server.o
	SRCS    =       $(OBJS:%.o=%.c)
	CC = clang
	CFLAGS = -g -Wall
	LDFLAGS =
endif

$(PROGRAM):$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LDLIBS)