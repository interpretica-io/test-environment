/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Windows Test Agent
 *
 * Basic configuration tree of the Windows Test Agent on top of the IP
 * Helper API: interfaces, their IPv4 and IPv6 addresses, neighbours,
 * routes, environment and system information.
 *
 * An interface is named by its NDIS name, such as "ethernet_32769",
 * which if_nametoindex() understands as well. The alias the user sees,
 * such as "Ethernet 2", may contain spaces, and an instance name may
 * not.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#define TE_LGR_USER     "Windows Conf"

#include "te_config.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <sys/utsname.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"
#include "te_sockaddr.h"
#include "te_ethernet.h"
#include "logger_api.h"
#include "comm_agent.h"
#include "rcf_ch_api.h"
#include "rcf_pch.h"
#include "rcf_pch_ta_cfg.h"
#include "cs_common.h"
#include "agentlib.h"

#include "win32_internal.h"

/** Convert a Windows error code to a TE one */
#define WIN_RC(_err) \
    TE_OS_RC(TE_TA_WIN32, te_win32_err2errno(_err))

/* Interfaces */

/**
 * Find an interface by its name.
 *
 * @param ifname    NDIS interface name
 * @param luid      location for the interface LUID
 *
 * @return Status code.
 */
static te_errno
if_luid(const char *ifname, NET_LUID *luid)
{
    if (ConvertInterfaceNameToLuidA(ifname, luid) != NO_ERROR)
        return TE_RC(TE_TA_WIN32, TE_ENODEV);
    return 0;
}

/**
 * Get the interface row of MIB.
 *
 * @param ifname    NDIS interface name
 * @param row       location for the row
 *
 * @return Status code.
 */
static te_errno
if_row(const char *ifname, MIB_IF_ROW2 *row)
{
    te_errno rc;
    DWORD err;

    memset(row, 0, sizeof(*row));
    if ((rc = if_luid(ifname, &row->InterfaceLuid)) != 0)
        return rc;

    if ((err = GetIfEntry2(row)) != NO_ERROR)
    {
        ERROR("GetIfEntry2(%s) failed: error %lu", ifname, err);
        return WIN_RC(err);
    }
    return 0;
}

/** Whether a row of the interface table is worth listing */
static bool
if_row_listed(const MIB_IF_ROW2 *row)
{
    /*
     * Every filter driver bound to an adapter, such as the WFP or QoS
     * ones, appears as an interface of its own.
     */
    return !row->InterfaceAndOperStatusFlags.FilterInterface;
}

/* See the description in win32_internal.h */
te_errno
win32_if_index(const char *ifname, NET_IFINDEX *ifindex)
{
    NET_LUID luid;
    te_errno rc;

    if ((rc = if_luid(ifname, &luid)) != 0)
        return rc;

    if (ConvertInterfaceLuidToIndex(&luid, ifindex) != NO_ERROR)
        return TE_RC(TE_TA_WIN32, TE_ENODEV);
    return 0;
}

/* See the description in win32_internal.h */
te_errno
win32_if_name(const NET_LUID *luid, char *ifname, size_t len)
{
    DWORD err = ConvertInterfaceLuidToNameA(luid, ifname, len);

    if (err != NO_ERROR)
        return WIN_RC(err);
    return 0;
}

static te_errno
interface_list(unsigned int gid, const char *oid, const char *sub_id,
               char **list)
{
    te_string str = TE_STRING_INIT;
    MIB_IF_TABLE2 *table = NULL;
    DWORD err;
    ULONG i;

    UNUSED(gid);
    UNUSED(oid);
    UNUSED(sub_id);

    if ((err = GetIfTable2(&table)) != NO_ERROR)
    {
        ERROR("GetIfTable2() failed: error %lu", err);
        return WIN_RC(err);
    }

    for (i = 0; i < table->NumEntries; i++)
    {
        char ifname[IF_NAMESIZE];

        if (!if_row_listed(&table->Table[i]) ||
            win32_if_name(&table->Table[i].InterfaceLuid, ifname,
                          sizeof(ifname)) != 0)
            continue;

        te_string_append(&str, "%s%s", str.len == 0 ? "" : " ", ifname);
    }
    FreeMibTable(table);

    *list = str.ptr != NULL ? str.ptr : strdup("");
    return 0;
}

static te_errno
ifindex_get(unsigned int gid, const char *oid, char *value,
            const char *ifname)
{
    NET_IFINDEX ifindex;
    te_errno rc;

    UNUSED(gid);
    UNUSED(oid);

    if ((rc = win32_if_index(ifname, &ifindex)) != 0)
        return rc;

    snprintf(value, RCF_MAX_VAL, "%lu", ifindex);
    return 0;
}

static te_errno
alias_get(unsigned int gid, const char *oid, char *value,
          const char *ifname)
{
    MIB_IF_ROW2 row;
    te_errno rc;

    UNUSED(gid);
    UNUSED(oid);

    if ((rc = if_row(ifname, &row)) != 0)
        return rc;

    if (WideCharToMultiByte(CP_UTF8, 0, row.Alias, -1, value, RCF_MAX_VAL,
                            NULL, NULL) == 0)
        *value = '\0';
    return 0;
}

/** Format a hardware address as colon-separated hex bytes */
static void
link_addr2str(const UCHAR *addr, ULONG len, char *str, size_t size)
{
    static const UCHAR zero[ETHER_ADDR_LEN] = { 0, };
    te_string s = TE_STRING_EXT_BUF_INIT(str, size);
    ULONG i;

    /* A hardware address is 6 bytes long in the TE model */
    if (len == 0)
    {
        addr = zero;
        len = ETHER_ADDR_LEN;
    }

    for (i = 0; i < len; i++)
        te_string_append(&s, "%s%02x", i == 0 ? "" : ":", addr[i]);
}

static te_errno
link_addr_get(unsigned int gid, const char *oid, char *value,
              const char *ifname)
{
    MIB_IF_ROW2 row;
    te_errno rc;

    UNUSED(gid);
    UNUSED(oid);

    if ((rc = if_row(ifname, &row)) != 0)
        return rc;

    link_addr2str(row.PhysicalAddress, row.PhysicalAddressLength, value,
                  RCF_MAX_VAL);
    return 0;
}

static te_errno
bcast_link_addr_get(unsigned int gid, const char *oid, char *value,
                    const char *ifname)
{
    static const UCHAR bcast[ETHER_ADDR_LEN] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };
    MIB_IF_ROW2 row;
    te_errno rc;

    UNUSED(gid);
    UNUSED(oid);

    if ((rc = if_row(ifname, &row)) != 0)
        return rc;

    if (row.PhysicalAddressLength == ETHER_ADDR_LEN)
        link_addr2str(bcast, sizeof(bcast), value, RCF_MAX_VAL);
    else
        link_addr2str(NULL, 0, value, RCF_MAX_VAL);
    return 0;
}

/**
 * Get the IP settings of an interface.
 *
 * @param ifname    NDIS interface name
 * @param family    AF_INET or AF_INET6
 * @param row       location for the settings
 *
 * @return Status code, TE_ENOENT if the protocol is not bound.
 */
