PROGRAM =       signaltest
OBJS    =       signaltest.o
SRCS    =       $(OBJS:%.o=%.c)
CFLAGS  =       -g -Wall -DUSE_SIGNAL
LDFLAGS =

$(PROGRAM):$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LDLIBS)
