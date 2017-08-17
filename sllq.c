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
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>

/*
 * Version
 */

static const char* _version = SLLQ_VERSION_STR;
inline const char* sllq_version_str(void)
{
    return _version;
}

inline int sllq_version_major(void)
{
    return SLLQ_VERSION_MAJOR;
}

inline int sllq_version_minor(void)
{
    return SLLQ_VERSION_MINOR;
}

inline int sllq_version_patch(void)
{
    return SLLQ_VERSION_PATCH;
}

/*
 * New/Free
 */

static sllq_t _sllq_t_defaults = SLLQ_T_INIT;

sllq_t* sllq_new(void)
{
    sllq_t* queue = calloc(1, sizeof(sllq_t));

    if (queue) {
        memcpy(queue, &_sllq_t_defaults, sizeof(sllq_t));
    }

    return queue;
}

void sllq_free(sllq_t* queue)
{
    if (queue) {
        free(queue);
    }
}

/*
 * Get/Set
 */

inline sllq_mode_t sllq_mode(const sllq_t* queue)
{
    sllq_assert(queue);
    return queue->mode;
}

int sllq_set_mode(sllq_t* queue, sllq_mode_t mode)
{
    sllq_assert(queue);
    if (!queue) {
        return SLLQ_EINVAL;
    }

    queue->mode = mode;

    return SLLQ_OK;
}

inline size_t sllq_size(const sllq_t* queue)
{
    sllq_assert(queue);
    return queue->size;
}

