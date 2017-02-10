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

#ifndef __sllq_h
#define __sllq_h

#include <pthread.h>
#if SLLQ_ENABLE_ASSERT
#include <assert.h>
#define sllq_assert(x) assert(x)
#else
#define sllq_assert(x)
#endif

#define SLLQ_VERSION_STR    "1.0.0"
#define SLLQ_VERSION_MAJOR  1
#define SLLQ_VERSION_MINOR  0
#define SLLQ_VERSION_PATCH  0

#define SLLQ_OK             0
#define SLLQ_ERROR          1
#define SLLQ_ERRNO          2
#define SLLQ_ENOMEM         3
#define SLLQ_EINVAL         4
#define SLLQ_ETIMEDOUT      5
#define SLLQ_EBUSY          6

#ifdef __cplusplus
extern "C" {
#endif

const char* sllq_version_str(void);
int sllq_version_major(void);
int sllq_version_minor(void);
int sllq_version_patch(void);

#define SLLQ_ITEM_T_INIT { 0, 0, PTHREAD_MUTEX_INITIALIZER }
typedef struct sllq_item sllq_item_t;
struct sllq_item {
    unsigned short  have_data : 1;

    void*           data;

    pthread_mutex_t mutex;
};

#define SLLQ_T_INIT { 0, 0, 0, 0, 0, PTHREAD_COND_INITIALIZER, 0, 0, PTHREAD_COND_INITIALIZER }
typedef struct sllq sllq_t;
struct sllq {
    sllq_item_t*    item;

    size_t          size;
    size_t          mask;

    size_t          read;
    size_t          readers;
    pthread_cond_t  read_cond;

    size_t          write;
    size_t          writers;
    pthread_cond_t  write_cond;
};

size_t sllq_size(const sllq_t* queue);
int sllq_set_size(sllq_t* queue, size_t size);
int sllq_init(sllq_t* queue);
int sllq_destroy(sllq_t* queue);
int sllq_push(sllq_t* queue, void* data);
int sllq_timed_push(sllq_t* queue, void* data, const struct timespec* abstime);
int sllq_shift(sllq_t* queue, void** data);
int sllq_timed_shift(sllq_t* queue, void** data, const struct timespec* abstime);

#ifdef __cplusplus
}
#endif

#endif /* __sllq_h */
