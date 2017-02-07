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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include "sllq.h"

#include <stdlib.h>
#include <errno.h>
#include <sched.h>

/*
 * Version
 */

static const char* _version = SLLQ_VERSION_STR;
inline const char* sllq_version_str(void) {
    return _version;
}

inline int sllq_version_major(void) {
    return SLLQ_VERSION_MAJOR;
}

inline int sllq_version_minor(void) {
    return SLLQ_VERSION_MINOR;
}

inline int sllq_version_patch(void) {
    return SLLQ_VERSION_PATCH;
}

/*
 * Get/Set
 */

inline size_t sllq_size(const sllq_t* queue) {
    sllq_assert(queue);
    return queue->size;
}

int sllq_set_size(sllq_t* queue, size_t size) {
    size_t n, bit;

    sllq_assert(queue);
    if (!queue) {
        return SLLQ_EINVAL;
    }
    sllq_assert(size);
    if (!size) {
        return SLLQ_EINVAL;
    }

    if (queue->item) {
        return SLLQ_EBUSY;
    }

    for (bit = 1, n = 0; n < (sizeof(size)*8); n++) {
        if (bit == size)
            break;
        if (size & bit)
            return SLLQ_EINVAL;
        bit << 1;
    }

    queue->size = bit;
    queue->mask = size - 1;
    if (!queue->mask)
        queue->mask = 1;

    return SLLQ_OK;
}

/*
 * Init/Destroy
 */

int sllq_init(sllq_t* queue) {
    size_t n;
    int err;
    sllq_item_t* item;

    sllq_assert(queue);
    if (!queue) {
        return SLLQ_EINVAL;
    }
    if (!queue->size) {
        return SLLQ_EINVAL;
    }

    if (queue->item) {
        return SLLQ_EBUSY;
    }

    if ((err = pthread_cond_init(&(queue->read_cond), 0))) {
        sllq_destroy(queue);
        errno = err;
        return SLLQ_ERRNO;
    }
    if ((err = pthread_cond_init(&(queue->write_cond), 0))) {
        sllq_destroy(queue);
        errno = err;
        return SLLQ_ERRNO;
    }
    if (!(item = calloc(queue->size, sizeof(sllq_item_t)))) {
        sllq_destroy(queue);
        return SLLQ_ENOMEM;
    }

    for (n = 0; n < queue->size; n++) {
        if ((err = pthread_mutex_init(&(item[n].mutex), 0))) {
            for (; n; n--) {
                pthread_mutex_destroy(&(item[n].mutex));
            }
            sllq_destroy(queue);
            errno = err;
            return SLLQ_ERRNO;
        }
    }

    queue->item = item;
    queue->read =
        queue->readers =
        queue->write =
        queue->writers = 0;

    return SLLQ_OK;
}

int sllq_destroy(sllq_t* queue) {
    int err;

    sllq_assert(queue);
    if (!queue) {
        return SLLQ_EINVAL;
    }

    if ((err = pthread_cond_destroy(&(queue->read_cond)))) {
        errno = err;
        return SLLQ_ERRNO;
    }
    if ((err = pthread_cond_destroy(&(queue->write_cond)))) {
        errno = err;
        return SLLQ_ERRNO;
    }

    if (queue->item) {
        size_t n;

        for (n = 0; n < queue->size; n++) {
            if ((err = pthread_mutex_destroy(&(queue->item[n].mutex)))) {
                errno = err;
                return SLLQ_ERRNO;
            }
        }
        free(queue->item);
        queue->item = 0;
    }

    return SLLQ_OK;
}

/*
 * Queue write
 */

