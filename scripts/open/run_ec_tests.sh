#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
# Run test_ec.py (open O6 EC vs Radxa's EC V1.07 in the IT5570 model) with
# PyPy when available (about 28x faster: the full suite in ~1 min instead
# of ~25 min), else python3.  PyPy: pypy3 on PATH, or its path in $PYPY.
#   run_ec_tests.sh [-v] [scenario...]
cd "$(dirname "$0")" || exit 1
if [ -n "$PYPY" ] && [ -x "$PYPY" ]; then
	PY=$PYPY
elif command -v pypy3 >/dev/null 2>&1; then
	PY=pypy3
else
	echo "run_ec_tests.sh: no PyPy (pypy3 on PATH or \$PYPY); using python3 (slow)" >&2
	PY=python3
fi
exec "$PY" -u test_ec.py "$@"
