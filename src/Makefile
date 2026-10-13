VERSION = 0.0.1

CFLAGS ?= -Wall -Wextra -Os
EXTRA_CFLAGS = -D_GNU_SOURCE -DPROGRAM_VERSION=$(VERSION)

ifneq ($(DEBUG),)
EXTRA_CFLAGS += -DDEBUG
endif

OBJS = main.o util.o log.o cmdline.o config.o key.o dm.o fs.o

ifeq ($(shell uname),Darwin)
EXTRA_CFLAGS += -Icompat
OBJS += compat/compat.o
endif

init: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

clean:
	$(RM) init $(OBJS)

.PHONY: clean
