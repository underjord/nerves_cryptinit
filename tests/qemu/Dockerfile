# Build environment for the QEMU integration tests. Has Buildroot's
# prerequisites plus the tools we need to assemble per-test images:
#   - veritysetup (cryptsetup-bin) to format the dm-verity hash tree
#   - squashfs-tools to build the rootfs
#   - fwup to write the rootfs out dm-crypt-encrypted (offline, AES-XTS)
#
# This image is only used for *building* — QEMU itself runs on the host.

FROM debian:trixie-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        bc bison flex \
        libssl-dev libelf-dev libncurses-dev \
        cpio rsync \
        python3 \
        wget file unzip ca-certificates git \
        cryptsetup-bin \
        squashfs-tools \
        autoconf automake libtool pkg-config \
        libconfuse-dev libarchive-dev \
    && rm -rf /var/lib/apt/lists/*

# Build fwup from source. The version in Buildroot/Debian predates
# aes-xts-plain64 support (added in fwup 1.16.0), which we need to match
# dm-crypt's default cipher offline. Building from the release tarball
# also sidesteps fwup shipping no arm64 .deb (so this works on Apple
# Silicon Docker too). A shared build against the distro's libconfuse is
# fine here: we only *create* .fw files on the host, never run them on a
# device, so the old-libconfuse caveat in fwup's docs doesn't apply.
ARG FWUP_VERSION=1.16.0
RUN cd /tmp && \
    wget -q "https://github.com/fwup-home/fwup/releases/download/v${FWUP_VERSION}/fwup-${FWUP_VERSION}.tar.gz" && \
    tar xf "fwup-${FWUP_VERSION}.tar.gz" && \
    cd "fwup-${FWUP_VERSION}" && \
    ./configure && \
    make -j"$(nproc)" && \
    make install && \
    cd / && rm -rf "/tmp/fwup-${FWUP_VERSION}" "/tmp/fwup-${FWUP_VERSION}.tar.gz"

WORKDIR /work