int sllq_set_size(sllq_t* queue, size_t size)
{
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

    for (bit = 1, n = 0; n < (sizeof(size) * 8); n++) {
        if (bit == size)
            break;
        if (size & bit)
            return SLLQ_EINVAL;
        bit <<= 1;
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

int sllq_init(sllq_t* queue)
{
    sllq_assert(queue);
    if (!queue) {
        return SLLQ_EINVAL;
    }

    if (queue->mode == SLLQ_MUTEX) {
        size_t       n;
        int          err;
        sllq_item_t* item;

        if (!queue->size) {
            return SLLQ_EINVAL;
        }
        if (queue->item) {
            return SLLQ_EBUSY;
        }

        if (!(item = calloc(queue->size, sizeof(sllq_item_t)))) {
            sllq_destroy(queue);
            return SLLQ_ENOMEM;
        }

        for (n = 0; n < queue->size; n++) {
            if ((err = pthread_mutex_init(&(item[n].mutex), 0))) {
                for (n--; n; n--) {
                    pthread_mutex_destroy(&(item[n].mutex));
                    pthread_cond_destroy(&(item[n].cond));
                }
                sllq_destroy(queue);
                errno = err;
                free(item);
                return SLLQ_ERRNO;
            }
            if ((err = pthread_cond_init(&(item[n].cond), 0))) {
                pthread_mutex_destroy(&(item[n].mutex));
                for (n--; n; n--) {
                    pthread_mutex_destroy(&(item[n].mutex));
                    pthread_cond_destroy(&(item[n].cond));
                }
                sllq_destroy(queue);
                errno = err;
                free(item);
                return SLLQ_ERRNO;
            }
        }

        queue->item  = item;
        queue->read  = 0;
        queue->write = 0;

        return SLLQ_OK;
    } else if (queue->mode == SLLQ_PIPE) {
        int fd[2];
        int flags, pipe_buf, errnum;

        if (pipe(fd)) {
            return SLLQ_ERRNO;
        }

        if ((flags = fcntl(fd[0], F_GETFL)) == -1
            || fcntl(fd[0], F_SETFL, flags | O_NONBLOCK)) {
            errnum = errno;
            close(fd[0]);
            close(fd[1]);
            errno = errnum;
            return SLLQ_ERRNO;
        }

        if ((flags = fcntl(fd[1], F_GETFL)) == -1
            || fcntl(fd[1], F_SETFL, flags | O_NONBLOCK)) {
            errnum = errno;
            close(fd[0]);
            close(fd[1]);
            errno = errnum;
            return SLLQ_ERRNO;
        }

        errno = 0;
        if ((pipe_buf = fpathconf(fd[1], _PC_PIPE_BUF)) < SIZEOF_VOIDP) {
            errnum = errno;
            close(fd[0]);
            close(fd[1]);
            errno = errnum;
            if (errno)
                return SLLQ_ERRNO;
            return SLLQ_EINVAL;
        }

        queue->read_pipe  = fd[0];
        queue->write_pipe = fd[1];

        return SLLQ_OK;
    }

    return SLLQ_EINVAL;
}

int sllq_destroy(sllq_t* queue)
{
    sllq_assert(queue);
    if (!queue) {
        return SLLQ_EINVAL;
    }

    if (queue->mode == SLLQ_MUTEX) {
        int err;

        if (queue->item) {
            size_t n;

            for (n = 0; n < queue->size; n++) {
                if ((err = pthread_mutex_destroy(&(queue->item[n].mutex)))) {
                    errno = err;
                    return SLLQ_ERRNO;
                }
                if ((err = pthread_cond_destroy(&(queue->item[n].cond)))) {
                    errno = err;
                    return SLLQ_ERRNO;
                }
            }
            free(queue->item);
            queue->item = 0;
        }

        return SLLQ_OK;
    } else if (queue->mode == SLLQ_PIPE) {
        if (queue->write_pipe > -1) {
            close(queue->write_pipe);
            queue->write_pipe = -1;
        }
        if (queue->read_pipe > -1) {
            close(queue->read_pipe);
            queue->read_pipe = -1;
        }

        return SLLQ_OK;
    }

    return SLLQ_EINVAL;
}

int sllq_flush(sllq_t* queue, sllq_item_callback_t callback)
{
    sllq_assert(queue);
    if (!queue) {
        return SLLQ_EINVAL;
    }
    sllq_assert(callback);
    if (!callback) {
        return SLLQ_EINVAL;
    }

    if (queue->mode == SLLQ_MUTEX) {
        int err;

        if (queue->item) {
            size_t n;

            for (n = 0; n < queue->size; n++) {
                sllq_item_t* item = &(queue->item[n]);

                if ((err = pthread_mutex_lock(&(item->mutex)))) {
                    errno = err;
                    return SLLQ_ERRNO;
                }

                if (item->have_data) {
                    callback(item->data);
                    item->data      = 0;
                    item->have_data = 0;
                }

                if ((err = pthread_mutex_unlock(&(item->mutex)))) {
                    errno = err;
                    return SLLQ_ERRNO;
                }
            }
        }

        return SLLQ_OK;
    } else if (queue->mode == SLLQ_PIPE) {
        void*   data = 0;
        ssize_t n;

        if (queue->read_pipe > -1) {
            while ((n = read(queue->read_pipe, &data, sizeof(data))) > 0) {
                if (n != sizeof(data)) {
                    close(queue->read_pipe);
                    queue->read_pipe = -1;
                    return SLLQ_ERROR;
                }

                callback(data);
            }

            if (n < 0) {
                switch (errno) {
                case EAGAIN:
#if EAGAIN != EWOULDBLOCK
                case EWOULDBLOCK:
#endif
                    break;

                default:
                    return SLLQ_ERRNO;
                }
            }
        }

        return SLLQ_OK;
    }

    return SLLQ_EINVAL;
}

/*
 * Queue write
 */

int sllq_push(sllq_t* queue, void* data, const struct timespec* timespec)
{
    sllq_assert(queue);
    if (!queue) {
        return SLLQ_EINVAL;
    }
    sllq_assert(data);
    if (!data) {
        return SLLQ_EINVAL;
    }

    if (queue->mode == SLLQ_MUTEX) {
        int          err, ret = SLLQ_FULL;
        sllq_item_t* item;

        sllq_assert(queue->item);
        if (!queue->item) {
            return SLLQ_EINVAL;
        }

        item = &(queue->item[queue->write]);

        if ((err = pthread_mutex_trylock(&(item->mutex)))) {
            if (err == EBUSY)
                return SLLQ_EAGAIN;
            errno = err;
            return SLLQ_ERRNO;
        }

        if (timespec) {
            while (item->have_data) {
                if (item->want_write) {
                    pthread_mutex_unlock(&(item->mutex));
                    return SLLQ_EINVAL;
                }
                if (item->want_read) {
                    if ((err = pthread_cond_signal(&(item->cond)))) {
                        pthread_mutex_unlock(&(item->mutex));
                        errno = err;
                        return SLLQ_ERRNO;
                    }
                }

                item->want_write = 1;
                err              = pthread_cond_timedwait(&(item->cond), &(item->mutex), timespec);
                item->want_write = 0;

                if (err) {
                    pthread_mutex_unlock(&(item->mutex));
                    if (err == ETIMEDOUT) {
                        return SLLQ_ETIMEDOUT;
                    }
                    errno = err;
                    return SLLQ_ERRNO;
                }
            }
        }

        if (!item->have_data) {
            item->data      = data;
            item->have_data = 1;

            queue->write++;
            queue->write &= queue->mask;

            if (item->want_read) {
                /* TODO: How to handle errors? We did a successful push */
                pthread_cond_signal(&(item->cond));
            }
            ret = SLLQ_OK;
        }

        if ((err = pthread_mutex_unlock(&(item->mutex)))) {
            errno = err;
            return SLLQ_ERRNO;
        }

        return ret;
    } else if (queue->mode == SLLQ_PIPE) {
        ssize_t n;

        if (queue->write_pipe < 0) {
            return SLLQ_EINVAL;
        }

        if ((n = write(queue->write_pipe, (void*)&data, sizeof(data))) < 0) {
            struct pollfd pfd;
            int           err, timeout;

            switch (errno) {
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
                if (timespec)
                    break;
                return SLLQ_EAGAIN;

            default:
                return SLLQ_ERRNO;
            }

            pfd.fd      = queue->write_pipe;
            pfd.events  = POLLOUT;
            pfd.revents = 0;

            timeout = timespec->tv_nsec / 1000;
            if (timeout < 1)
                timeout = 1;
            else if (timeout > 999999)
                timeout = 1000000;

            if ((err = poll(&pfd, 1, timeout)) < 0) {
                return SLLQ_ERRNO;
            } else if (!err) {
                return SLLQ_ETIMEDOUT;
            }

            if ((n = write(queue->write_pipe, (void*)&data, sizeof(data))) < 0) {
                switch (errno) {
                case EAGAIN:
#if EAGAIN != EWOULDBLOCK
                case EWOULDBLOCK:
#endif
                    return SLLQ_EAGAIN;

                default:
                    break;
                }
                return SLLQ_ERRNO;
            }
        }
        if (n != sizeof(data)) {
            close(queue->write_pipe);
            queue->write_pipe = -1;
            return SLLQ_ERROR;
        }

        return SLLQ_OK;
    }

    return SLLQ_EINVAL;
}

/*
 * Queue read
 */

int sllq_shift(sllq_t* queue, void** data, const struct timespec* timespec)
{
    sllq_assert(queue);
    if (!queue) {
        return SLLQ_EINVAL;
    }
    sllq_assert(data);
    if (!data) {
        return SLLQ_EINVAL;
    }

    if (queue->mode == SLLQ_MUTEX) {
        int          err, ret = SLLQ_EMPTY;
        sllq_item_t* item;

        sllq_assert(queue->item);
        if (!queue->item) {
            return SLLQ_EINVAL;
        }

        item = &(queue->item[queue->read]);

        if ((err = pthread_mutex_trylock(&(item->mutex)))) {
            if (err == EBUSY)
                return SLLQ_EAGAIN;
            errno = err;
            return SLLQ_ERRNO;
        }

        if (timespec) {
            while (!item->have_data) {
                if (item->want_read) {
                    pthread_mutex_unlock(&(item->mutex));
                    return SLLQ_EINVAL;
                }
                if (item->want_write) {
                    if ((err = pthread_cond_signal(&(item->cond)))) {
                        pthread_mutex_unlock(&(item->mutex));
                        errno = err;
                        return SLLQ_ERRNO;
                    }
                }

                item->want_read = 1;
                err             = pthread_cond_timedwait(&(item->cond), &(item->mutex), timespec);
                item->want_read = 0;

                if (err) {
                    pthread_mutex_unlock(&(item->mutex));
                    if (err == ETIMEDOUT) {
                        return SLLQ_ETIMEDOUT;
                    }
                    errno = err;
                    return SLLQ_ERRNO;
                }
            }
        }

        if (item->have_data) {
            *data           = item->data;
            item->data      = 0;
            item->have_data = 0;

            queue->read++;
            queue->read &= queue->mask;

            if (item->want_write) {
                /* TODO: How to handle errors? We did a successful shift */
                pthread_cond_signal(&(item->cond));
            }

            ret = SLLQ_OK;
        }

        if ((err = pthread_mutex_unlock(&(item->mutex)))) {
            errno = err;
            return SLLQ_ERRNO;
        }

        return ret;
    } else if (queue->mode == SLLQ_PIPE) {
        void*   _data = 0;
        ssize_t n;

        if (queue->read_pipe < 0) {
            return SLLQ_EINVAL;
        }

        if ((n = read(queue->read_pipe, &_data, sizeof(_data))) < 0) {
            struct pollfd pfd;
            int           err, timeout;

            switch (errno) {
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
                if (timespec)
                    break;
                return SLLQ_EAGAIN;

            default:
                return SLLQ_ERRNO;
            }

            pfd.fd      = queue->read_pipe;
            pfd.events  = POLLIN;
            pfd.revents = 0;

            timeout = timespec->tv_nsec / 1000;
            if (timeout < 1)
                timeout = 1;
            else if (timeout > 999999)
                timeout = 1000000;

            if ((err = poll(&pfd, 1, timeout)) < 0) {
                return SLLQ_ERRNO;
            } else if (!err) {
                return SLLQ_ETIMEDOUT;
            }

            if ((n = read(queue->read_pipe, &_data, sizeof(_data))) < 0) {
                switch (errno) {
                case EAGAIN:
#if EAGAIN != EWOULDBLOCK
                case EWOULDBLOCK:
#endif
                    return SLLQ_EAGAIN;

                default:
                    break;
                }
                return SLLQ_ERRNO;
            }
        }
        if (n != sizeof(_data)) {
            close(queue->read_pipe);
            queue->read_pipe = -1;
            return SLLQ_ERROR;
        }

        *data = _data;

        return SLLQ_OK;
    }

    return SLLQ_EINVAL;
}

/*
 * Errors
 */

const char* sllq_strerror(int errnum)
{
    switch (errnum) {
    case SLLQ_OK:
        return 0;
    case SLLQ_ERROR:
        return SLLQ_ERROR_STR;
    case SLLQ_ERRNO:
        return SLLQ_ERRNO_STR;
    case SLLQ_ENOMEM:
        return SLLQ_ENOMEM_STR;
    case SLLQ_EINVAL:
        return SLLQ_EINVAL_STR;
    case SLLQ_ETIMEDOUT:
        return SLLQ_ETIMEDOUT_STR;
    case SLLQ_EBUSY:
        return SLLQ_EBUSY_STR;
    case SLLQ_EAGAIN:
        return SLLQ_EAGAIN_STR;
    case SLLQ_EMPTY:
        return SLLQ_EMPTY_STR;
    case SLLQ_FULL:
        return SLLQ_FULL_STR;
    }
    return "UNKNOWN";
}
