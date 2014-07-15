##
## This file is part of the sigrok-cli project.
##
## Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

ACLOCAL_AMFLAGS = -I autostuff

bin_PROGRAMS = sigrok-cli

sigrok_cli_SOURCES = \
	main.c \
	show.c \
	device.c \
	session.c \
	input.c \
	decode.c \
	sigrok-cli.h \
	parsers.c \
	anykey.c \
	options.c

dist_man_MANS = doc/sigrok-cli.1

EXTRA_DIST = contrib/sigrok-cli_cross.nsi contrib/sigrok-logo-notext.ico

MAINTAINERCLEANFILES = ChangeLog

.PHONY: ChangeLog
ChangeLog:
	git --git-dir $(top_srcdir)/.git log > ChangeLog || touch ChangeLog

dist-hook: ChangeLog

