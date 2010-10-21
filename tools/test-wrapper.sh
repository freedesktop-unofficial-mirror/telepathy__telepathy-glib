#!/bin/sh
# Make tests shut up. On success, if stdout is a tty, we only output messages
# about skipped tests; on failure, or if stdout is a file or pipe, we output
# the lot.
#
# Usage: test-wrapper.sh PROGRAM [ARGS...]

set -e
e=0
"$@" > capture-$$.log 2>&1 || e=$?
if test z$e = z0 && test -t 1; then
  grep -i skipped capture-$$.log || true
  rm -f capture-$$.log
else
  cat capture-$$.log
  exit $e
fi

# Copyright © 2010 Collabora Ltd. <http://www.collabora.co.uk/>
# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved. There is no warranty.
