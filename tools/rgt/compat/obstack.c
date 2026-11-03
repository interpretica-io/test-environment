/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Object stacks of glibc for the systems which lack them
 *
 * See obstack.h.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "obstack.h"

/** The usual size of a chunk, as in glibc */
#define OBSTACK_CHUNK_SIZE  4064

/** The alignment of objects: malloc() gives the same */
#define OBSTACK_ALIGN       _Alignof(max_align_t)

/** Round @p x up to the alignment of objects */
#define OBSTACK_ROUND(x) \
    (((x) + OBSTACK_ALIGN - 1) & ~((uintptr_t)OBSTACK_ALIGN - 1))

/** The start of the objects in a chunk */
#define OBSTACK_CHUNK_START(c) \
    ((char *)(c) + OBSTACK_ROUND(sizeof(struct _obstack_chunk)))

static void
obstack_default_alloc_failed(void)
{
    fputs("memory exhausted\n", stderr);
    exit(EXIT_FAILURE);
}

void (*obstack_alloc_failed_handler)(void) = obstack_default_alloc_failed;

/* Report that no memory is left. The handler does not return. */
static void
obstack_no_memory(void)
{
    obstack_alloc_failed_handler();
    abort();
}

/* Allocate a chunk for at least @p size bytes of objects */
static struct _obstack_chunk *
obstack_chunk_new(struct obstack *h, size_t size)
{
    size_t total = OBSTACK_ROUND(sizeof(struct _obstack_chunk)) + size;
    struct _obstack_chunk *chunk;

    if (total < size)
        obstack_no_memory();
    if (total < h->chunk_size)
        total = h->chunk_size;
    /* The end is aligned too: an empty object may start there */
    total = OBSTACK_ROUND(total);
    if (total == 0)
        obstack_no_memory();

    chunk = malloc(total);
    if (chunk == NULL)
        obstack_no_memory();

    chunk->limit = (char *)chunk + total;
    return chunk;
}

int
obstack_init(struct obstack *h)
{
    struct _obstack_chunk *chunk;

    h->chunk_size = OBSTACK_CHUNK_SIZE;
    chunk = obstack_chunk_new(h, 0);
    chunk->prev = NULL;

    h->chunk = chunk;
    h->object_base = h->next_free = OBSTACK_CHUNK_START(chunk);
    h->chunk_limit = chunk->limit;
    return 1;
}

/*
 * Move the current object to a new chunk with room for @p length more
 * bytes. Free the old chunk if the object was the only thing in it.
 */
static void
obstack_newchunk(struct obstack *h, size_t length)
{
    struct _obstack_chunk *old = h->chunk;
    size_t obj_size = obstack_object_size(h);
    size_t size = obj_size + length;
    struct _obstack_chunk *chunk;
    char *start;

    if (size < obj_size)
        obstack_no_memory();
    /* Leave room for more growth, as glibc does */
    if (size + (size >> 3) + 100 > size)
        size += (size >> 3) + 100;

    chunk = obstack_chunk_new(h, size);
    start = OBSTACK_CHUNK_START(chunk);
    if (obj_size > 0)
        memcpy(start, h->object_base, obj_size);

    if (old != NULL && h->object_base == OBSTACK_CHUNK_START(old))
    {
        chunk->prev = old->prev;
        free(old);
    }
    else
    {
        chunk->prev = old;
    }

    h->chunk = chunk;
    h->object_base = start;
    h->next_free = start + obj_size;
    h->chunk_limit = chunk->limit;
}

/* Make room for @p size more bytes in the current object */
static inline void
obstack_room(struct obstack *h, size_t size)
{
    if ((size_t)(h->chunk_limit - h->next_free) < size)
        obstack_newchunk(h, size);
}

void
obstack_grow(struct obstack *h, const void *data, size_t size)
{
    obstack_room(h, size);
    if (size > 0)
        memcpy(h->next_free, data, size);
    h->next_free += size;
}

void
obstack_1grow(struct obstack *h, int c)
{
    obstack_room(h, 1);
    *h->next_free++ = (char)c;
}

void *
obstack_finish(struct obstack *h)
{
    char *obj = h->object_base;
    uintptr_t next = OBSTACK_ROUND((uintptr_t)h->next_free);

    if (next > (uintptr_t)h->chunk_limit)
        next = (uintptr_t)h->chunk_limit;

    h->next_free = h->object_base = (char *)next;
    return obj;
}

void *
obstack_alloc(struct obstack *h, size_t size)
{
    obstack_room(h, size);
    h->next_free += size;
    return obstack_finish(h);
}

void *
obstack_copy(struct obstack *h, const void *address, size_t size)
{
    obstack_grow(h, address, size);
    return obstack_finish(h);
}

void *
obstack_copy0(struct obstack *h, const void *address, size_t size)
{
    obstack_grow(h, address, size);
    obstack_1grow(h, '\0');
    return obstack_finish(h);
}

int
obstack_vprintf(struct obstack *h, const char *fmt, va_list ap)
{
    va_list ap2;
    int len;

    va_copy(ap2, ap);
    len = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (len < 0)
        return len;

    /* vsnprintf() needs room for the zero byte, which is not kept */
    obstack_room(h, (size_t)len + 1);
    vsnprintf(h->next_free, (size_t)len + 1, fmt, ap);
    h->next_free += len;
    return len;
}

int
obstack_printf(struct obstack *h, const char *fmt, ...)
{
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = obstack_vprintf(h, fmt, ap);
    va_end(ap);
    return len;
}

void
obstack_free(struct obstack *h, void *obj)
{
    struct _obstack_chunk *chunk = h->chunk;
    char *p = obj;

    /* Free the chunks made after the one with the object */
    while (chunk != NULL &&
           (p == NULL || p < OBSTACK_CHUNK_START(chunk) || p > chunk->limit))
    {
        struct _obstack_chunk *prev = chunk->prev;

        free(chunk);
        chunk = prev;
    }

    h->chunk = chunk;
    if (chunk != NULL)
    {
        h->object_base = h->next_free = p;
        h->chunk_limit = chunk->limit;
    }
    else if (p != NULL)
    {
        /* The object is not in this obstack */
        abort();
    }
    else
    {
        h->object_base = h->next_free = h->chunk_limit = NULL;
    }
}
