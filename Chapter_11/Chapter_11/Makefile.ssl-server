PROGRAM=ssl-server
OBJS=ssl-server.o
SRCS=$(OBJS:%.o=%.c)
#CFLAGS=-Wall -g -I/usr/local/ssl/include
#LDFLAGS= -L/usr/local/ssl/lib
#LDLIBS= -lssl -lcrypto -ldl
CFLAGS=-Wall -g -DUSE_DH -DCHECK_CLIENT_CERT
#CFLAGS=-Wall -g
LDFLAGS=
LDLIBS= -lssl -lcrypto
$(PROGRAM):$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LDLIBS)
