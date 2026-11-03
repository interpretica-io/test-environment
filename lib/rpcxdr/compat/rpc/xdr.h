/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief XDR for hosts without an RPC library
 *
 * Memory streams and the primitive filters which rpcgen output and RCF
 * RPC use. The wire format is the one of RFC 4506, so the data is
 * exchanged with glibc and libtirpc peers as is.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_RPC_COMPAT_XDR_H__
#define __TE_RPC_COMPAT_XDR_H__

#include <stdint.h>
#include <rpc/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Direction of an XDR operation */
enum xdr_op {
    XDR_ENCODE = 0,
    XDR_DECODE = 1,
    XDR_FREE = 2,
};

/** Size of the basic unit of the encoded data */
#define BYTES_PER_XDR_UNIT 4

/** Round a size up to a whole number of XDR units */
#define RNDUP(_x) \
    ((((_x) + BYTES_PER_XDR_UNIT - 1) / BYTES_PER_XDR_UNIT) * \
     BYTES_PER_XDR_UNIT)

/** Unlimited size of a variable-length item */
#define LASTUNSIGNED ((u_int)0 - 1)

typedef struct XDR XDR;

/** XDR stream handle */
struct XDR {
    enum xdr_op x_op;               /**< Operation */
    const struct xdr_ops {
        bool_t (*x_getlong)(XDR *xdrs, long *lp);
        bool_t (*x_putlong)(XDR *xdrs, const long *lp);
        bool_t (*x_getbytes)(XDR *xdrs, caddr_t addr, u_int len);
        bool_t (*x_putbytes)(XDR *xdrs, const char *addr, u_int len);
        u_int (*x_getpostn)(const XDR *xdrs);
        bool_t (*x_setpostn)(XDR *xdrs, u_int pos);
        int32_t *(*x_inline)(XDR *xdrs, u_int len);
        void (*x_destroy)(XDR *xdrs);
        bool_t (*x_getint32)(XDR *xdrs, int32_t *ip);
        bool_t (*x_putint32)(XDR *xdrs, const int32_t *ip);
    } *x_ops;                       /**< Stream operations */
    caddr_t x_public;               /**< Data of the stream user */
    caddr_t x_private;              /**< Current position in the stream */
    caddr_t x_base;                 /**< Start of the stream */
    u_int x_handy;                  /**< Bytes left in the stream */
};

/**
 * XDR filter. The third argument, if any, is the maximum size of
 * the item.
 */
typedef bool_t (*xdrproc_t)(XDR *xdrs, void *objp, ...);

#define XDR_GETLONG(_xdrs, _lp)     (*(_xdrs)->x_ops->x_getlong)(_xdrs, _lp)
#define XDR_PUTLONG(_xdrs, _lp)     (*(_xdrs)->x_ops->x_putlong)(_xdrs, _lp)
#define XDR_GETINT32(_xdrs, _ip)    (*(_xdrs)->x_ops->x_getint32)(_xdrs, _ip)
#define XDR_PUTINT32(_xdrs, _ip)    (*(_xdrs)->x_ops->x_putint32)(_xdrs, _ip)
#define XDR_GETBYTES(_xdrs, _addr, _len) \
    (*(_xdrs)->x_ops->x_getbytes)(_xdrs, _addr, _len)
#define XDR_PUTBYTES(_xdrs, _addr, _len) \
    (*(_xdrs)->x_ops->x_putbytes)(_xdrs, _addr, _len)
#define XDR_GETPOS(_xdrs)           (*(_xdrs)->x_ops->x_getpostn)(_xdrs)
#define XDR_SETPOS(_xdrs, _pos)     (*(_xdrs)->x_ops->x_setpostn)(_xdrs, _pos)
#define XDR_INLINE(_xdrs, _len)     (*(_xdrs)->x_ops->x_inline)(_xdrs, _len)
#define XDR_DESTROY(_xdrs) \
    do {                                                    \
        if ((_xdrs)->x_ops->x_destroy != NULL)              \
            (*(_xdrs)->x_ops->x_destroy)(_xdrs);            \
    } while (0)

#define xdr_getlong     XDR_GETLONG
#define xdr_putlong     XDR_PUTLONG
#define xdr_getbytes    XDR_GETBYTES
#define xdr_putbytes    XDR_PUTBYTES
#define xdr_getpos      XDR_GETPOS
#define xdr_setpos      XDR_SETPOS
#define xdr_inline      XDR_INLINE
#define xdr_destroy     XDR_DESTROY

/** Convert a 32-bit value between the host and the XDR byte order */
static inline uint32_t
te_xdr_swap32(uint32_t v)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(v);
#else
    return v;
#endif
}

/*
 * Access to the inline buffer which XDR_INLINE() returns. The buffer
 * pointer is advanced past the item.
 */
#define IXDR_GET_INT32(_buf)    ((int32_t)te_xdr_swap32((uint32_t)*(_buf)++))
#define IXDR_PUT_INT32(_buf, _v) \
    (*(_buf)++ = (int32_t)te_xdr_swap32((uint32_t)(_v)))
#define IXDR_GET_U_INT32(_buf)  ((uint32_t)IXDR_GET_INT32(_buf))
#define IXDR_PUT_U_INT32(_buf, _v) IXDR_PUT_INT32(_buf, (int32_t)(_v))

