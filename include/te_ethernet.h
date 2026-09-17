/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief TE Portability
 *
 * Definitions for Ethernet.
 *
 *
 * Copyright (C) 2004-2022 OKTET Labs Ltd. All rights reserved.
 */

#ifndef __TE_ETHERNET_H__
#define __TE_ETHERNET_H__

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_STDINT_H
#include <stdint.h>
#endif
#if HAVE_NET_ETHERNET_H
#include <net/ethernet.h>
#endif
#if HAVE_NET_IF_ETHER_H
#if defined(__NetBSD__) /* FIXME */
#include "te_queue.h"
#include <net/if.h>
#endif
#include <net/if_ether.h>
#endif
#if HAVE_NETINET_ETHER_H
#include <netinet/ether.h>
#endif

#if HAVE_NETINET_IF_ETHER_H
/* Required on OpenBSD */
#if HAVE_NET_IF_ARP_H
#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include <net/if_arp.h>
#endif
#if HAVE_NET_IF_H
#include <net/if.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#include <netinet/if_ether.h>
#endif

#if HAVE_SYS_ETHERNET_H
#include <sys/ethernet.h>
#endif

#ifdef __CYGWIN__
#define ETHER_ADDR_LEN 6
#define ETHERTYPE_IP   0x0800
#define ETHERTYPE_ARP  0x0806
#define ETHER_MIN_LEN  64
#define ETHER_CRC_LEN  4

#define ICMP_ECHOREPLY      0  /* Echo Reply */
#define ICMP_DEST_UNREACH   3  /* Destination Unreachable */
#define ICMP_SOURCE_QUENCH  4  /* Source Quench */
#define ICMP_REDIRECT       5  /* Redirect (change route) */
#define ICMP_ECHO           8  /* Echo Request */
#define ICMP_TIME_EXCEEDED  11 /* Time Exceeded */
#define ICMP_PARAMETERPROB  12 /* Parameter Problem */
#define ICMP_TIMESTAMP      13 /* Timestamp Request */
#define ICMP_TIMESTAMPREPLY 14 /* Timestamp Reply */
#define ICMP_INFO_REQUEST   15 /* Information Request */
#define ICMP_INFO_REPLY     16 /* Information Reply */
#define ICMP_ADDRESS        17 /* Address Mask Request */
#define ICMP_ADDRESSREPLY   18 /* Address Mask Reply */
#endif

#ifndef ETHER_ADDR_LEN
#ifdef ETHERADDRL
#define ETHER_ADDR_LEN  ETHERADDRL
#endif
#endif

#ifndef ETHER_TYPE_LEN
#define ETHER_TYPE_LEN  (2) /* FIXME */
#endif

#ifndef ETHER_HDR_LEN
#define ETHER_HDR_LEN   (2 * ETHER_ADDR_LEN + ETHER_TYPE_LEN) /* FIXME */
#endif

#ifndef ETHER_MIN_LEN
#if defined(ETHERMIN) && defined(ETHERFCSL)
#define ETHER_MIN_LEN   (ETHERMIN + ETHERFCSL)
#endif
#endif

#ifndef ETHER_CRC_LEN
#ifdef ETHERFCSL
#define ETHER_CRC_LEN   ETHERFCSL
#endif
#endif

#ifndef ETHER_DATA_LEN
#define ETHER_DATA_LEN 1500
#endif

/*
 * <linux/if_ether.h> definitions used across TE. Platforms without
 * that header, such as Darwin, Solaris2 and the BSDs, get equivalents
 * here so that the common code needs no conditionals.
 */

#ifndef ETH_ALEN
#define ETH_ALEN        ETHER_ADDR_LEN
#endif

#ifndef ETH_HLEN
#define ETH_HLEN        ETHER_HDR_LEN
#endif

#ifndef ETH_DATA_LEN
#define ETH_DATA_LEN    ETHER_DATA_LEN
#endif

#ifndef ETH_P_ALL
#define ETH_P_ALL       0x0003  /**< Every packet */
#endif
#ifndef ETH_P_IP
#define ETH_P_IP        0x0800  /**< Internet Protocol packet */
#endif
#ifndef ETH_P_ARP
#define ETH_P_ARP       0x0806  /**< Address Resolution packet */
#endif
#ifndef ETH_P_IPV6
#define ETH_P_IPV6      0x86DD  /**< IPv6 over bluebook */
#endif
#ifndef ETH_P_PAUSE
#define ETH_P_PAUSE     0x8808  /**< IEEE Pause frames */
#endif
#ifndef ETH_P_8021Q
#define ETH_P_8021Q     0x8100  /**< 802.1Q VLAN Extended Header */
#endif
#ifndef ETH_P_8021AD
#define ETH_P_8021AD    0x88A8  /**< 802.1ad Service VLAN */
#endif
#ifndef ETH_P_PPP_DISC
#define ETH_P_PPP_DISC  0x8863  /**< PPPoE discovery messages */
#endif
#ifndef ETH_P_PPP_SES
#define ETH_P_PPP_SES   0x8864  /**< PPPoE session messages */
#endif

#ifndef HAVE_LINUX_IF_ETHER_H
/**
 * Ethernet header in the layout used by @c <linux/if_ether.h>.
 *
 * Darwin, Solaris2 and the BSDs name the very same fields
 * differently, so TE defines the Linux layout for them.
 */
struct ethhdr {
    uint8_t     h_dest[ETH_ALEN];   /**< Destination MAC address */
    uint8_t     h_source[ETH_ALEN]; /**< Source MAC address */
    uint16_t    h_proto;            /**< Packet type ID field */
} __attribute__((packed));
#endif

#endif /* !__TE_ETHERNET_H__ */
