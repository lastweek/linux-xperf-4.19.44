CFLAGS := -static

SRCS := $(wildcard *.c posix/*.c)
OBJS := $(SRCS:.c=.o)

all: $(OBJS)

clean:
	rm -f *.o

%.o: %.c
	gcc -O1 -g -o $@ $< -lm -pthread