#define IXDR_GET_LONG(_buf)     ((long)IXDR_GET_INT32(_buf))
#define IXDR_PUT_LONG(_buf, _v) IXDR_PUT_INT32(_buf, (int32_t)(_v))
#define IXDR_GET_U_LONG(_buf)   ((u_long)IXDR_GET_U_INT32(_buf))
#define IXDR_PUT_U_LONG(_buf, _v) IXDR_PUT_U_INT32(_buf, _v)

#define IXDR_GET_BOOL(_buf)     ((bool_t)IXDR_GET_LONG(_buf))
#define IXDR_PUT_BOOL(_buf, _v) IXDR_PUT_LONG(_buf, _v)
#define IXDR_GET_ENUM(_buf, _t) ((_t)IXDR_GET_LONG(_buf))
#define IXDR_PUT_ENUM(_buf, _v) IXDR_PUT_LONG(_buf, _v)
#define IXDR_GET_SHORT(_buf)    ((short)IXDR_GET_LONG(_buf))
#define IXDR_PUT_SHORT(_buf, _v) IXDR_PUT_LONG(_buf, _v)
#define IXDR_GET_U_SHORT(_buf)  ((u_short)IXDR_GET_LONG(_buf))
#define IXDR_PUT_U_SHORT(_buf, _v) IXDR_PUT_LONG(_buf, _v)

/** Discriminated union arm, see xdr_union() */
struct xdr_discrim {
    int value;
    xdrproc_t proc;
};

/** Opaque object of variable length */
#define MAX_NETOBJ_SZ 1024
struct netobj {
    u_int n_len;
    char *n_bytes;
};
typedef struct netobj netobj;

extern bool_t xdr_void(void);
extern bool_t xdr_short(XDR *xdrs, short *sp);
extern bool_t xdr_u_short(XDR *xdrs, u_short *usp);
extern bool_t xdr_int(XDR *xdrs, int *ip);
extern bool_t xdr_u_int(XDR *xdrs, u_int *up);
extern bool_t xdr_long(XDR *xdrs, long *lp);
extern bool_t xdr_u_long(XDR *xdrs, u_long *ulp);
extern bool_t xdr_hyper(XDR *xdrs, quad_t *llp);
extern bool_t xdr_u_hyper(XDR *xdrs, u_quad_t *ullp);
extern bool_t xdr_longlong_t(XDR *xdrs, quad_t *llp);
extern bool_t xdr_u_longlong_t(XDR *xdrs, u_quad_t *ullp);
extern bool_t xdr_int8_t(XDR *xdrs, int8_t *ip);
extern bool_t xdr_uint8_t(XDR *xdrs, uint8_t *up);
extern bool_t xdr_u_int8_t(XDR *xdrs, uint8_t *up);
extern bool_t xdr_int16_t(XDR *xdrs, int16_t *ip);
extern bool_t xdr_uint16_t(XDR *xdrs, uint16_t *up);
extern bool_t xdr_u_int16_t(XDR *xdrs, uint16_t *up);
extern bool_t xdr_int32_t(XDR *xdrs, int32_t *ip);
extern bool_t xdr_uint32_t(XDR *xdrs, uint32_t *up);
extern bool_t xdr_u_int32_t(XDR *xdrs, uint32_t *up);
extern bool_t xdr_int64_t(XDR *xdrs, int64_t *ip);
extern bool_t xdr_uint64_t(XDR *xdrs, uint64_t *up);
extern bool_t xdr_u_int64_t(XDR *xdrs, uint64_t *up);
extern bool_t xdr_quad_t(XDR *xdrs, quad_t *ip);
extern bool_t xdr_u_quad_t(XDR *xdrs, u_quad_t *up);
extern bool_t xdr_bool(XDR *xdrs, bool_t *bp);
extern bool_t xdr_enum(XDR *xdrs, enum_t *ep);
extern bool_t xdr_char(XDR *xdrs, char *cp);
extern bool_t xdr_u_char(XDR *xdrs, u_char *ucp);
extern bool_t xdr_float(XDR *xdrs, float *fp);
extern bool_t xdr_double(XDR *xdrs, double *dp);
extern bool_t xdr_opaque(XDR *xdrs, caddr_t cp, u_int cnt);
extern bool_t xdr_bytes(XDR *xdrs, char **cpp, u_int *sizep,
                        u_int maxsize);
extern bool_t xdr_netobj(XDR *xdrs, struct netobj *np);
extern bool_t xdr_string(XDR *xdrs, char **cpp, u_int maxsize);
extern bool_t xdr_wrapstring(XDR *xdrs, char **cpp);
extern bool_t xdr_union(XDR *xdrs, enum_t *dscmp, char *unp,
                        const struct xdr_discrim *choices,
                        xdrproc_t dfault);
extern bool_t xdr_array(XDR *xdrs, caddr_t *addrp, u_int *sizep,
                        u_int maxsize, u_int elsize, xdrproc_t elproc);
extern bool_t xdr_vector(XDR *xdrs, char *basep, u_int nelem,
                         u_int elemsize, xdrproc_t xdr_elem);
extern bool_t xdr_reference(XDR *xdrs, caddr_t *pp, u_int size,
                            xdrproc_t proc);
extern bool_t xdr_pointer(XDR *xdrs, char **objpp, u_int obj_size,
                          xdrproc_t xdr_obj);

/** Release the memory a filter has allocated while decoding */
extern void xdr_free(xdrproc_t proc, char *objp);

/** Make an XDR stream over a memory buffer */
extern void xdrmem_create(XDR *xdrs, caddr_t addr, u_int size,
                          enum xdr_op op);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TE_RPC_COMPAT_XDR_H__ */
