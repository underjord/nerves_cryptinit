CFLAGS ?= -Wall -Wextra -Os
LDFLAGS ?=
TARGET = unlock

# For an initramfs you typically want a static binary so libc doesn't
# need to be present in the CPIO. Set CFLAGS to include -static when
# building with a Linux/musl toolchain. The default omits it so the
# example compiles cleanly on macOS for syntax checking.

all: $(TARGET)

$(TARGET): unlock.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	$(RM) $(TARGET)

.PHONY: all clean
