PROGRAM =       u-client2
OBJS    =       u-client2.o
SRCS    =       $(OBJS:%.o=%.c)
CFLAGS  =       -g -Wall
LDFLAGS =

$(PROGRAM):$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LDLIBS)
