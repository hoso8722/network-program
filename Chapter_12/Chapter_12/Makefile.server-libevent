PROGRAM =       server-libevent
OBJS    =       server-libevent.o
SRCS    =       $(OBJS:%.o=%.c)
CFLAGS  =       -g -Wall
LDFLAGS =	-levent

$(PROGRAM):$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LDLIBS)

