PROGRAM =       pdump
OBJS    =       pdump.o
SRCS    =       $(OBJS:%.o=%.c)
CFLAGS  =       -g -Wall
LDFLAGS =

$(PROGRAM):$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LDLIBS)
