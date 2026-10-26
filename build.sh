#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Build edk2-cix firmware inside the build container.
#
#   ./build.sh --list
#   ./build.sh orion-o6 ms-r1
#   ./build.sh --all --image opi6plus=~/Downloads/OPI6PLUS_BIOS_1.4_General.bin \
#                    --image opi6=~/Downloads/opi6_bios_image_20261019.img
#
# All arguments are passed to scripts/build.py.  Vendor images given as local
# paths are mounted read-only into the container.  Set EDK2CIX_NO_DOCKER=1 to
# run scripts/build.py directly (e.g. when already inside the container).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${EDK2CIX_IMAGE:-edk2-cix-build}"

if [[ "${EDK2CIX_NO_DOCKER:-0}" == 1 ]]; then
  exec python3 "$ROOT/scripts/build.py" "$@"
fi

if ! command -v docker >/dev/null; then
  echo "docker is required (or set EDK2CIX_NO_DOCKER=1 inside a prepared environment)" >&2
  exit 1
fi

# Rebuild the container when docker/Dockerfile changes.
tag="$(cat "$ROOT/docker/Dockerfile" | { sha256sum 2>/dev/null || shasum -a 256; } | cut -c1-12)"
if ! docker image inspect "$IMAGE:$tag" >/dev/null 2>&1; then
  echo "[edk2-cix] building container $IMAGE:$tag"
  docker build -t "$IMAGE:$tag" "$ROOT/docker"
fi

mounts=()
args=()
while (($#)); do
  case "$1" in
    --image)
      spec="$2"; shift 2
      key="${spec%%=*}"; src="${spec#*=}"
      src="${src/#\~/$HOME}"
      if [[ "$src" != http://* && "$src" != https://* ]]; then
        [[ -f "$src" ]] || { echo "vendor image not found: $src" >&2; exit 1; }
        abs="$(cd "$(dirname "$src")" && pwd)/$(basename "$src")"
        mounts+=(-v "$abs:/vendor/$key:ro")
        src="/vendor/$key"
      fi
      args+=(--image "$key=$src")
      ;;
    *) args+=("$1"); shift ;;
  esac
done

envs=()
for v in $(env | grep -oE '^(EDK2CIX_[A-Z0-9_]+|SOURCE_DATE_EPOCH|GITHUB_REPOSITORY)=' | tr -d =); do
  envs+=(-e "$v")
done

tty=()
[[ -t 1 ]] && tty=(-t)

exec docker run --rm ${tty[@]+"${tty[@]}"} \
  --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$ROOT:/work" -w /work \
  "${mounts[@]+"${mounts[@]}"}" "${envs[@]+"${envs[@]}"}" \
  "$IMAGE:$tag" python3 scripts/build.py "${args[@]+"${args[@]}"}"
