# Author Jerry Lundström <jerry@dns-oarc.net>
# Copyright (c) 2017, OARC, Inc.
# All rights reserved.
#
# This file is part of sllq.
#
# sllq is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# sllq is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with sllq.  If not, see <http://www.gnu.org/licenses/>.

ACLOCAL_AMFLAGS = -I ../m4

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
CLEANFILES = sllq.c sllq.h

SUBDIRS = test

AM_CFLAGS = -Wall -I$(srcdir) -I$(top_srcdir)/../ $(PTHREAD_CFLAGS)

bin_PROGRAMS      = sllqbench

sllqbench_SOURCES = sllqbench.c sllq.c
sllqbench_LDADD   = $(PTHREAD_LIBS)

sllq.c: $(top_srcdir)/../sllq.c sllq.h
	cp "$(top_srcdir)/../sllq.c" .

sllq.h: $(top_srcdir)/../sllq.h
	cp "$(top_srcdir)/../sllq.h" .

test: check