static te_errno
ip_if_row(const char *ifname, ADDRESS_FAMILY family,
          MIB_IPINTERFACE_ROW *row)
{
    te_errno rc;
    DWORD err;

    InitializeIpInterfaceEntry(row);
    row->Family = family;
    if ((rc = if_luid(ifname, &row->InterfaceLuid)) != 0)
        return rc;

    err = GetIpInterfaceEntry(row);
    if (err == ERROR_NOT_FOUND)
        return TE_RC(TE_TA_WIN32, TE_ENOENT);
    if (err != NO_ERROR)
    {
        ERROR("GetIpInterfaceEntry(%s) failed: error %lu", ifname, err);
        return WIN_RC(err);
    }
    return 0;
}

/**
 * Make the IP settings GetIpInterfaceEntry() has returned acceptable for
 * SetIpInterfaceEntry(), which rejects an IPv4 row with a site prefix.
 */
static void
ip_if_row_prepare_set(MIB_IPINTERFACE_ROW *row)
{
    if (row->Family == AF_INET)
        row->SitePrefixLength = 0;
}

static te_errno
mtu_get(unsigned int gid, const char *oid, char *value,
        const char *ifname)
{
    MIB_IPINTERFACE_ROW ip_row;
    MIB_IF_ROW2 row;
    ULONG mtu;
    te_errno rc;

    UNUSED(gid);
    UNUSED(oid);

    if (ip_if_row(ifname, AF_INET, &ip_row) == 0 ||
        ip_if_row(ifname, AF_INET6, &ip_row) == 0)
    {
        mtu = ip_row.NlMtu;
    }
    else
    {
        if ((rc = if_row(ifname, &row)) != 0)
            return rc;
        mtu = row.Mtu;
    }

    /*
     * The loopback interface of Windows has no limit and reports
     * ULONG_MAX, while the MTU is an int32 in the configuration tree.
     */
    snprintf(value, RCF_MAX_VAL, "%lu", MIN(mtu, (ULONG)INT32_MAX));
    return 0;
}

static te_errno
mtu_set(unsigned int gid, const char *oid, const char *value,
        const char *ifname)
{
    static const ADDRESS_FAMILY families[] = { AF_INET, AF_INET6 };
    unsigned int set = 0;
    unsigned long mtu;
    unsigned int i;
    te_errno rc;

    UNUSED(gid);
    UNUSED(oid);

    if (te_strtoul(value, 0, &mtu) != 0 || mtu == 0 || mtu > UINT32_MAX)
        return TE_RC(TE_TA_WIN32, TE_EINVAL);

    /* The MTU of IP is set per protocol, keep them the same */
    for (i = 0; i < TE_ARRAY_LEN(families); i++)
    {
        MIB_IPINTERFACE_ROW row;
        DWORD err;

        rc = ip_if_row(ifname, families[i], &row);
        if (TE_RC_GET_ERROR(rc) == TE_ENOENT)
            continue;
        if (rc != 0)
            return rc;

        row.NlMtu = mtu;
        ip_if_row_prepare_set(&row);
        if ((err = SetIpInterfaceEntry(&row)) != NO_ERROR)
        {
            ERROR("Failed to set MTU %lu on %s: error %lu", mtu, ifname, err);
            return WIN_RC(err);
        }
        set++;
    }

    if (set == 0)
    {
        ERROR("Neither IPv4 nor IPv6 is bound to %s", ifname);
        return TE_RC(TE_TA_WIN32, TE_ENOENT);
    }
    return 0;
}

static te_errno
status_get(unsigned int gid, const char *oid, char *value,
           const char *ifname)
{
    MIB_IF_ROW2 row;
    te_errno rc;

    UNUSED(gid);
    UNUSED(oid);

    if ((rc = if_row(ifname, &row)) != 0)
        return rc;

    snprintf(value, RCF_MAX_VAL, "%d",
             row.AdminStatus == NET_IF_ADMIN_STATUS_UP ? 1 : 0);
    return 0;
}

static te_errno
status_set(unsigned int gid, const char *oid, const char *value,
           const char *ifname)
{
    MIB_IFROW row;
    NET_IFINDEX ifindex;
    te_errno rc;
    DWORD err;

    UNUSED(gid);
    UNUSED(oid);

    if (strcmp(value, "0") != 0 && strcmp(value, "1") != 0)
        return TE_RC(TE_TA_WIN32, TE_EINVAL);

    if ((rc = win32_if_index(ifname, &ifindex)) != 0)
        return rc;

    /* The legacy SetIfEntry() is the call which changes admin status */
    memset(&row, 0, sizeof(row));
    row.dwIndex = ifindex;
    if ((err = GetIfEntry(&row)) != NO_ERROR)
        return WIN_RC(err);

    row.dwAdminStatus = (*value == '1') ? MIB_IF_ADMIN_STATUS_UP :
                                          MIB_IF_ADMIN_STATUS_DOWN;
    if ((err = SetIfEntry(&row)) != NO_ERROR)
    {
        ERROR("Failed to set status %s on %s: error %lu",
              value, ifname, err);
        return WIN_RC(err);
    }
    return 0;
}

static te_errno
oper_status_get(unsigned int gid, const char *oid, char *value,
                const char *ifname)
{
    MIB_IF_ROW2 row;
    te_errno rc;

    UNUSED(gid);
    UNUSED(oid);

    if ((rc = if_row(ifname, &row)) != 0)
        return rc;

    snprintf(value, RCF_MAX_VAL, "%d",
             row.OperStatus == IfOperStatusUp ? 1 : 0);
    return 0;
}

/**
 * Get or set IP forwarding on an interface.
 *
 * @param ifname    NDIS interface name
 * @param family    AF_INET or AF_INET6
 * @param value     value to set, or @c NULL to get it into @p out
 * @param out       location for the value
 *
 * @return Status code.
 */
static te_errno
if_forwarding(const char *ifname, ADDRESS_FAMILY family, const char *value,
              char *out)
{
    MIB_IPINTERFACE_ROW row;
    te_errno rc;
    DWORD err;

    if ((rc = ip_if_row(ifname, family, &row)) != 0)
        return rc;

    if (value == NULL)
    {
        snprintf(out, RCF_MAX_VAL, "%d", row.ForwardingEnabled ? 1 : 0);
        return 0;
    }

    if (strcmp(value, "0") != 0 && strcmp(value, "1") != 0)
        return TE_RC(TE_TA_WIN32, TE_EINVAL);

    row.ForwardingEnabled = (*value == '1');
    ip_if_row_prepare_set(&row);
    if ((err = SetIpInterfaceEntry(&row)) != NO_ERROR)
    {
        ERROR("Failed to set forwarding on %s: error %lu", ifname, err);
        return WIN_RC(err);
    }
    return 0;
}

static te_errno
iface_ip4_fw_get(unsigned int gid, const char *oid, char *value,
                 const char *ifname)
{
    UNUSED(gid);
    UNUSED(oid);
    return if_forwarding(ifname, AF_INET, NULL, value);
}

