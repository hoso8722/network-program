PROGRAM =       server9
OBJS    =       server9.o
SRCS    =       $(OBJS:%.o=%.c)
CFLAGS  =       -g -Wall
LDFLAGS =       -lpthread

$(PROGRAM):$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LDLIBS)
