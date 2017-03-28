# Semi Lock-Less Queue

[![Build Status](https://travis-ci.org/DNS-OARC/sllq.svg?branch=develop)](https://travis-ci.org/DNS-OARC/sllq) [![Coverity Scan Build Status](https://scan.coverity.com/projects/11847/badge.svg)](https://scan.coverity.com/projects/dns-oarc-sllq)

## About

This is a helper library for sending data between threads or within the
same process between logic layers.

## Queue Modes

Current modes for the queues are:
- `SLLQ_MUTEX`: Use POSIX thread mutexes and conditions
- `SLLQ_PIPE`: Use UNIX pipes

## Usage

Here is a short example how to use this, see the sllqbench directory
for a more complete example.

```c
#include "config.h"
#include "sllq/sllq.h"
#include <stdlib.h>
#include <pthread.h>

void* push(void* vp) {
    sllq_t* q = (sllq_t*)vp;
    size_t n = 10;
    int err;
    void* data = 0xdeadbeef;

    while (n--) {
        err = SLLQ_EAGAIN;
        while (err == SLLQ_EAGAIN || err == SLLQ_FULL)
            err = sllq_push(q, data, 0);
        if (err != SLLQ_OK)
            exit(1);
    }

    return 0;
}

void* shift(void* vp) {
    sllq_t* q = (sllq_t*)vp;
    size_t n = 10;
    int err;
    void* data = 0;

    while (n--) {
        err = SLLQ_EAGAIN;
        while (err == SLLQ_EAGAIN || err == SLLQ_FULL)
            err = sllq_shift(q, &data, 0);
        if (err != SLLQ_OK)
            exit(1);
    }

    return 0;
}

int main(void) {
    sllq_t q = SLLQ_T_INIT;
    pthread_t thrpush, thrshift;

    sllq_set_mode(&q, SLLQ_MUTEX);
    sllq_set_size(&q, 0x100);
    sllq_init(&q);

    pthread_create(&thrpush, 0, push, (void*)&q);
    pthread_create(&thrshift, 0, shift, (void*)&q);
    pthread_join(thrpush, 0);
    pthread_join(thrshift, 0);

    return 0;
}
```

### git submodule

```shell
git submodule init
git submodule add https://github.com/DNS-OARC/sllq.git src/sllq
```

### auto(re)conf

```shell
autoreconf ... --include=src/sllq/m4
```

### configure.ac

```m4
AX_SLLQ
```

### Top level Makefile.am

```m4
ACLOCAL_AMFLAGS = ... -I src/sllq/m4
```

### Makefile.am

```m4
AM_CFLAGS += $(PTHREAD_CFLAGS)
AM_CPPFLAGS += $(PTHREAD_CFLAGS)
AM_CXXFLAGS += $(PTHREAD_CFLAGS)

program_SOURCES += sllq/sllq.c
dist_program_SOURCES += sllq/sllq.h
program_LDADD += $(PTHREAD_LIBS)
```

## Author(s)

Jerry Lundström <jerry@dns-oarc.net>

## Copyright

Copyright (c) 2017, OARC, Inc.
All rights reserved.

This file is part of sllq.

sllq is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

sllq is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with sllq.  If not, see <http://www.gnu.org/licenses/>.
