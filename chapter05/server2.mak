MAKEFILE_LIST:=$(MAKEFILE_LIST)

all:
	@echo "実行されたMakefileは$(MAKEFILE_LIST)です"

PROGRAM =       server2
OBJS    =       server2.o
SRCS    =       $(OBJS:%.o=%.c)
CFLAGS  =       -g -Wall
LDFLAGS =

$(PROGRAM):$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LDLIBS)
