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

AC_INIT([sigrok-cli], [0.6.0], [sigrok-devel@lists.sourceforge.net],
	[sigrok-cli], [http://www.sigrok.org])
AC_CONFIG_SRCDIR([sigrok-cli.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([autostuff])
AC_CONFIG_HEADERS([config.h])

# We require at least automake 1.11 (needed for 'silent rules').
AM_INIT_AUTOMAKE([1.11 -Wall -Werror no-define std-options check-news])
AM_SILENT_RULES([yes])

AC_CANONICAL_HOST

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Set the standard the C library headers should conform to.
AH_VERBATIM([_POSIX_C_SOURCE], [/* The targeted POSIX standard. */
#ifndef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200112L
#endif])

# Get compiler versions.
SR_PROG_VERSION([$CC], [sc_cc_version])

# Set up the sigrok-cli version defines.
SR_PKG_VERSION_SET([SC_PACKAGE_VERSION], [AC_PACKAGE_VERSION])

#############################
##  Optional dependencies  ##
#############################

# Initialize pkg-config.
# We require at least 0.22, as "Requires.private" behaviour changed there.
PKG_PROG_PKG_CONFIG([0.22])

# Keep track of all checked modules so we can list them at the end.
SR_PKG_CHECK_SUMMARY([sc_pkglibs_summary])

# Collect the pkg-config module names of all dependencies.
SR_VAR_OPT_PKG([SC_PKGLIBS])

SR_ARG_OPT_PKG([libsigrokdecode], [SRD],,
	[libsigrokdecode >= 0.4.0])

######################
##  Feature checks  ##
######################

AC_LANG([C])

# Enable the C99 standard if possible.
SC_EXTRA_CFLAGS=
SR_CHECK_COMPILE_FLAGS([SC_EXTRA_CFLAGS], [C99],
	[-std=c99 -c99 -AC99 -qlanglvl=extc99])

SR_ARG_ENABLE_WARNINGS([SC_WFLAGS], [-Wall], [-Wall -Wextra])

# Check host characteristics.
AC_SYS_LARGEFILE

##############################
##  Finalize configuration  ##
##############################

# Retrieve the compile and link flags for all modules combined.
# Also, bail out at this point if any module dependency is not met.
PKG_CHECK_MODULES([SIGROK_CLI],
	[glib-2.0 >= 2.32.0 libsigrok >= 0.4.0 $SC_PKGLIBS])

sc_glib_version=`$PKG_CONFIG --modversion glib-2.0 2>&AS_MESSAGE_LOG_FD`
sc_libsigrok_version=`$PKG_CONFIG --modversion libsigrok 2>&AS_MESSAGE_LOG_FD`

AC_CONFIG_FILES([Makefile contrib/sigrok-cli_cross.nsi])

AC_OUTPUT

cat >&AS_MESSAGE_FD <<_EOF

sigrok-cli configuration summary:
 - Package version................. $SC_PACKAGE_VERSION
 - Prefix.......................... $prefix
 - Building on..................... $build
 - Building for.................... $host

Compile configuration:
 - C compiler...................... $CC
 - C compiler version.............. $sc_cc_version
 - C compiler flags................ $CFLAGS
 - Additional C compiler flags..... $SC_EXTRA_CFLAGS
 - C compiler warnings............. $SC_WFLAGS

Detected libraries (required):
 - glib-2.0 >= 2.32.0.............. $sc_glib_version
 - libsigrok >= 0.4.0.............. $sc_libsigrok_version

Detected libraries (optional):
$sc_pkglibs_summary
_EOF
