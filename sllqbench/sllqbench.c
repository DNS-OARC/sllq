/*
 * Author Jerry Lundström <jerry@dns-oarc.net>
 * Copyright (c) 2017, OARC, Inc.
 * All rights reserved.
 *
 * This file is part of sllq.
 *
 * sllq is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * sllq is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with sllq.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "sllq.h"

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

void usage(void)
{
    printf(
        "usage: sllqbench [options]\n"
        " -m mode            use mode; mutex, pipe\n"
        " -n num             number of push/shift to do\n"
        " -V                 display version and exit\n"
        " -h                 this\n");
}

struct context {
    pthread_t thr;
    sllq_t*   q;
    size_t    num;
    int       err;
};

void* push(void* vp)
{
    struct context* ctx  = (struct context*)vp;
    struct timespec wait = { 0, 500000 };

    while (ctx->num) {
        if (sllq_mode(ctx->q) == SLLQ_MUTEX && clock_gettime(CLOCK_REALTIME, &wait)) {
            ctx->err = -1;
            return 0;
        }
        wait.tv_sec++;
        ctx->err = SLLQ_EAGAIN;
        while (ctx->err == SLLQ_EAGAIN || ctx->err == SLLQ_FULL)
            ctx->err = sllq_push(ctx->q, (void*)1, &wait);
        if (ctx->err == SLLQ_ETIMEDOUT)
            continue;
        if (ctx->err != SLLQ_OK)
            break;
        ctx->num--;
    }

    return 0;
}

void* shift(void* vp)
{
    struct context* ctx  = (struct context*)vp;
    struct timespec wait = { 0, 500000 };
    void*           data;

    while (ctx->num) {
        if (sllq_mode(ctx->q) == SLLQ_MUTEX && clock_gettime(CLOCK_REALTIME, &wait)) {
            ctx->err = -1;
            return 0;
        }
        wait.tv_sec++;
        ctx->err = SLLQ_EAGAIN;
        while (ctx->err == SLLQ_EAGAIN || ctx->err == SLLQ_EMPTY)
            ctx->err = sllq_shift(ctx->q, &data, &wait);
        if (ctx->err == SLLQ_ETIMEDOUT)
            continue;
        if (ctx->err != SLLQ_OK)
            break;
        ctx->num--;
    }

    return 0;
}

int main(int argc, char** argv)
{
    int             opt, err;
    sllq_t          q    = SLLQ_T_INIT;
    sllq_mode_t     mode = SLLQ_MUTEX;
    struct context  a, b;
    size_t          num = 100;
    struct timespec start, end;
    float           fraction;

    while ((opt = getopt(argc, argv, "m:n:hV")) != -1) {
        switch (opt) {
        case 'm':
            if (!strcmp(optarg, "mutex")) {
                mode = SLLQ_MUTEX;
            } else if (!strcmp(optarg, "pipe")) {
                mode = SLLQ_PIPE;
            } else {
                usage();
                return 1;
            }
            break;
        case 'n':
            num = strtoul(optarg, 0, 10);
            break;
        case 'h':
            usage();
            return 0;
        case 'V':
            printf("sllqbench version %s (sllq version %s)\n", PACKAGE_VERSION, SLLQ_VERSION_STR);
            return 0;
        default:
            usage();
            return 1;
        }
    }

    if ((err = sllq_set_mode(&q, mode))) {
        fprintf(stderr, "sllq_set_mode(): %s\n", sllq_strerror(err));
        return 2;
    }
    if ((err = sllq_set_size(&q, 64))) {
        fprintf(stderr, "sllq_set_size(): %s\n", sllq_strerror(err));
        return 2;
    }
    if ((err = sllq_init(&q))) {
        fprintf(stderr, "sllq_set_size(): %s\n", sllq_strerror(err));
        return 2;
    }
    a.q   = &q;
    a.num = num;
    b.q   = &q;
    b.num = num;

    if (clock_gettime(CLOCK_MONOTONIC, &start)) {
        perror("clock_gettime()");
        return 2;
    }

    if ((err = pthread_create(&(a.thr), 0, push, (void*)&a))) {
        errno = err;
        perror("pthread_create()");
        return 2;
    }
    if ((err = pthread_create(&(b.thr), 0, shift, (void*)&b))) {
        errno = err;
        perror("pthread_create()");
        pthread_cancel(a.thr);
        return 2;
    }
    if ((err = pthread_join(a.thr, 0))) {
        errno = err;
        perror("pthread_join()");
        pthread_cancel(b.thr);
        return 2;
    }
    if (a.err == SLLQ_OK) {
        if ((err = pthread_join(b.thr, 0))) {
            errno = err;
            perror("pthread_join()");
            return 2;
        }
    } else {
        if ((err = pthread_cancel(b.thr))) {
            errno = err;
            perror("pthread_cancel()");
            return 2;
        }
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end)) {
        perror("clock_gettime()");
        return 2;
    }

    printf("push: %d %lu\n", a.err, num - a.num);
    printf("shift: %d %lu\n", b.err, num - b.num);

    if (end.tv_sec == start.tv_sec && end.tv_nsec >= start.tv_nsec) {
        fraction = 1. / (((float)end.tv_nsec - (float)start.tv_nsec) / (float)1000000000);
    } else if (end.tv_sec > start.tv_sec) {
        fraction = 1. / (((float)end.tv_sec - (float)start.tv_sec - 1) + ((float)(1000000000 - start.tv_nsec + end.tv_nsec) / (float)1000000000));
    } else {
        fraction = 0.;
    }
    if (!b.num && fraction) {
        printf("%.0f/sec\n", num * fraction);
    }

    return 0;
}
