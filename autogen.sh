#!/bin/sh
mkdir -vp build-aux/m4 || exit $?
gtkdocize || exit 1
autoreconf -fi
