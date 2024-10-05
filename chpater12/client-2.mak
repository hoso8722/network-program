PROGRAM =       client-2
OBJS    =       parser.tab.o lex.yy.o client-2.o
SRCS    =       $(OBJS:%.o=%.c)
CFLAGS  =       -g -Wall
LDFLAGS =
BISON	=	bison
BISONFLAGS=	-d
FLEX	=	flex


$(PROGRAM):$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LDLIBS)
parser.tab.c: parser.y
	$(BISON) $(BISONFLAGS) parser.y
lex.yy.c: lexer.l
	$(FLEX) $(FLEXFLAGS) lexer.l
