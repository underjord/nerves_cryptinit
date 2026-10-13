CFLAGS ?= -fPIC -O0 -g -Wall -Wextra -Wno-unused-parameter
LDFLAGS += -fPIC -shared

ifeq ($(shell uname),Darwin)
CFLAGS += -I../../src/compat
else
LDFLAGS += -ldl
endif

TARGET = init_fixture.so

all: $(TARGET)

$(TARGET): init_fixture.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

clean:
	$(RM) $(TARGET) *.o

.PHONY: all clean
