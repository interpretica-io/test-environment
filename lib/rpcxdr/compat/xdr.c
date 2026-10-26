/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief XDR for hosts without an RPC library
 *
 * Implementation of the memory stream and of the filters declared in
 * the compat rpc/xdr.h. Integers narrower than 32 bits take a whole
 * XDR unit, 64-bit ones take two with the most significant first, as
 * RFC 4506 and the glibc and libtirpc implementations do.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <rpc/types.h>
#include <rpc/xdr.h>

/** Zero bytes to pad opaque data up to a whole XDR unit */
static const char xdr_zero[BYTES_PER_XDR_UNIT] = { 0, 0, 0, 0 };

/* Memory stream */

static bool_t
xdrmem_getint32(XDR *xdrs, int32_t *ip)
{
    uint32_t v;

    if (xdrs->x_handy < sizeof(v))
        return FALSE;

    memcpy(&v, xdrs->x_private, sizeof(v));
    *ip = (int32_t)te_xdr_swap32(v);
    xdrs->x_private += sizeof(v);
    xdrs->x_handy -= sizeof(v);
    return TRUE;
}

static bool_t
xdrmem_putint32(XDR *xdrs, const int32_t *ip)
{
    uint32_t v;

    if (xdrs->x_handy < sizeof(v))
        return FALSE;

    v = te_xdr_swap32((uint32_t)*ip);
    memcpy(xdrs->x_private, &v, sizeof(v));
    xdrs->x_private += sizeof(v);
    xdrs->x_handy -= sizeof(v);
    return TRUE;
}

static bool_t
xdrmem_getlong(XDR *xdrs, long *lp)
{
    int32_t v;

    if (!xdrmem_getint32(xdrs, &v))
        return FALSE;

    *lp = v;
    return TRUE;
}

static bool_t
xdrmem_putlong(XDR *xdrs, const long *lp)
{
    int32_t v = (int32_t)*lp;

    return xdrmem_putint32(xdrs, &v);
}

static bool_t
xdrmem_getbytes(XDR *xdrs, caddr_t addr, u_int len)
{
    if (xdrs->x_handy < len)
        return FALSE;

    memcpy(addr, xdrs->x_private, len);
    xdrs->x_private += len;
    xdrs->x_handy -= len;
    return TRUE;
}

static bool_t
xdrmem_putbytes(XDR *xdrs, const char *addr, u_int len)
{
    if (xdrs->x_handy < len)
        return FALSE;

    memcpy(xdrs->x_private, addr, len);
    xdrs->x_private += len;
    xdrs->x_handy -= len;
    return TRUE;
}

static u_int
xdrmem_getpos(const XDR *xdrs)
{
    return (u_int)(xdrs->x_private - xdrs->x_base);
}

static bool_t
xdrmem_setpos(XDR *xdrs, u_int pos)
{
    size_t total = (size_t)(xdrs->x_private - xdrs->x_base) +
                   xdrs->x_handy;

    if (pos > total)
        return FALSE;

    xdrs->x_private = xdrs->x_base + pos;
    xdrs->x_handy = (u_int)(total - pos);
    return TRUE;
}

static int32_t *
xdrmem_inline(XDR *xdrs, u_int len)
{
    int32_t *buf;

    /* The caller reads the buffer as int32_t, so it must be aligned */
    if (xdrs->x_handy < len ||
        ((uintptr_t)xdrs->x_private % sizeof(int32_t)) != 0)
        return NULL;

    buf = (int32_t *)(void *)xdrs->x_private;
    xdrs->x_private += len;
    xdrs->x_handy -= len;
    return buf;
}

static void
xdrmem_destroy(XDR *xdrs)
{
    (void)xdrs;
}

static const struct xdr_ops xdrmem_ops = {
    .x_getlong = xdrmem_getlong,
    .x_putlong = xdrmem_putlong,
    .x_getbytes = xdrmem_getbytes,
    .x_putbytes = xdrmem_putbytes,
    .x_getpostn = xdrmem_getpos,
    .x_setpostn = xdrmem_setpos,
    .x_inline = xdrmem_inline,
    .x_destroy = xdrmem_destroy,
    .x_getint32 = xdrmem_getint32,
    .x_putint32 = xdrmem_putint32,
};

/* See the description in rpc/xdr.h */
void
xdrmem_create(XDR *xdrs, caddr_t addr, u_int size, enum xdr_op op)
{
    xdrs->x_op = op;
    xdrs->x_ops = &xdrmem_ops;
    xdrs->x_public = NULL;
    xdrs->x_private = addr;
    xdrs->x_base = addr;
    xdrs->x_handy = size;
}

/* Integers */

