SRC = ../../src

CFLAGS ?= -Wall -Wextra -O0 -g
EXTRA_CFLAGS = -D_GNU_SOURCE -DPROGRAM_VERSION=0.0.1-test -I$(SRC)

TESTS = test_cmdline test_config test_key test_fs

all: $(TESTS)

test_cmdline: test_cmdline.c $(SRC)/cmdline.c $(SRC)/log.c $(SRC)/util.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(LDFLAGS) -o $@ $^

test_config: test_config.c $(SRC)/config.c $(SRC)/log.c $(SRC)/util.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(LDFLAGS) -o $@ $^

test_key: test_key.c $(SRC)/key.c $(SRC)/log.c $(SRC)/util.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(LDFLAGS) -o $@ $^

test_fs: test_fs.c $(SRC)/fs.c $(SRC)/log.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(LDFLAGS) -o $@ $^

clean:
	$(RM) $(TESTS) *.o

.PHONY: all clean