static te_errno
iface_ip4_fw_set(unsigned int gid, const char *oid, const char *value,
                 const char *ifname)
{
    UNUSED(gid);
    UNUSED(oid);
    return if_forwarding(ifname, AF_INET, value, NULL);
}

static te_errno
iface_ip6_fw_get(unsigned int gid, const char *oid, char *value,
                 const char *ifname)
{
    UNUSED(gid);
    UNUSED(oid);
    return if_forwarding(ifname, AF_INET6, NULL, value);
}

static te_errno
iface_ip6_fw_set(unsigned int gid, const char *oid, const char *value,
                 const char *ifname)
{
    UNUSED(gid);
    UNUSED(oid);
    return if_forwarding(ifname, AF_INET6, value, NULL);
}

/* Network addresses */

/**
 * Parse an IPv4 or IPv6 address.
 *
 * @param str       address string, an IPv6 one has no scope
 * @param addr      location for the address
 *
 * @return Status code.
 */
static te_errno
inet_addr_parse(const char *str, SOCKADDR_INET *addr)
{
    memset(addr, 0, sizeof(*addr));

    if (inet_pton(AF_INET, str, &addr->Ipv4.sin_addr) == 1)
    {
        addr->si_family = AF_INET;
        return 0;
    }
    if (inet_pton(AF_INET6, str, &addr->Ipv6.sin6_addr) == 1)
    {
        addr->si_family = AF_INET6;
        return 0;
    }

    ERROR("Invalid IP address '%s'", str);
    return TE_RC(TE_TA_WIN32, TE_EINVAL);
}

/** Format an IPv4 or IPv6 address without the scope */
static void
inet_addr_str(const SOCKADDR_INET *addr, char *str, size_t len)
{
    const void *a = (addr->si_family == AF_INET) ?
                        (const void *)&addr->Ipv4.sin_addr :
                        (const void *)&addr->Ipv6.sin6_addr;

    if (inet_ntop(addr->si_family, a, str, len) == NULL)
        te_strlcpy(str, "", len);
}

/** Whether an address is the unspecified one */
static bool
inet_addr_is_any(const SOCKADDR_INET *a)
{
    static const IN6_ADDR any6;

    if (a->si_family == AF_INET)
        return a->Ipv4.sin_addr.s_addr == INADDR_ANY;
    return memcmp(&a->Ipv6.sin6_addr, &any6, sizeof(any6)) == 0;
}

/**
 * Find an address of an interface.
 *
 * @param ifname    NDIS interface name
 * @param addr_str  address string
 * @param row       location for the address row
 *
 * @return Status code.
 */
static te_errno
net_addr_find(const char *ifname, const char *addr_str,
              MIB_UNICASTIPADDRESS_ROW *row)
{
    SOCKADDR_INET addr;
    te_errno rc;
    DWORD err;

    if ((rc = inet_addr_parse(addr_str, &addr)) != 0)
        return rc;

    InitializeUnicastIpAddressEntry(row);
    if ((rc = if_luid(ifname, &row->InterfaceLuid)) != 0)
        return rc;
    row->Address = addr;

    if ((err = GetUnicastIpAddressEntry(row)) != NO_ERROR)
        return TE_RC(TE_TA_WIN32, TE_ENOENT);
    return 0;
}

static te_errno
net_addr_add(unsigned int gid, const char *oid, const char *value,
             const char *ifname, const char *addr_str)
{
    MIB_UNICASTIPADDRESS_ROW row;
    unsigned long prefix;
    te_errno rc;
    DWORD err;

    UNUSED(gid);
    UNUSED(oid);

    InitializeUnicastIpAddressEntry(&row);
    if ((rc = if_luid(ifname, &row.InterfaceLuid)) != 0)
        return rc;
    if ((rc = inet_addr_parse(addr_str, &row.Address)) != 0)
        return rc;
    if (inet_addr_is_any(&row.Address))
        return TE_RC(TE_TA_WIN32, TE_EINVAL);

    if (te_strtoul(value, 10, &prefix) != 0 ||
        prefix > (row.Address.si_family == AF_INET ? 32 : 128))
    {
        ERROR("Invalid prefix length '%s'", value);
        return TE_RC(TE_TA_WIN32, TE_EINVAL);
    }

    if (prefix == 0)
    {
        /* The prefix of the address class, as the Unix agent does */
        if (row.Address.si_family == AF_INET)
        {
            uint32_t a = ntohl(row.Address.Ipv4.sin_addr.s_addr);

            prefix = (a & 0x80000000) == 0 ? 8 :
                     (a & 0xc0000000) == 0x80000000 ? 16 : 24;
        }
        else
        {
            prefix = 64;
        }
    }

    row.OnLinkPrefixLength = prefix;
    row.DadState = IpDadStatePreferred;

    if ((err = CreateUnicastIpAddressEntry(&row)) != NO_ERROR)
    {
        ERROR("Failed to add %s/%lu to %s: error %lu",
              addr_str, prefix, ifname, err);
        return (err == ERROR_OBJECT_ALREADY_EXISTS) ?
                   TE_RC(TE_TA_WIN32, TE_EEXIST) : WIN_RC(err);
    }
    return 0;
}

static te_errno
net_addr_del(unsigned int gid, const char *oid, const char *ifname,
             const char *addr_str)
{
    MIB_UNICASTIPADDRESS_ROW row;
    te_errno rc;
    DWORD err;

    UNUSED(gid);
    UNUSED(oid);

    if ((rc = net_addr_find(ifname, addr_str, &row)) != 0)
        return rc;

    if ((err = DeleteUnicastIpAddressEntry(&row)) != NO_ERROR)
    {
        ERROR("Failed to delete %s from %s: error %lu",
              addr_str, ifname, err);
        return WIN_RC(err);
    }
    return 0;
}

static te_errno
net_addr_list(unsigned int gid, const char *oid, const char *sub_id,
              char **list, const char *ifname)
{
    te_string str = TE_STRING_INIT;
    MIB_UNICASTIPADDRESS_TABLE *table = NULL;
    NET_LUID luid;
    te_errno rc;
    DWORD err;
    ULONG i;

    UNUSED(gid);
    UNUSED(oid);
    UNUSED(sub_id);

    if ((rc = if_luid(ifname, &luid)) != 0)
        return rc;

    if ((err = GetUnicastIpAddressTable(AF_UNSPEC, &table)) != NO_ERROR)
    {
        ERROR("GetUnicastIpAddressTable() failed: error %lu", err);
        return WIN_RC(err);
    }

    for (i = 0; i < table->NumEntries; i++)
    {
        char addr[INET6_ADDRSTRLEN];

        if (table->Table[i].InterfaceLuid.Value != luid.Value)
            continue;

        inet_addr_str(&table->Table[i].Address, addr, sizeof(addr));
        te_string_append(&str, "%s%s", str.len == 0 ? "" : " ", addr);
    }
    FreeMibTable(table);

    *list = str.ptr != NULL ? str.ptr : strdup("");
    return 0;
}

