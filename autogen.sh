#!/bin/sh
mkdir -vp build-aux/m4 || exit $?
autoreconf -vfi
