# Convenience wrapper. Real work happens in src/Makefile and tests/qemu/.

.PHONY: all build check clean help qemu-check qemu-build qemu-clean distclean

all: build

build:
	$(MAKE) -C src

check: build
	cd tests && ./run_tests.sh

# QEMU integration tests. Slow first time (~30 minutes for the kernel
# build); cached after. Requires Docker and qemu-system-aarch64.
qemu-build:
	$(MAKE) -C tests/qemu build

qemu-check:
	$(MAKE) -C tests/qemu check

qemu-clean:
	$(MAKE) -C tests/qemu distclean

clean:
	$(MAKE) -C src clean

distclean: clean qemu-clean

help:
	@echo "nerves_cryptinit Makefile targets"
	@echo "---------------------------------"
	@echo "build       - Build the init binary (default)"
	@echo "check       - Build and run host (LD_PRELOAD) tests"
	@echo "qemu-build  - Build artifacts for QEMU tests (Docker + Buildroot)"
	@echo "qemu-check  - Build + run QEMU integration tests"
	@echo "qemu-clean  - Drop QEMU build artifacts and the cached volume"
	@echo "clean       - Remove host build artifacts"