/** Encode or decode a signed 32-bit value */
static bool_t
xdr_i32(XDR *xdrs, int32_t *ip)
{
    switch (xdrs->x_op)
    {
        case XDR_ENCODE:
            return XDR_PUTINT32(xdrs, ip);

        case XDR_DECODE:
            return XDR_GETINT32(xdrs, ip);

        case XDR_FREE:
            return TRUE;
    }
    return FALSE;
}

/** Encode or decode an unsigned 64-bit value as two XDR units */
static bool_t
xdr_u64(XDR *xdrs, uint64_t *up)
{
    int32_t hi;
    int32_t lo;

    switch (xdrs->x_op)
    {
        case XDR_ENCODE:
            hi = (int32_t)(uint32_t)(*up >> 32);
            lo = (int32_t)(uint32_t)*up;
            return XDR_PUTINT32(xdrs, &hi) && XDR_PUTINT32(xdrs, &lo);

        case XDR_DECODE:
            if (!XDR_GETINT32(xdrs, &hi) || !XDR_GETINT32(xdrs, &lo))
                return FALSE;
            *up = ((uint64_t)(uint32_t)hi << 32) | (uint32_t)lo;
            return TRUE;

        case XDR_FREE:
            return TRUE;
    }
    return FALSE;
}

/*
 * Narrow integers go through a 32-bit temporary. The decoded value
 * is truncated to the type, as the other implementations do.
 */
#define XDR_NARROW(_name, _type) \
    bool_t                                                  \
    _name(XDR *xdrs, _type *p)                              \
    {                                                       \
        int32_t v;                                          \
                                                            \
        switch (xdrs->x_op)                                 \
        {                                                   \
            case XDR_ENCODE:                                \
                v = (int32_t)*p;                            \
                return XDR_PUTINT32(xdrs, &v);              \
                                                            \
            case XDR_DECODE:                                \
                if (!XDR_GETINT32(xdrs, &v))                \
                    return FALSE;                           \
                *p = (_type)v;                              \
                return TRUE;                                \
                                                            \
            case XDR_FREE:                                  \
                return TRUE;                                \
        }                                                   \
        return FALSE;                                       \
    }

XDR_NARROW(xdr_short, short)
XDR_NARROW(xdr_u_short, u_short)
XDR_NARROW(xdr_int, int)
XDR_NARROW(xdr_u_int, u_int)
XDR_NARROW(xdr_long, long)
XDR_NARROW(xdr_u_long, u_long)
XDR_NARROW(xdr_char, char)
XDR_NARROW(xdr_u_char, u_char)
XDR_NARROW(xdr_int8_t, int8_t)
XDR_NARROW(xdr_uint8_t, uint8_t)
XDR_NARROW(xdr_u_int8_t, uint8_t)
XDR_NARROW(xdr_int16_t, int16_t)
XDR_NARROW(xdr_uint16_t, uint16_t)
XDR_NARROW(xdr_u_int16_t, uint16_t)

#undef XDR_NARROW

/* See the description in rpc/xdr.h */
bool_t
xdr_void(void)
{
    return TRUE;
}

