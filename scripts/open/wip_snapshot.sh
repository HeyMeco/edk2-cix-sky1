#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
# Save the working tree's state of firmware/csupm and scripts/open as a
# commit under refs/wip/NAME, without touching the index, the working tree
# or any branch (a checkpoint for work in progress).
#   wip_snapshot.sh NAME "message"
set -e
name=$1; msg=${2:-checkpoint}
[ -n "$name" ] || { echo "usage: $0 NAME [message]" >&2; exit 2; }
cd "$(git rev-parse --show-toplevel)"
idx=$(mktemp); trap 'rm -f "$idx"' EXIT
GIT_INDEX_FILE=$idx git read-tree HEAD
GIT_INDEX_FILE=$idx git add -A firmware/csupm scripts/open
tree=$(GIT_INDEX_FILE=$idx git write-tree)
parent=$(git rev-parse -q --verify "refs/wip/$name" || git rev-parse HEAD)
commit=$(git commit-tree "$tree" -p "$parent" -m "$msg")
git update-ref "refs/wip/$name" "$commit"
echo "refs/wip/$name -> $commit"