static te_errno
prefix_get(unsigned int gid, const char *oid, char *value,
           const char *ifname, const char *addr_str)
{
    MIB_UNICASTIPADDRESS_ROW row;
    te_errno rc;

    UNUSED(gid);
    UNUSED(oid);

    if ((rc = net_addr_find(ifname, addr_str, &row)) != 0)
        return rc;

    snprintf(value, RCF_MAX_VAL, "%u", row.OnLinkPrefixLength);
    return 0;
}

static te_errno
prefix_set(unsigned int gid, const char *oid, const char *value,
           const char *ifname, const char *addr_str)
{
    MIB_UNICASTIPADDRESS_ROW row;
    unsigned long prefix;
    te_errno rc;
    DWORD err;

    UNUSED(gid);
    UNUSED(oid);

    if ((rc = net_addr_find(ifname, addr_str, &row)) != 0)
        return rc;

    if (te_strtoul(value, 10, &prefix) != 0 ||
        prefix > (row.Address.si_family == AF_INET ? 32 : 128))
        return TE_RC(TE_TA_WIN32, TE_EINVAL);

    row.OnLinkPrefixLength = prefix;
    if ((err = SetUnicastIpAddressEntry(&row)) != NO_ERROR)
    {
        ERROR("Failed to set prefix %lu of %s on %s: error %lu",
              prefix, addr_str, ifname, err);
        return WIN_RC(err);
    }
    return 0;
}

/** Compute the broadcast address of an IPv4 network address */
static te_errno
broadcast_compute(const char *ifname, const char *addr_str,
                  struct in_addr *bcast)
{
    MIB_UNICASTIPADDRESS_ROW row;
    uint32_t mask;
    te_errno rc;

    if ((rc = net_addr_find(ifname, addr_str, &row)) != 0)
        return rc;

    if (row.Address.si_family != AF_INET)
        return TE_RC(TE_TA_WIN32, TE_EAFNOSUPPORT);

    mask = row.OnLinkPrefixLength == 0 ?
               0 : htonl(0xffffffffU << (32 - row.OnLinkPrefixLength));
    bcast->s_addr = row.Address.Ipv4.sin_addr.s_addr | ~mask;
    return 0;
}

static te_errno
broadcast_get(unsigned int gid, const char *oid, char *value,
              const char *ifname, const char *addr_str)
{
    struct in_addr bcast;
    te_errno rc;

    UNUSED(gid);
    UNUSED(oid);

    rc = broadcast_compute(ifname, addr_str, &bcast);
    if (TE_RC_GET_ERROR(rc) == TE_EAFNOSUPPORT)
    {
        snprintf(value, RCF_MAX_VAL, "0.0.0.0");
        return 0;
    }
    if (rc != 0)
        return rc;

    inet_ntop(AF_INET, &bcast, value, RCF_MAX_VAL);
    return 0;
}

static te_errno
broadcast_set(unsigned int gid, const char *oid, const char *value,
              const char *ifname, const char *addr_str)
{
    struct in_addr bcast;
    struct in_addr req;
    te_errno rc;

    UNUSED(gid);
    UNUSED(oid);

    /* Windows derives the broadcast address from the prefix */
    rc = broadcast_compute(ifname, addr_str, &bcast);
    if (rc != 0)
        return rc;

    if (inet_pton(AF_INET, value, &req) != 1)
        return TE_RC(TE_TA_WIN32, TE_EINVAL);

    if (req.s_addr != bcast.s_addr)
    {
        ERROR("Broadcast address %s of %s differs from the one the prefix "
              "gives, Windows cannot set it", value, addr_str);
        return TE_RC(TE_TA_WIN32, TE_EOPNOTSUPP);
    }
    return 0;
}

/* Neighbours */

/** Whether a neighbour entry belongs to the static or dynamic table */
static bool
neigh_is_static(const MIB_IPNET_ROW2 *row)
{
    return row->State == NlnsPermanent;
}

/** Whether the OID is the one of a static neighbour */
static bool
neigh_oid_static(const char *oid)
{
    return strstr(oid, "neigh_static") != NULL;
}

/** Whether a neighbour entry is worth listing */
static bool
neigh_listed(const MIB_IPNET_ROW2 *row)
{
    if (row->State == NlnsUnreachable || row->State == NlnsIncomplete)
        return false;

    /* Multicast and broadcast entries are created by the stack itself */
    if (row->Address.si_family == AF_INET)
    {
        uint32_t a = ntohl(row->Address.Ipv4.sin_addr.s_addr);

        return !IN_MULTICAST(a) && a != INADDR_BROADCAST;
    }
    return !IN6_IS_ADDR_MULTICAST(&row->Address.Ipv6.sin6_addr);
}

/**
 * Find a neighbour entry.
 *
 * @param oid       OID of the instance, it tells static from dynamic
 * @param ifname    NDIS interface name
 * @param addr_str  neighbour address
 * @param row       location for the entry
 *
 * @return Status code.
 */
static te_errno
neigh_find(const char *oid, const char *ifname, const char *addr_str,
           MIB_IPNET_ROW2 *row)
{
    SOCKADDR_INET addr;
    te_errno rc;

    if ((rc = inet_addr_parse(addr_str, &addr)) != 0)
        return rc;

    memset(row, 0, sizeof(*row));
    if ((rc = if_luid(ifname, &row->InterfaceLuid)) != 0)
        return rc;
    row->Address = addr;

    if (GetIpNetEntry2(row) != NO_ERROR || !neigh_listed(row) ||
        neigh_is_static(row) != neigh_oid_static(oid))
        return TE_RC(TE_TA_WIN32, TE_ENOENT);

    return 0;
}

/** Parse a hardware address of colon-separated hex bytes */
static te_errno
link_addr_parse(const char *str, MIB_IPNET_ROW2 *row)
{
    unsigned int b[ETHER_ADDR_LEN];
    unsigned int i;
    char end;

    if (sscanf(str, "%2x:%2x:%2x:%2x:%2x:%2x%c", &b[0], &b[1], &b[2],
               &b[3], &b[4], &b[5], &end) != ETHER_ADDR_LEN)
    {
        ERROR("Invalid link-layer address '%s'", str);
        return TE_RC(TE_TA_WIN32, TE_EINVAL);
    }

    for (i = 0; i < ETHER_ADDR_LEN; i++)
        row->PhysicalAddress[i] = b[i];
    row->PhysicalAddressLength = ETHER_ADDR_LEN;
    return 0;
}

static te_errno
neigh_state_get(unsigned int gid, const char *oid, char *value,
                const char *ifname, const char *addr_str)
{
    MIB_IPNET_ROW2 row;
    te_errno rc;
    int state;

    UNUSED(gid);

    if ((rc = neigh_find(oid, ifname, addr_str, &row)) != 0)
        return rc;

    switch (row.State)
    {
        case NlnsIncomplete:    state = CS_NEIGH_INCOMPLETE; break;
        case NlnsProbe:         state = CS_NEIGH_PROBE; break;
        case NlnsDelay:         state = CS_NEIGH_DELAY; break;
        case NlnsStale:         state = CS_NEIGH_STALE; break;
        case NlnsReachable:     state = CS_NEIGH_REACHABLE; break;
        case NlnsUnreachable:   state = CS_NEIGH_FAILED; break;
        default:                state = CS_NEIGH_NOARP; break;
    }

    snprintf(value, RCF_MAX_VAL, "%d", state);
    return 0;
}

