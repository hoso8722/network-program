PROGRAM =       ssl-client
OBJS    =       ssl-client.o
SRCS    =       $(OBJS:%.o=%.c)
#CFLAGS	=	-Wall -g -I/usr/local/ssl/include
#LDFLAGS=	-L/usr/local/ssl/lib -lssl -lcrypto -ldl
CFLAGS  =       -g -Wall -DCHECK_SERVER_CERT -DCHECK_CLIENT_CERT
#CFLAGS =       -g -Wall -DCHECK_CLIENT_CERT
#CFLAGS =       -g -Wall -DCHECK_SERVER_CERT
LDFLAGS =	-lssl -lcrypto

$(PROGRAM):$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LDLIBS)
