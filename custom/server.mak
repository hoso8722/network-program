ARCH ?= $(shell uname -m)
MAKEFILE_NAME:=$(shell basename $(MAKEFILE_LIST) .mak)

ifeq ($(ARCH), x86_64)
	# x86_64用のコンパイルオプション
	CFLAGS = -g -Wall
	LDFLAGS =
	PROGRAM =       x86_64_$(MAKEFILE_NAME)
	OBJS    =       x86_64_$(MAKEFILE_NAME).o
	SRCS    =       $(OBJS:%.o=%.c)

else ifeq ($(ARCH), arm64)
	# ARM用のコンパイルオプション
	CFLAGS = -g -Wall
	LDFLAGS = 
	PROGRAM =       arm_$(MAKEFILE_NAME)
	OBJS    =       arm_$(MAKEFILE_NAME).o
	SRCS    =       $(MAKEFILE_NAME).c

endif

# リンキング
$(PROGRAM):$(OBJS)
	@echo "Linking $(PROGRAM)"
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LDLIBS)
	
# オブジェクトファイルを作成
$(OBJS):$(SRCS)
	@echo "objecting $(OBJS)"
	$(CC) $(CFLAGS) $(LDFLAGS) -c -o $(OBJS) $(SRCS)