static te_errno
neigh_get(unsigned int gid, const char *oid, char *value,
          const char *ifname, const char *addr_str)
{
    MIB_IPNET_ROW2 row;
    te_errno rc;

    UNUSED(gid);

    if ((rc = neigh_find(oid, ifname, addr_str, &row)) != 0)
        return rc;

    link_addr2str(row.PhysicalAddress, row.PhysicalAddressLength, value,
                  RCF_MAX_VAL);
    return 0;
}

static te_errno
neigh_set(unsigned int gid, const char *oid, const char *value,
          const char *ifname, const char *addr_str)
{
    MIB_IPNET_ROW2 row;
    te_errno rc;
    DWORD err;

    UNUSED(gid);

    if ((rc = neigh_find(oid, ifname, addr_str, &row)) != 0)
        return rc;
    if ((rc = link_addr_parse(value, &row)) != 0)
        return rc;

    if ((err = SetIpNetEntry2(&row)) != NO_ERROR)
    {
        ERROR("Failed to change neighbour %s on %s: error %lu",
              addr_str, ifname, err);
        return WIN_RC(err);
    }
    return 0;
}

static te_errno
neigh_add(unsigned int gid, const char *oid, const char *value,
          const char *ifname, const char *addr_str)
{
    MIB_IPNET_ROW2 row;
    te_errno rc;
    DWORD err;

    UNUSED(gid);

    memset(&row, 0, sizeof(row));
    if ((rc = if_luid(ifname, &row.InterfaceLuid)) != 0)
        return rc;
    if ((rc = inet_addr_parse(addr_str, &row.Address)) != 0)
        return rc;
    if ((rc = link_addr_parse(value, &row)) != 0)
        return rc;

    row.State = neigh_oid_static(oid) ? NlnsPermanent : NlnsReachable;

    if ((err = CreateIpNetEntry2(&row)) != NO_ERROR)
    {
        if (err == ERROR_OBJECT_ALREADY_EXISTS)
            err = SetIpNetEntry2(&row);
        if (err != NO_ERROR)
        {
            ERROR("Failed to add neighbour %s on %s: error %lu",
                  addr_str, ifname, err);
            return WIN_RC(err);
        }
    }
    return 0;
}

static te_errno
neigh_del(unsigned int gid, const char *oid, const char *ifname,
          const char *addr_str)
{
    MIB_IPNET_ROW2 row;
    te_errno rc;
    DWORD err;

    UNUSED(gid);

    if ((rc = neigh_find(oid, ifname, addr_str, &row)) != 0)
        return rc;

    if ((err = DeleteIpNetEntry2(&row)) != NO_ERROR)
    {
        ERROR("Failed to delete neighbour %s on %s: error %lu",
              addr_str, ifname, err);
        return WIN_RC(err);
    }
    return 0;
}

static te_errno
neigh_list(unsigned int gid, const char *oid, const char *sub_id,
           char **list, const char *ifname)
{
    te_string str = TE_STRING_INIT;
    MIB_IPNET_TABLE2 *table = NULL;
    bool is_static = neigh_oid_static(oid);
    NET_LUID luid;
    te_errno rc;
    DWORD err;
    ULONG i;

    UNUSED(gid);
    UNUSED(sub_id);

    if ((rc = if_luid(ifname, &luid)) != 0)
        return rc;

    if ((err = GetIpNetTable2(AF_UNSPEC, &table)) != NO_ERROR)
    {
        ERROR("GetIpNetTable2() failed: error %lu", err);
        return WIN_RC(err);
    }

    for (i = 0; i < table->NumEntries; i++)
    {
        const MIB_IPNET_ROW2 *row = &table->Table[i];
        char addr[INET6_ADDRSTRLEN];

        if (row->InterfaceLuid.Value != luid.Value || !neigh_listed(row) ||
            neigh_is_static(row) != is_static)
            continue;

        inet_addr_str(&row->Address, addr, sizeof(addr));
        te_string_append(&str, "%s%s", str.len == 0 ? "" : " ", addr);
    }
    FreeMibTable(table);

    *list = str.ptr != NULL ? str.ptr : strdup("");
    return 0;
}

/* Routes */

/** Length of the full prefix of an address family */
static unsigned int
family_bits(ADDRESS_FAMILY family)
{
    return family == AF_INET ? 32 : 128;
}

/**
 * Whether a route is listed. Windows keeps the routes Linux puts into
 * the local table (to the own addresses and the broadcast ones) in the
 * same table, and a multicast route on every interface. These are left
 * out, so that the names stay unique.
 */
static bool
route_listed(const MIB_IPFORWARD_ROW2 *row)
{
    const SOCKADDR_INET *dst = &row->DestinationPrefix.Prefix;

    if (row->Protocol == MIB_IPPROTO_LOCAL &&
        row->DestinationPrefix.PrefixLength == family_bits(dst->si_family))
        return false;

    if (dst->si_family == AF_INET)
    {
        uint32_t a = ntohl(dst->Ipv4.sin_addr.s_addr);

        return !IN_MULTICAST(a) && a != INADDR_BROADCAST;
    }
    return !IN6_IS_ADDR_MULTICAST(&dst->Ipv6.sin6_addr);
}

/** Make the instance name of a route */
static void
route_name(const MIB_IPFORWARD_ROW2 *row, te_string *name)
{
    char dst[INET6_ADDRSTRLEN];

    inet_addr_str(&row->DestinationPrefix.Prefix, dst, sizeof(dst));
    te_string_append(name, "%s|%u", dst, row->DestinationPrefix.PrefixLength);
    if (row->Metric != 0)
        te_string_append(name, ",metric=%lu", row->Metric);
}

/**
 * Find a route by its instance name.
 *
 * @param name      instance name
 * @param rt_info   location for the route information, or @c NULL
 * @param out       location for the route row, or @c NULL
 *
 * @return Status code.
 */
