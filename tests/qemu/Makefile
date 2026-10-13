# QEMU integration test harness. Driven from the project root via
# `make qemu-check`.

DOCKER_IMAGE = nerves-cryptinit-builder
REPO_ROOT = $(abspath ../..)
SCENARIOS = 01-plaintext 02-dm-crypt-hex

.PHONY: all check build image scenarios clean distclean help

all: check

check: build scenarios

# Build the Docker image used for kernel and rootfs assembly.
image:
	docker build -t $(DOCKER_IMAGE) .

# Build the kernel + busybox + cross-compiled cryptinit + encrypt helper.
# Slow first time (~30 minutes); fast on subsequent invocations because
# the buildroot output is cached in a Docker volume.
build: image
	docker volume create cryptinit-build >/dev/null
	docker run --rm \
		-e HOST_UID=$(shell id -u) \
		-e HOST_GID=$(shell id -g) \
		-v $(REPO_ROOT):/work \
		-v cryptinit-build:/build \
		$(DOCKER_IMAGE) \
		/work/tests/qemu/build.sh

scenarios:
	@for scenario in $(SCENARIOS); do \
		./run-test.sh $$scenario || exit 1; \
	done

# Run a single scenario by name: `make 01-plaintext` etc.
$(SCENARIOS): build
	./run-test.sh $@

clean:
	rm -rf build/test-*

distclean:
	rm -rf build
	docker volume rm cryptinit-build 2>/dev/null || true

help:
	@echo "QEMU test targets"
	@echo "  make check      - build + run all scenarios"
	@echo "  make build      - build kernel/busybox/cryptinit (slow first time)"
	@echo "  make scenarios  - run all scenarios against existing build"
	@echo "  make 01-plaintext, 02-dm-crypt-hex - run a single scenario"
	@echo "  make clean      - remove per-scenario build dirs"
	@echo "  make distclean  - also drop the buildroot volume (loses cache)"