int sllq_push(sllq_t* queue, void* data) {
    int err, locked = 0;

    sllq_assert(queue);
    if (!queue) {
        return SLLQ_EINVAL;
    }
    sllq_assert(data);
    if (!data) {
        return SLLQ_EINVAL;
    }
    sllq_assert(queue->item);
    if (!queue->item) {
        return SLLQ_EINVAL;
    }

    while (1) {
        while (!(queue->item[queue->write].have_data)) {
            if ((err = pthread_mutex_lock(&(queue->item[queue->write].mutex)))) {
                errno = err;
                return SLLQ_ERRNO;
            }
            locked = 1;

            if (queue->item[queue->write].have_data)
                break;

            queue->writers++;
            if ((err = pthread_cond_wait(&(queue->write_cond), &(queue->item[queue->write].mutex)))) {
                queue->writers--;
                pthread_mutex_unlock(&(queue->item[queue->write].mutex));
                errno = err;
                return SLLQ_ERRNO;
            }
            queue->writers--;

            if (queue->item[queue->write].have_data)
                break;

            if ((err = pthread_mutex_unlock(&(queue->item[queue->write].mutex)))) {
                errno = err;
                return SLLQ_ERRNO;
            }

            locked = 0;

            sched_yield();
        }

        if (!locked && (err = pthread_mutex_trylock(&(queue->item[queue->write].mutex)))) {
            if (err == EBUSY)
                continue;
            errno = err;
            return SLLQ_ERRNO;
        }

        queue->item[queue->write].data = data;
        queue->item[queue->write].have_data = 1;
        queue->write++;
        queue->write &= queue->mask;

        if (queue->readers) {
            /* TODO: How to handle this? We did a successful read */
            pthread_cond_signal(&(queue->read_cond));
        }

        if ((err = pthread_mutex_unlock(&(queue->item[queue->write].mutex)))) {
            errno = err;
            return SLLQ_ERRNO;
        }
        break;
    }

    return SLLQ_OK;
}

int sllq_timed_push(sllq_t* queue, void* data, const struct timespec* abstime) {
    int err, locked = 0;

    sllq_assert(queue);
    if (!queue) {
        return SLLQ_EINVAL;
    }
    sllq_assert(data);
    if (!data) {
        return SLLQ_EINVAL;
    }
    sllq_assert(abstime);
    if (!abstime) {
        return SLLQ_EINVAL;
    }
    sllq_assert(queue->item);
    if (!queue->item) {
        return SLLQ_EINVAL;
    }

    while (1) {
        while (!(queue->item[queue->write].have_data)) {
            if ((err = pthread_mutex_lock(&(queue->item[queue->write].mutex)))) {
                errno = err;
                return SLLQ_ERRNO;
            }
            locked = 1;

            if (queue->item[queue->write].have_data)
                break;

            queue->writers++;
            if ((err = pthread_cond_timedwait(&(queue->write_cond), &(queue->item[queue->write].mutex), abstime))) {
                queue->writers--;
                pthread_mutex_unlock(&(queue->item[queue->write].mutex));
                if (err == ETIMEDOUT) {
                    return SLLQ_ETIMEDOUT;
                }
                errno = err;
                return SLLQ_ERRNO;
            }
            queue->writers--;

            if (queue->item[queue->write].have_data)
                break;

            if ((err = pthread_mutex_unlock(&(queue->item[queue->write].mutex)))) {
                errno = err;
                return SLLQ_ERRNO;
            }

            locked = 0;

            sched_yield();
        }

        if (!locked && (err = pthread_mutex_trylock(&(queue->item[queue->write].mutex)))) {
            if (err == EBUSY)
                continue;
            errno = err;
            return SLLQ_ERRNO;
        }

        queue->item[queue->write].data = data;
        queue->item[queue->write].have_data = 1;
        queue->write++;
        queue->write &= queue->mask;

        if (queue->readers) {
            /* TODO: How to handle this? We did a successful read */
            pthread_cond_signal(&(queue->read_cond));
        }

        if ((err = pthread_mutex_unlock(&(queue->item[queue->write].mutex)))) {
            errno = err;
            return SLLQ_ERRNO;
        }
        break;
    }

    return SLLQ_OK;
}

/*
 * Queue read
 */

