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

AC_PREREQ(2.61)
AC_INIT([sllqbench], [1.0.0], [admin@dns-oarc.net], [sllqbench], [https://github.com/DNS-OARC/sllq/issues])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_CONFIG_SRCDIR([sllqbench.c])
AC_CONFIG_HEADER([config.h])

AC_PROG_CC
AM_PROG_CC_C_O

AX_SLLQ
AC_SEARCH_LIBS([clock_gettime], [rt])

AC_CONFIG_FILES([Makefile test/Makefile])
AC_OUTPUT
