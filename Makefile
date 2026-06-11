TARGET  := ethCmd
CC      := gcc
SRCS    := $(wildcard *.c)
OBJS    := $(SRCS:.c=.o)

CFLAGS  := -Wall -Wextra
LDFLAGS := -lpthread

ifeq ($(DBG),1)
CFLAGS += -O0 -ggdb
else
CFLAGS += -O2
endif

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f ./*.o

.PHONY: clean