int sllq_shift(sllq_t* queue, void** data) {
    int err, locked = 0;

    sllq_assert(queue);
    if (!queue) {
        return SLLQ_EINVAL;
    }
    sllq_assert(data);
    if (!data) {
        return SLLQ_EINVAL;
    }
    sllq_assert(queue->item);
    if (!queue->item) {
        return SLLQ_EINVAL;
    }

    while (1) {
        while (!(queue->item[queue->read].have_data)) {
            if ((err = pthread_mutex_lock(&(queue->item[queue->read].mutex)))) {
                errno = err;
                return SLLQ_ERRNO;
            }
            locked = 1;

            if (queue->item[queue->read].have_data)
                break;

            queue->readers++;
            if ((err = pthread_cond_wait(&(queue->read_cond), &(queue->item[queue->read].mutex)))) {
                queue->readers--;
                pthread_mutex_unlock(&(queue->item[queue->read].mutex));
                errno = err;
                return SLLQ_ERRNO;
            }
            queue->readers--;

            if (queue->item[queue->read].have_data)
                break;

            if ((err = pthread_mutex_unlock(&(queue->item[queue->read].mutex)))) {
                errno = err;
                return SLLQ_ERRNO;
            }

            locked = 0;

            sched_yield();
        }

        if (!locked && (err = pthread_mutex_trylock(&(queue->item[queue->read].mutex)))) {
            if (err == EBUSY)
                continue;
            errno = err;
            return SLLQ_ERRNO;
        }

        *data = queue->item[queue->read].data;
        queue->item[queue->read].data = 0;
        queue->item[queue->read].have_data = 0;
        queue->read++;
        queue->read &= queue->mask;

        if (queue->writers) {
            /* TODO: How to handle this? We did a successful read */
            pthread_cond_signal(&(queue->write_cond));
        }

        if ((err = pthread_mutex_unlock(&(queue->item[queue->read].mutex)))) {
            errno = err;
            return SLLQ_ERRNO;
        }
        break;
    }

    return SLLQ_OK;
}

int sllq_timed_shift(sllq_t* queue, void** data, const struct timespec* abstime) {
    int err, locked = 0;

    sllq_assert(queue);
    if (!queue) {
        return SLLQ_EINVAL;
    }
    sllq_assert(data);
    if (!data) {
        return SLLQ_EINVAL;
    }
    sllq_assert(abstime);
    if (!abstime) {
        return SLLQ_EINVAL;
    }
    sllq_assert(queue->item);
    if (!queue->item) {
        return SLLQ_EINVAL;
    }

    while (1) {
        while (!(queue->item[queue->read].have_data)) {
            if ((err = pthread_mutex_lock(&(queue->item[queue->read].mutex)))) {
                errno = err;
                return SLLQ_ERRNO;
            }
            locked = 1;

            if (queue->item[queue->read].have_data)
                break;

            queue->readers++;
            if ((err = pthread_cond_timedwait(&(queue->read_cond), &(queue->item[queue->read].mutex), abstime))) {
                queue->readers--;
                pthread_mutex_unlock(&(queue->item[queue->read].mutex));
                if (err == ETIMEDOUT) {
                    return SLLQ_ETIMEDOUT;
                }
                errno = err;
                return SLLQ_ERRNO;
            }
            queue->readers--;

            if (queue->item[queue->read].have_data)
                break;

            if ((err = pthread_mutex_unlock(&(queue->item[queue->read].mutex)))) {
                errno = err;
                return SLLQ_ERRNO;
            }

            locked = 0;

            sched_yield();
        }

        if (!locked && (err = pthread_mutex_trylock(&(queue->item[queue->read].mutex)))) {
            if (err == EBUSY)
                continue;
            errno = err;
            return SLLQ_ERRNO;
        }

        *data = queue->item[queue->read].data;
        queue->item[queue->read].data = 0;
        queue->item[queue->read].have_data = 0;
        queue->read++;
        queue->read &= queue->mask;

        if (queue->writers) {
            /* TODO: How to handle this? We did a successful read */
            pthread_cond_signal(&(queue->write_cond));
        }

        if ((err = pthread_mutex_unlock(&(queue->item[queue->read].mutex)))) {
            errno = err;
            return SLLQ_ERRNO;
        }
        break;
    }

    return SLLQ_OK;
}
