#!/bin/sh
#
# Florence - Florence is a simple virtual keyboard for Gnome.
#
# Copyright (C) 2008 François Agrech
# Copyright (C) 2026 Devin Teske
#
# Regenerate the GNU build system with the host's unversioned autotools.
# Prefer this over hand-running aclocal/automake so aclocal.m4, missing,
# and the Makefile.in files stay in sync.
#
# Usage: ./autogen.sh && ./configure --without-docs && gmake
#

set -e
cd "$(dirname "$0")"

# gnome-doc-prepare is optional (gnome-doc-utils is extinct on modern hosts;
# --without-docs is the supported FreeBSD path).
if command -v gnome-doc-prepare >/dev/null 2>&1; then
	gnome-doc-prepare --force || true
fi

if command -v glib-gettextize >/dev/null 2>&1; then
	glib-gettextize --force --copy
fi
if command -v intltoolize >/dev/null 2>&1; then
	intltoolize --copy --force --automake
fi

# AC_CONFIG_MACRO_DIR([m4]) + ACLOCAL_AMFLAGS=-I m4; force -I for aclocal.
ACLOCAL="${ACLOCAL:-aclocal} -I m4"
export ACLOCAL
autoreconf -fi

echo "Build system regenerated. Next: ./configure --without-docs && gmake"