/* See the description in rpc/xdr.h */
bool_t
xdr_int32_t(XDR *xdrs, int32_t *ip)
{
    return xdr_i32(xdrs, ip);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_uint32_t(XDR *xdrs, uint32_t *up)
{
    return xdr_i32(xdrs, (int32_t *)up);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_u_int32_t(XDR *xdrs, uint32_t *up)
{
    return xdr_i32(xdrs, (int32_t *)up);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_uint64_t(XDR *xdrs, uint64_t *up)
{
    return xdr_u64(xdrs, up);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_u_int64_t(XDR *xdrs, uint64_t *up)
{
    return xdr_u64(xdrs, up);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_int64_t(XDR *xdrs, int64_t *ip)
{
    return xdr_u64(xdrs, (uint64_t *)ip);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_hyper(XDR *xdrs, quad_t *llp)
{
    return xdr_u64(xdrs, (uint64_t *)llp);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_u_hyper(XDR *xdrs, u_quad_t *ullp)
{
    return xdr_u64(xdrs, ullp);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_longlong_t(XDR *xdrs, quad_t *llp)
{
    return xdr_u64(xdrs, (uint64_t *)llp);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_u_longlong_t(XDR *xdrs, u_quad_t *ullp)
{
    return xdr_u64(xdrs, ullp);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_quad_t(XDR *xdrs, quad_t *ip)
{
    return xdr_u64(xdrs, (uint64_t *)ip);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_u_quad_t(XDR *xdrs, u_quad_t *up)
{
    return xdr_u64(xdrs, up);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_bool(XDR *xdrs, bool_t *bp)
{
    int32_t v;

    switch (xdrs->x_op)
    {
        case XDR_ENCODE:
            v = *bp ? TRUE : FALSE;
            return XDR_PUTINT32(xdrs, &v);

        case XDR_DECODE:
            if (!XDR_GETINT32(xdrs, &v))
                return FALSE;
            *bp = (v != 0) ? TRUE : FALSE;
            return TRUE;

        case XDR_FREE:
            return TRUE;
    }
    return FALSE;
}

/* See the description in rpc/xdr.h */
bool_t
xdr_enum(XDR *xdrs, enum_t *ep)
{
    return xdr_i32(xdrs, (int32_t *)ep);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_float(XDR *xdrs, float *fp)
{
    int32_t v;

    switch (xdrs->x_op)
    {
        case XDR_ENCODE:
            memcpy(&v, fp, sizeof(v));
            return XDR_PUTINT32(xdrs, &v);

        case XDR_DECODE:
            if (!XDR_GETINT32(xdrs, &v))
                return FALSE;
            memcpy(fp, &v, sizeof(v));
            return TRUE;

        case XDR_FREE:
            return TRUE;
    }
    return FALSE;
}

/* See the description in rpc/xdr.h */
bool_t
xdr_double(XDR *xdrs, double *dp)
{
    uint64_t v;

    if (xdrs->x_op == XDR_ENCODE)
        memcpy(&v, dp, sizeof(v));

    if (!xdr_u64(xdrs, &v))
        return FALSE;

    if (xdrs->x_op == XDR_DECODE)
        memcpy(dp, &v, sizeof(v));
    return TRUE;
}

/* Opaque data and strings */

/* See the description in rpc/xdr.h */
bool_t
xdr_opaque(XDR *xdrs, caddr_t cp, u_int cnt)
{
    char pad[BYTES_PER_XDR_UNIT];
    u_int rndup;

    if (cnt == 0)
        return TRUE;

    rndup = cnt % BYTES_PER_XDR_UNIT;
    if (rndup != 0)
        rndup = BYTES_PER_XDR_UNIT - rndup;

    switch (xdrs->x_op)
    {
        case XDR_ENCODE:
            if (!XDR_PUTBYTES(xdrs, cp, cnt))
                return FALSE;
            return rndup == 0 || XDR_PUTBYTES(xdrs, xdr_zero, rndup);

        case XDR_DECODE:
            if (!XDR_GETBYTES(xdrs, cp, cnt))
                return FALSE;
            return rndup == 0 || XDR_GETBYTES(xdrs, pad, rndup);

        case XDR_FREE:
            return TRUE;
    }
    return FALSE;
}

/* See the description in rpc/xdr.h */
bool_t
xdr_bytes(XDR *xdrs, char **cpp, u_int *sizep, u_int maxsize)
{
    char *sp = *cpp;
    u_int nodesize;
    bool_t rc;

    if (!xdr_u_int(xdrs, sizep))
        return FALSE;

    nodesize = *sizep;
    if (nodesize > maxsize && xdrs->x_op != XDR_FREE)
        return FALSE;

    switch (xdrs->x_op)
    {
        case XDR_DECODE:
            if (nodesize == 0)
                return TRUE;
            if (sp == NULL)
            {
                sp = mem_alloc(nodesize);
                if (sp == NULL)
                    return FALSE;
                *cpp = sp;
            }
            rc = xdr_opaque(xdrs, sp, nodesize);
            return rc;

        case XDR_ENCODE:
            return xdr_opaque(xdrs, sp, nodesize);

        case XDR_FREE:
            if (sp != NULL)
            {
                mem_free(sp, nodesize);
                *cpp = NULL;
            }
            return TRUE;
    }
    return FALSE;
}

/* See the description in rpc/xdr.h */
bool_t
xdr_netobj(XDR *xdrs, struct netobj *np)
{
    return xdr_bytes(xdrs, &np->n_bytes, &np->n_len, MAX_NETOBJ_SZ);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_string(XDR *xdrs, char **cpp, u_int maxsize)
{
    char *sp = *cpp;
    u_int size = 0;

    switch (xdrs->x_op)
    {
        case XDR_FREE:
            if (sp == NULL)
                return TRUE;
            break;

        case XDR_ENCODE:
            if (sp == NULL)
                return FALSE;
            size = (u_int)strlen(sp);
            break;

        case XDR_DECODE:
            break;
    }

    if (xdrs->x_op != XDR_FREE)
    {
        if (!xdr_u_int(xdrs, &size))
            return FALSE;
        if (size > maxsize)
            return FALSE;
    }

    switch (xdrs->x_op)
    {
        case XDR_DECODE:
            /* The terminating NUL needs one more byte */
            if (size == LASTUNSIGNED)
                return FALSE;
            if (sp == NULL)
            {
                sp = mem_alloc(size + 1);
                if (sp == NULL)
                    return FALSE;
                *cpp = sp;
            }
            sp[size] = '\0';
            return xdr_opaque(xdrs, sp, size);

        case XDR_ENCODE:
            return xdr_opaque(xdrs, sp, size);

        case XDR_FREE:
            mem_free(sp, strlen(sp) + 1);
            *cpp = NULL;
            return TRUE;
    }
    return FALSE;
}

/* See the description in rpc/xdr.h */
bool_t
xdr_wrapstring(XDR *xdrs, char **cpp)
{
    return xdr_string(xdrs, cpp, LASTUNSIGNED);
}

/* Compound data */

/* See the description in rpc/xdr.h */
bool_t
xdr_union(XDR *xdrs, enum_t *dscmp, char *unp,
          const struct xdr_discrim *choices, xdrproc_t dfault)
{
    if (!xdr_enum(xdrs, dscmp))
        return FALSE;

    for (; choices->proc != NULL; choices++)
    {
        if (choices->value == *dscmp)
            return (*choices->proc)(xdrs, unp, LASTUNSIGNED);
    }

    return dfault == NULL ? FALSE : (*dfault)(xdrs, unp, LASTUNSIGNED);
}

/* See the description in rpc/xdr.h */
bool_t
xdr_array(XDR *xdrs, caddr_t *addrp, u_int *sizep, u_int maxsize,
          u_int elsize, xdrproc_t elproc)
{
    caddr_t target = *addrp;
    u_int c;
    u_int i;
    bool_t stat = TRUE;

    if (!xdr_u_int(xdrs, sizep))
        return FALSE;

    c = *sizep;
    if ((c > maxsize || (elsize != 0 && c > LASTUNSIGNED / elsize)) &&
        xdrs->x_op != XDR_FREE)
        return FALSE;

    if (target == NULL)
    {
        switch (xdrs->x_op)
        {
            case XDR_DECODE:
                if (c == 0)
                    return TRUE;
                target = mem_alloc((size_t)c * elsize);
                if (target == NULL)
                    return FALSE;
                *addrp = target;
                break;

            case XDR_FREE:
                return TRUE;

            case XDR_ENCODE:
                /* Nothing to encode is fine, a missing array is not */
                return c == 0;
        }
    }

    for (i = 0; i < c && stat; i++)
    {
        stat = (*elproc)(xdrs, target, LASTUNSIGNED);
        target += elsize;
    }

    if (xdrs->x_op == XDR_FREE)
    {
        mem_free(*addrp, (size_t)c * elsize);
        *addrp = NULL;
    }
    return stat;
}

/* See the description in rpc/xdr.h */
bool_t
xdr_vector(XDR *xdrs, char *basep, u_int nelem, u_int elemsize,
           xdrproc_t xdr_elem)
{
    char *elptr = basep;
    u_int i;

    for (i = 0; i < nelem; i++)
    {
        if (!(*xdr_elem)(xdrs, elptr, LASTUNSIGNED))
            return FALSE;
        elptr += elemsize;
    }
    return TRUE;
}

/* See the description in rpc/xdr.h */
bool_t
xdr_reference(XDR *xdrs, caddr_t *pp, u_int size, xdrproc_t proc)
{
    caddr_t loc = *pp;
    bool_t stat;

    if (loc == NULL)
    {
        switch (xdrs->x_op)
        {
            case XDR_FREE:
                return TRUE;

            case XDR_DECODE:
                loc = mem_alloc(size);
                if (loc == NULL)
                    return FALSE;
                *pp = loc;
                break;

            case XDR_ENCODE:
                return FALSE;
        }
    }

    stat = (*proc)(xdrs, loc, LASTUNSIGNED);

    if (xdrs->x_op == XDR_FREE)
    {
        mem_free(loc, size);
        *pp = NULL;
    }
    return stat;
}

/* See the description in rpc/xdr.h */
bool_t
xdr_pointer(XDR *xdrs, char **objpp, u_int obj_size, xdrproc_t xdr_obj)
{
    bool_t more_data = (*objpp != NULL);

    if (!xdr_bool(xdrs, &more_data))
        return FALSE;

    if (!more_data)
    {
        *objpp = NULL;
        return TRUE;
    }
    return xdr_reference(xdrs, objpp, obj_size, xdr_obj);
}

/* See the description in rpc/xdr.h */
void
xdr_free(xdrproc_t proc, char *objp)
{
    XDR x;

    x.x_op = XDR_FREE;
    x.x_ops = NULL;
    (*proc)(&x, objp, LASTUNSIGNED);
}