static te_errno
route_find(const char *name, ta_rt_info_t *rt_info, MIB_IPFORWARD_ROW2 *out)
{
    MIB_IPFORWARD_TABLE2 *table = NULL;
    te_errno rc = TE_RC(TE_TA_WIN32, TE_ENOENT);
    ta_rt_info_t parsed;
    DWORD err;
    ULONG i;

    if ((rc = ta_rt_parse_inst_name(name, &parsed)) != 0)
        return TE_RC(TE_TA_WIN32, rc);
    rc = TE_RC(TE_TA_WIN32, TE_ENOENT);

    if ((err = GetIpForwardTable2(AF_UNSPEC, &table)) != NO_ERROR)
    {
        ERROR("GetIpForwardTable2() failed: error %lu", err);
        return WIN_RC(err);
    }

    for (i = 0; i < table->NumEntries; i++)
    {
        const MIB_IPFORWARD_ROW2 *row = &table->Table[i];
        te_string rname = TE_STRING_INIT;
        bool match;

        if (!route_listed(row))
            continue;

        route_name(row, &rname);
        match = (strcmp(rname.ptr, name) == 0);
        te_string_free(&rname);
        if (!match)
            continue;

        if (rt_info != NULL)
        {
            *rt_info = parsed;
            if (!inet_addr_is_any(&row->NextHop))
            {
                memcpy(&rt_info->gw, &row->NextHop,
                       row->NextHop.si_family == AF_INET ?
                           sizeof(row->NextHop.Ipv4) :
                           sizeof(row->NextHop.Ipv6));
                rt_info->flags |= TA_RT_INFO_FLG_GW;
            }
            if (win32_if_name(&row->InterfaceLuid, rt_info->ifname,
                              sizeof(rt_info->ifname)) == 0)
                rt_info->flags |= TA_RT_INFO_FLG_IF;
        }
        if (out != NULL)
            *out = *row;
        rc = 0;
        break;
    }
    FreeMibTable(table);

    return rc;
}

static te_errno
route_load_attrs(ta_cfg_obj_t *obj)
{
    ta_rt_info_t rt_info;
    te_errno rc;

    if ((rc = route_find(obj->name, &rt_info, NULL)) != 0)
        return rc;

    if ((rt_info.flags & TA_RT_INFO_FLG_IF) != 0 &&
        (rc = ta_obj_attr_set(obj, "dev", rt_info.ifname)) != 0)
        return rc;

    return 0;
}

static te_errno
route_list(unsigned int gid, const char *oid, const char *sub_id,
           char **list)
{
    te_string str = TE_STRING_INIT;
    te_string seen = TE_STRING_INIT;
    MIB_IPFORWARD_TABLE2 *table = NULL;
    DWORD err;
    ULONG i;

    UNUSED(gid);
    UNUSED(oid);
    UNUSED(sub_id);

    if ((err = GetIpForwardTable2(AF_UNSPEC, &table)) != NO_ERROR)
    {
        ERROR("GetIpForwardTable2() failed: error %lu", err);
        return WIN_RC(err);
    }

    /* Space-separated and surrounded, so that strstr() finds whole names */
    te_string_append(&seen, " ");
    for (i = 0; i < table->NumEntries; i++)
    {
        te_string name = TE_STRING_INIT;
        te_string key = TE_STRING_INIT;

        if (!route_listed(&table->Table[i]))
            continue;

        route_name(&table->Table[i], &name);
        te_string_append(&key, " %s ", name.ptr);

        /*
         * The same destination may be reachable via several interfaces
         * with the same metric. Only the first route has a name then.
         */
        if (strstr(seen.ptr, key.ptr) == NULL)
        {
            te_string_append(&seen, "%s ", name.ptr);
            te_string_append(&str, "%s%s", str.len == 0 ? "" : " ",
                             name.ptr);
        }
        te_string_free(&key);
        te_string_free(&name);
    }
    FreeMibTable(table);
    te_string_free(&seen);

    *list = str.ptr != NULL ? str.ptr : strdup("");
    return 0;
}

static te_errno
route_get(unsigned int gid, const char *oid, char *value,
          const char *route)
{
    ta_rt_info_t rt_info;
    te_errno rc;

    UNUSED(gid);
    UNUSED(oid);

    if ((rc = route_find(route, &rt_info, NULL)) != 0)
        return rc;

    if (rt_info.flags & TA_RT_INFO_FLG_GW)
    {
        te_sockaddr_h2str_buf(SA(&rt_info.gw), value, RCF_MAX_VAL);
    }
    else
    {
        snprintf(value, RCF_MAX_VAL, "%s",
                 rt_info.dst.ss_family == AF_INET ? "0.0.0.0" : "::");
    }
    return 0;
}

static te_errno
route_set(unsigned int gid, const char *oid, const char *value,
          const char *route)
{
    UNUSED(oid);

    return ta_obj_value_set(TA_OBJ_TYPE_ROUTE, route, value, gid,
                            route_load_attrs);
}

static te_errno
route_add(unsigned int gid, const char *oid, const char *value,
          const char *route)
{
    UNUSED(oid);

    return ta_obj_add(TA_OBJ_TYPE_ROUTE, route, value, gid,
                      NULL, NULL, NULL);
}

static te_errno
route_del(unsigned int gid, const char *oid, const char *route)
{
    UNUSED(oid);

    return ta_obj_del(TA_OBJ_TYPE_ROUTE, route, NULL, NULL, gid,
                      route_load_attrs);
}

static te_errno
route_dev_get(unsigned int gid, const char *oid, char *value,
              const char *route)
{
    ta_rt_info_t rt_info;
    te_errno rc;

    UNUSED(gid);
    UNUSED(oid);

    if ((rc = route_find(route, &rt_info, NULL)) != 0)
        return rc;

    snprintf(value, RCF_MAX_VAL, "%s", rt_info.ifname);
    return 0;
}

static te_errno
route_dev_set(unsigned int gid, const char *oid, const char *value,
              const char *route)
{
    UNUSED(oid);

    return ta_obj_set(TA_OBJ_TYPE_ROUTE, route, "dev", value, gid,
                      route_load_attrs);
}

/** Convert route information to a forwarding table row */
static te_errno
rt_info2row(const ta_rt_info_t *rt_info, MIB_IPFORWARD_ROW2 *row)
{
    ADDRESS_FAMILY family = rt_info->dst.ss_family;
    te_errno rc;

    InitializeIpForwardEntry(row);

    row->DestinationPrefix.Prefix.si_family = family;
    if (family == AF_INET)
        row->DestinationPrefix.Prefix.Ipv4 = *SIN(&rt_info->dst);
    else
        row->DestinationPrefix.Prefix.Ipv6 = *SIN6(&rt_info->dst);
    row->DestinationPrefix.PrefixLength = rt_info->prefix;

    row->NextHop.si_family = family;
    if (rt_info->flags & TA_RT_INFO_FLG_GW)
    {
        if (family == AF_INET)
            row->NextHop.Ipv4 = *SIN(&rt_info->gw);
        else
            row->NextHop.Ipv6 = *SIN6(&rt_info->gw);
    }

    if (rt_info->flags & TA_RT_INFO_FLG_IF)
    {
        if ((rc = if_luid(rt_info->ifname, &row->InterfaceLuid)) != 0)
            return rc;
    }
    else if (rt_info->flags & TA_RT_INFO_FLG_GW)
    {
        SOCKADDR_INET best_src;
        MIB_IPFORWARD_ROW2 best;
        DWORD err;

        /* The interface the gateway is reachable through */
        err = GetBestRoute2(NULL, 0, NULL, &row->NextHop, 0, &best,
                            &best_src);
        if (err != NO_ERROR)
        {
            ERROR("No interface to reach the gateway: error %lu", err);
            return WIN_RC(err);
        }
        row->InterfaceLuid = best.InterfaceLuid;
    }
    else
    {
        ERROR("Route has neither a gateway nor an interface");
        return TE_RC(TE_TA_WIN32, TE_EINVAL);
    }

    /*
     * Windows picks a metric if none is given, and the route would not
     * have the name it is added with then.
     */
    row->Metric = (rt_info->flags & TA_RT_INFO_FLG_METRIC) ?
                      rt_info->metric : 0;

    if (rt_info->flags & (TA_RT_INFO_FLG_MTU | TA_RT_INFO_FLG_WIN |
                          TA_RT_INFO_FLG_IRTT | TA_RT_INFO_FLG_TOS |
                          TA_RT_INFO_FLG_SRC | TA_RT_INFO_FLG_HOPLIMIT))
        WARN("Windows routes have no MTU, window, IRTT, TOS, source "
             "and hop limit, they are ignored");

    row->Protocol = MIB_IPPROTO_NETMGMT;
    return 0;
}

