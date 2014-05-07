##
## This file is part of the sigrok-cli project.
##
## Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
##
## This program is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program.  If not, see <http://www.gnu.org/licenses/>.
##

# We require at least autoconf 2.63 (AC_INIT format changed there).
AC_PREREQ([2.63])

AC_INIT([sigrok-cli], [0.5.0], [sigrok-devel@lists.sourceforge.net],
	[sigrok-cli], [http://www.sigrok.org])

AC_CONFIG_SRCDIR([sigrok-cli.h])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([autostuff])
AC_CONFIG_AUX_DIR([autostuff])

# We require at least automake 1.11 (needed for 'silent rules').
AM_INIT_AUTOMAKE([1.11 -Wall -Werror std-options check-news])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AH_TOP([#ifndef SIGROK_CLI_CONFIG_H
#define SIGROK_CLI_CONFIG_H    /* To stop multiple inclusions. */])
AH_BOTTOM([#endif /* SIGROK_CLI_CONFIG_H */])

# Enable more compiler warnings via -Wall and -Wextra.
CFLAGS="$CFLAGS -Wall -Wextra"

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S

# Initialize libtool.
LT_INIT

# Initialize pkg-config.
# We require at least 0.22, as "Requires.private" behaviour changed there.
PKG_PROG_PKG_CONFIG([0.22])

# Checks for libraries.
AC_ARG_WITH([libsigrokdecode],
	AS_HELP_STRING([--with-libsigrokdecode],
			[compile with libsigrokdecode support]),
	[WITH_SRD=$withval],[WITH_SRD=yes])
AM_CONDITIONAL(WITH_SRD, test x$WITH_SRD = xyes)

AM_PATH_GLIB_2_0([2.28.0],
	[CFLAGS="$CFLAGS $GLIB_CFLAGS"; LIBS="$LIBS $GLIB_LIBS"])

PKG_CHECK_MODULES([libsigrok], [libsigrok >= 0.3.0],
	[CFLAGS="$CFLAGS $libsigrok_CFLAGS";
	LIBS="$LIBS $libsigrok_LIBS"])

if test "x$WITH_SRD" = "xyes"; then
	PKG_CHECK_MODULES([libsigrokdecode], [libsigrokdecode >= 0.3.0],
		[CFLAGS="$CFLAGS $libsigrokdecode_CFLAGS";
			LIBS="$LIBS $libsigrokdecode_LIBS";
			AC_DEFINE(HAVE_SRD, [1], [libsigrokdecode is available.])]
	)
fi

# Checks for header files.
# These are already checked: inttypes.h stdint.h stdlib.h string.h unistd.h.

AC_SUBST(MAKEFLAGS, '--no-print-directory')
AC_SUBST(AM_LIBTOOLFLAGS, '--silent')

AC_CONFIG_FILES([Makefile contrib/sigrok-cli_cross.nsi])

AC_OUTPUT

echo
echo "sigrok-cli configuration summary:"
echo
echo "  - Package version: $VERSION"
echo "  - Prefix: $prefix"
echo "  - Building on: $build"
echo "  - Building for: $host"
echo
echo "Detected libraries:"
echo

# Note: This only works for libs with pkg-config integration.
for lib in "glib-2.0 >= 2.28.0" "libsigrok >= 0.3.0" "libsigrokdecode >= 0.3.0"; do
	if test "$lib" = "libsigrokdecode" -a "$WITH_SRD" != "yes" ; then
		answer="no"
	else
		if `$PKG_CONFIG --exists $lib`; then
			ver=`$PKG_CONFIG --modversion $lib`
			answer="yes ($ver)"
		else
			answer="no"
		fi
	fi
	echo "  - (REQUIRED) $lib: $answer"
done

echo

