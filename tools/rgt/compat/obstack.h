/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Object stacks of glibc for the systems which lack them
 *
 * The part of the API of <obstack.h> of glibc that RGT uses, for the
 * systems which lack it, such as macOS. The build of RGT puts this
 * directory in the include path only on such systems.
 *
 * An obstack keeps objects in a list of chunks. The last object can
 * grow while it is not finished. Freeing an object frees all the
 * objects made after it.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_RGT_COMPAT_OBSTACK_H__
#define __TE_RGT_COMPAT_OBSTACK_H__

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A chunk of memory with objects */
struct _obstack_chunk {
    char                  *limit;   /**< The end of the chunk */
    struct _obstack_chunk *prev;    /**< The previous chunk or NULL */
};

/** An obstack */
struct obstack {
    size_t                 chunk_size;  /**< The usual size of a chunk */
    struct _obstack_chunk *chunk;       /**< The current chunk */
    char                  *object_base; /**< The start of the current
                                             object */
    char                  *next_free;   /**< The end of the current
                                             object */
    char                  *chunk_limit; /**< The end of the current
                                             chunk */
};

/**
 * The function to call when no memory is left. It must not return:
 * if it returns, the program aborts. The default prints a message and
 * exits.
 */
extern void (*obstack_alloc_failed_handler)(void);

/**
 * Initialize an obstack. Unlike glibc, it ignores
 * obstack_chunk_alloc and obstack_chunk_free and uses malloc() and
 * free().
 *
 * @param h         The obstack.
 *
 * @return 1.
 */
extern int obstack_init(struct obstack *h);

/**
 * Make an object of @p size bytes.
 *
 * @return The object.
 */
extern void *obstack_alloc(struct obstack *h, size_t size);

/**
 * Make an object which is a copy of @p size bytes at @p address.
 *
 * @return The object.
 */
extern void *obstack_copy(struct obstack *h, const void *address,
                          size_t size);

/**
 * Make an object which is a copy of @p size bytes at @p address and
 * a zero byte.
 *
 * @return The object.
 */
extern void *obstack_copy0(struct obstack *h, const void *address,
                           size_t size);

/** Add @p size bytes at @p data to the current object */
extern void obstack_grow(struct obstack *h, const void *data, size_t size);

/** Add the byte @p c to the current object */
extern void obstack_1grow(struct obstack *h, int c);

/**
 * Add text made by printf() to the current object. No zero byte is
 * added.
 *
 * @return The number of bytes added.
 */
extern int obstack_printf(struct obstack *h, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/** obstack_printf() with a va_list */
extern int obstack_vprintf(struct obstack *h, const char *fmt, va_list ap)
    __attribute__((format(printf, 2, 0)));

/**
 * Finish the current object. The next object starts after it.
 *
 * @return The finished object.
 */
extern void *obstack_finish(struct obstack *h);

/**
 * Free @p obj and all the objects made after it. If @p obj is @c NULL,
 * free all the objects: the obstack must be initialized again before
 * the next use.
 */
extern void obstack_free(struct obstack *h, void *obj);

/** The start of the current object */
static inline void *
obstack_base(struct obstack *h)
{
    return h->object_base;
}

/** The end of the current object */
static inline void *
obstack_next_free(struct obstack *h)
{
    return h->next_free;
}

/** The size of the current object */
static inline size_t
obstack_object_size(struct obstack *h)
{
    return (size_t)(h->next_free - h->object_base);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* !__TE_RGT_COMPAT_OBSTACK_H__ */