static te_errno
route_commit(unsigned int gid, const cfg_oid *p_oid)
{
    const char *route;
    ta_cfg_obj_t *obj;
    ta_cfg_obj_action_e action;
    ta_rt_info_t rt_info;
    MIB_IPFORWARD_ROW2 old;
    MIB_IPFORWARD_ROW2 row;
    te_errno rc = 0;
    DWORD err;

    route = ((cfg_inst_subid *)(p_oid->ids))[p_oid->len - 1].name;

    if ((obj = ta_obj_find(TA_OBJ_TYPE_ROUTE, route, gid)) == NULL)
    {
        WARN("Commit for %s route which has not been updated", route);
        return 0;
    }

    memset(&rt_info, 0, sizeof(rt_info));
    if ((rc = ta_rt_parse_obj(obj, &rt_info)) != 0)
    {
        ta_obj_free(obj);
        return TE_RC(TE_TA_WIN32, rc);
    }

    action = obj->action;
    if (action == TA_CFG_OBJ_DELETE || action == TA_CFG_OBJ_SET)
        rc = route_find(obj->name, NULL, &old);
    ta_obj_free(obj);
    if (rc != 0)
        goto out;

    switch (action)
    {
        case TA_CFG_OBJ_DELETE:
            if ((err = DeleteIpForwardEntry2(&old)) != NO_ERROR)
            {
                ERROR("Failed to delete route %s: error %lu", route, err);
                rc = WIN_RC(err);
            }
            break;

        case TA_CFG_OBJ_SET:
        case TA_CFG_OBJ_CREATE:
            if ((rc = rt_info2row(&rt_info, &row)) != 0)
                break;

            if (action == TA_CFG_OBJ_SET &&
                (err = DeleteIpForwardEntry2(&old)) != NO_ERROR)
            {
                ERROR("Failed to replace route %s: error %lu", route, err);
                rc = WIN_RC(err);
                break;
            }

            if ((err = CreateIpForwardEntry2(&row)) != NO_ERROR)
            {
                ERROR("Failed to add route %s: error %lu", route, err);
                rc = (err == ERROR_OBJECT_ALREADY_EXISTS) ?
                         TE_RC(TE_TA_WIN32, TE_EEXIST) : WIN_RC(err);
            }
            break;

        default:
            ERROR("Unknown object action %d", action);
            rc = TE_RC(TE_TA_WIN32, TE_EINVAL);
            break;
    }

out:
    ta_rt_info_clean(&rt_info);
    return rc;
}

/* Environment */

/** Variables the agent hides from the configurator */
static const char * const env_hidden[] = {
    "SSH_CLIENT",
    "SSH_CONNECTION",
    "SUDO_COMMAND",
    "TE_RPC_PORT",
    "TE_LOG_PORT",
    "TARPC_DL_NAME",
    "TCE_",
};

/** Whether a variable is hidden, see env_hidden */
static bool
env_is_hidden(const char *name, size_t name_len)
{
    unsigned int i;

    for (i = 0; i < TE_ARRAY_LEN(env_hidden); i++)
    {
        size_t len = strlen(env_hidden[i]);

        if (name_len >= len && strncmp(env_hidden[i], name, len) == 0)
            return true;
    }
    return false;
}

static te_errno
env_get(unsigned int gid, const char *oid, char *value, const char *name)
{
    const char *val = getenv(name);

    UNUSED(gid);
    UNUSED(oid);

    if (val == NULL || env_is_hidden(name, strlen(name)))
        return TE_RC(TE_TA_WIN32, TE_ENOENT);

    if (strlen(val) >= RCF_MAX_VAL)
        WARN("Environment variable '%s' value truncated", name);
    te_strlcpy(value, val, RCF_MAX_VAL);
    return 0;
}

static te_errno
env_set(unsigned int gid, const char *oid, const char *value,
        const char *name)
{
    UNUSED(gid);
    UNUSED(oid);

    if (env_is_hidden(name, strlen(name)))
        return TE_RC(TE_TA_WIN32, TE_EPERM);

    if (setenv(name, value, 1) != 0)
        return TE_OS_RC(TE_TA_WIN32, errno);
    return 0;
}

static te_errno
env_add(unsigned int gid, const char *oid, const char *value,
        const char *name)
{
    if (getenv(name) != NULL)
        return TE_RC(TE_TA_WIN32, TE_EEXIST);

    return env_set(gid, oid, value, name);
}

static te_errno
env_del(unsigned int gid, const char *oid, const char *name)
{
    UNUSED(gid);
    UNUSED(oid);

    if (getenv(name) == NULL || env_is_hidden(name, strlen(name)))
        return TE_RC(TE_TA_WIN32, TE_ENOENT);

    if (unsetenv(name) != 0)
        return TE_OS_RC(TE_TA_WIN32, errno);
    return 0;
}

static te_errno
env_list(unsigned int gid, const char *oid, const char *sub_id,
         char **list)
{
    te_string str = TE_STRING_INIT;
    char **env;

    UNUSED(gid);
    UNUSED(oid);
    UNUSED(sub_id);

    for (env = environ; env != NULL && *env != NULL; env++)
    {
        const char *eq = strchr(*env, '=');

        /* Windows keeps per-drive directories as "=C:=C:\..." */
        if (eq == NULL || eq == *env ||
            env_is_hidden(*env, (size_t)(eq - *env)))
            continue;

        te_string_append(&str, "%s%.*s", str.len == 0 ? "" : " ",
                         (int)(eq - *env), *env);
    }

    *list = str.ptr != NULL ? str.ptr : strdup("");
    return 0;
}

/* System information */

static te_errno
uname_field_get(char *value, size_t offset)
{
    struct utsname val;

    if (uname(&val) != 0)
        return TE_OS_RC(TE_TA_WIN32, errno);

    te_strlcpy(value, (const char *)&val + offset, RCF_MAX_VAL);
    return 0;
}

static te_errno
uname_get(unsigned int gid, const char *oid, char *value)
{
    UNUSED(gid);
    UNUSED(oid);
    return uname_field_get(value, offsetof(struct utsname, sysname));
}

static te_errno
uname_machine_get(unsigned int gid, const char *oid, char *value)
{
    UNUSED(gid);
    UNUSED(oid);
    return uname_field_get(value, offsetof(struct utsname, machine));
}

