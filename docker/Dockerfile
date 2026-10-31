# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Build container for edk2-cix.
#
# Works on linux/amd64 and linux/arm64 hosts. On both architectures the
# AArch64 bare-metal GCC 10.2-2020.11 from Arm (the toolchain CIX documents)
# is used, so firmware built on an Apple Silicon Mac, on an x86_64 PC and in
# GitHub Actions comes from the same compiler version.

FROM debian:12-slim

ARG TARGETARCH
ARG GCC_VERSION=10.2-2020.11
ARG GCC_SHA256_AMD64=32abfbc7b24c56542f2a6e6969d6b8787e47f7223e8f2097d84151ebd9f86743
ARG GCC_SHA256_ARM64=4f9f060e2ca993ec9564054e17b6fdeacb47260e983f766f84f157d00345bf29
ARG ACPICA_TAG=R2024_12_12
ARG ACPICA_COMMIT=7dae72155bf06b0edda9f3aea713da1d48c1c418

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

RUN apt-get update && apt-get install -y --no-install-recommends \
        bc bison build-essential ca-certificates curl file flex git m4 \
        python3 python3-yaml python-is-python3 uuid-dev xz-utils \
        libssl3 openssl zlib1g rsync zip mtools \
        libssl-dev python3-pyelftools python3-cryptography \
    && rm -rf /var/lib/apt/lists/*

# Arm GNU toolchain (aarch64-none-elf)
RUN set -eux; \
    case "${TARGETARCH:-$(dpkg --print-architecture)}" in \
      amd64) host=x86_64;  sha="${GCC_SHA256_AMD64}" ;; \
      arm64) host=aarch64; sha="${GCC_SHA256_ARM64}" ;; \
      *) echo "unsupported architecture ${TARGETARCH}"; exit 1 ;; \
    esac; \
    name="gcc-arm-${GCC_VERSION}-${host}-aarch64-none-elf"; \
    curl -fsSL -o /tmp/gcc.tar.xz \
      "https://developer.arm.com/-/media/Files/downloads/gnu-a/${GCC_VERSION}/binrel/${name}.tar.xz"; \
    echo "${sha}  /tmp/gcc.tar.xz" | sha256sum -c -; \
    mkdir -p /opt/toolchain; \
    tar -xf /tmp/gcc.tar.xz -C /opt/toolchain; \
    ln -s "/opt/toolchain/${name}" /opt/toolchain/aarch64-none-elf; \
    rm /tmp/gcc.tar.xz

# ACPICA iasl, pinned to the tag CIX documents
RUN set -eux; \
    git clone --depth 1 --branch "${ACPICA_TAG}" https://github.com/acpica/acpica.git /tmp/acpica; \
    test "$(git -C /tmp/acpica rev-parse HEAD)" = "${ACPICA_COMMIT}"; \
    make -C /tmp/acpica -j"$(nproc)" iasl acpiexec acpidump acpixtract; \
    mkdir -p /opt/acpica/bin; \
    cp /tmp/acpica/generate/unix/bin/* /opt/acpica/bin/; \
    rm -rf /tmp/acpica

# Cortex-M firmware (SE, sensor fusion hub): Debian's arm-none-eabi GCC
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-arm-none-eabi libnewlib-arm-none-eabi binutils-arm-none-eabi \
    && rm -rf /var/lib/apt/lists/*

ENV GCC5_AARCH64_PREFIX=/opt/toolchain/aarch64-none-elf/bin/aarch64-none-elf- \
    IASL_PREFIX=/opt/acpica/bin/ \
    PATH=/opt/acpica/bin:/opt/toolchain/aarch64-none-elf/bin:$PATH

# The workspace is bind-mounted here; mark it safe for git regardless of
# the host UID that owns it.
RUN git config --system --add safe.directory '*'

WORKDIR /work
