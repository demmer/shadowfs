
OBJS := dispatch_ops.o root_ops.o shadow_ops.o offline.o main.o
LL_OBJS := ll_shadow_ops.o offline.o ll_main.o

CFLAGS := -g -Wall -D_FILE_OFFSET_BITS=64
#CFLAGS := -g -Wall -I/tmp/fuse-2.7.3/include -D_FILE_OFFSET_BITS=64

UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
LDFLAGS := -losxfuse
else
LDFLAGS := -lfuse
endif

all: shadowfs ll_shadowfs

%.o: %.cc shadowfs.h
	g++ $(CFLAGS) -c $< -o $@

%.o: %.c shadowfs.h
	gcc $(CFLAGS) -c $< -o $@

shadowfs: $(OBJS)
	g++ $^ -o $@ $(LDFLAGS)

ll_shadowfs: $(LL_OBJS)
	g++ $^ -o $@ $(LDFLAGS)

clean:
	rm -f *.o *.E shadowfs ll_shadowfs