static te_errno
uname_release_get(unsigned int gid, const char *oid, char *value)
{
    UNUSED(gid);
    UNUSED(oid);
    return uname_field_get(value, offsetof(struct utsname, release));
}

static te_errno
uname_version_get(unsigned int gid, const char *oid, char *value)
{
    UNUSED(gid);
    UNUSED(oid);
    return uname_field_get(value, offsetof(struct utsname, version));
}

static te_errno
agent_platform_get(unsigned int gid, const char *oid, char *value)
{
    UNUSED(gid);
    UNUSED(oid);
    te_strlcpy(value, TE_AGT_PLATFORM, RCF_MAX_VAL);
    return 0;
}

static te_errno
agent_dir_get(unsigned int gid, const char *oid, char *value)
{
    UNUSED(gid);
    UNUSED(oid);
    te_strlcpy(value, ta_dir, RCF_MAX_VAL);
    return 0;
}

static te_errno
agent_tmp_dir_get(unsigned int gid, const char *oid, char *value)
{
    UNUSED(gid);
    UNUSED(oid);
    te_strlcpy(value, ta_tmp_dir, RCF_MAX_VAL);
    return 0;
}

/*
 * The configuration tree. A node points to its first child and to its
 * next sibling, so the tree is defined from the leaves up.
 */

RCF_PCH_CFG_NODE_RO(node_neigh_state, "state", NULL, NULL,
                    neigh_state_get);

static rcf_pch_cfg_object node_neigh_dynamic =
    { "neigh_dynamic", 0, &node_neigh_state, NULL,
      (rcf_ch_cfg_get)neigh_get, (rcf_ch_cfg_set)neigh_set,
      (rcf_ch_cfg_add)neigh_add, (rcf_ch_cfg_del)neigh_del,
      (rcf_ch_cfg_list)neigh_list, NULL, NULL, NULL };

static rcf_pch_cfg_object node_neigh_static =
    { "neigh_static", 0, NULL, &node_neigh_dynamic,
      (rcf_ch_cfg_get)neigh_get, (rcf_ch_cfg_set)neigh_set,
      (rcf_ch_cfg_add)neigh_add, (rcf_ch_cfg_del)neigh_del,
      (rcf_ch_cfg_list)neigh_list, NULL, NULL, NULL };

RCF_PCH_CFG_NODE_RW(node_broadcast, "broadcast", NULL, NULL,
                    broadcast_get, broadcast_set);

static rcf_pch_cfg_object node_net_addr =
    { "net_addr", 0, &node_broadcast, &node_neigh_static,
      (rcf_ch_cfg_get)prefix_get, (rcf_ch_cfg_set)prefix_set,
      (rcf_ch_cfg_add)net_addr_add, (rcf_ch_cfg_del)net_addr_del,
      (rcf_ch_cfg_list)net_addr_list, NULL, NULL, NULL };

RCF_PCH_CFG_NODE_RW(node_iface_ip6_fw, "iface_ip6_fw", NULL, &node_net_addr,
                    iface_ip6_fw_get, iface_ip6_fw_set);

RCF_PCH_CFG_NODE_RW(node_iface_ip4_fw, "iface_ip4_fw", NULL,
                    &node_iface_ip6_fw, iface_ip4_fw_get, iface_ip4_fw_set);

RCF_PCH_CFG_NODE_RW(node_status, "status", NULL, &node_iface_ip4_fw,
                    status_get, status_set);

RCF_PCH_CFG_NODE_RO(node_oper_status, "oper_status", NULL, &node_status,
                    oper_status_get);

RCF_PCH_CFG_NODE_RW(node_mtu, "mtu", NULL, &node_oper_status,
                    mtu_get, mtu_set);

RCF_PCH_CFG_NODE_RO(node_bcast_link_addr, "bcast_link_addr", NULL,
                    &node_mtu, bcast_link_addr_get);

RCF_PCH_CFG_NODE_RO(node_link_addr, "link_addr", NULL,
                    &node_bcast_link_addr, link_addr_get);

RCF_PCH_CFG_NODE_RO(node_alias, "alias", NULL, &node_link_addr,
                    alias_get);

RCF_PCH_CFG_NODE_RO(node_ifindex, "index", NULL, &node_alias,
                    ifindex_get);

RCF_PCH_CFG_NODE_COLLECTION(node_interface, "interface",
                            &node_ifindex, NULL,
                            NULL, NULL, interface_list, NULL);

static rcf_pch_cfg_object node_route;

RCF_PCH_CFG_NODE_RWC(node_route_dev, "dev", NULL, NULL,
                     route_dev_get, route_dev_set, &node_route);

static rcf_pch_cfg_object node_route =
    { "route", 0, &node_route_dev, &node_interface,
      (rcf_ch_cfg_get)route_get, (rcf_ch_cfg_set)route_set,
      (rcf_ch_cfg_add)route_add, (rcf_ch_cfg_del)route_del,
      (rcf_ch_cfg_list)route_list, (rcf_ch_cfg_commit)route_commit,
      NULL, NULL };

RCF_PCH_CFG_NODE_RW_COLLECTION(node_env, "env", NULL, &node_route,
                               env_get, env_set, env_add, env_del,
                               env_list, NULL);

RCF_PCH_CFG_NODE_RO(node_uname_machine, "machine", NULL, NULL,
                    uname_machine_get);

RCF_PCH_CFG_NODE_RO(node_uname_release, "release", NULL,
                    &node_uname_machine, uname_release_get);

RCF_PCH_CFG_NODE_RO(node_uname_version, "version", NULL,
                    &node_uname_release, uname_version_get);

RCF_PCH_CFG_NODE_RO(node_uname, "uname", &node_uname_version, &node_env,
                    uname_get);

RCF_PCH_CFG_NODE_RO(node_tmp_dir, "tmp_dir", NULL, &node_uname,
                    agent_tmp_dir_get);

RCF_PCH_CFG_NODE_RO(node_dir, "dir", NULL, &node_tmp_dir,
                    agent_dir_get);

RCF_PCH_CFG_NODE_RO(node_platform, "platform", NULL, &node_dir,
                    agent_platform_get);

/* See the description in lib/rcfpch/rcf_ch_api.h */
int
rcf_ch_conf_init(void)
{
    static bool init = false;

    if (init)
        return 0;

    if (rcf_pch_add_node("/agent", &node_platform) != 0)
        return -1;

    rcf_pch_rsrc_init();
    rcf_pch_rsrc_info("/agent/interface",
                      rcf_pch_rsrc_grab_dummy,
                      rcf_pch_rsrc_release_dummy);

#ifdef RCF_RPC
    rcf_pch_rpc_init(ta_dir);
#endif

    init = true;
    return 0;
}

/* See the description in lib/rcfpch/rcf_ch_api.h */
const char *
rcf_ch_conf_agent(void)
{
    return ta_name;
}

/* See the description in lib/rcfpch/rcf_ch_api.h */
void
rcf_ch_conf_fini(void)
{
    ta_obj_cleanup();
}
