/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2025 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief WiFi agent library
 *
 * Publicly available WiFi agent library functions and helpers.
 */

#ifndef __TA_WIFI_DEF_H__
#define __TA_WIFI_DEF_H__

#include "te_config.h"
#include "te_defs.h"
#include "config.h"
#include "rcf_common.h"

#if HAVE_SYS_QUEUE_H
#include <sys/queue.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Supported WiFi configurators */
typedef enum ta_wifi_configurator {
    TA_WIFI_CFG_AUTO = 0,                   /**< Automatic detection of
                                                 configurator */
    TA_WIFI_CFG_HOSTAPD_WPA_SUPPLICANT,     /**< hostapd/wpa_supplicant
                                                 configurator */
    TA_WIFI_CFG_UCI,                        /**< UCI configurator */
} ta_wifi_configurator;

/** Supported WiFi standards */
typedef enum ta_wifi_standard {
    TA_WIFI_STANDARD_G = 0,             /**< G standard (2.4GHz) */
    TA_WIFI_STANDARD_N,                 /**< N standard (2.4GHz) */
    TA_WIFI_STANDARD_AC,                /**< AC standard (5GHz) */
    TA_WIFI_STANDARD_AX,                /**< AX standard (2.4/5GHz) */
    TA_WIFI_STANDARD_BE,                /**< BE standard (2.4/5/6GHz) */
} ta_wifi_standard;

/** Supported WiFi bandwidths */
typedef enum ta_wifi_width {
    TA_WIFI_WIDTH_NOT_SET = 0,              /**< Not set */
    TA_WIFI_WIDTH_20 = 20,                  /**< 20 MHz */
    TA_WIFI_WIDTH_40 = 40,                  /**< 40 MHz */
    TA_WIFI_WIDTH_80 = 80,                  /**< 80 MHz */
    TA_WIFI_WIDTH_160 = 160,                /**< 160 MHz */
    TA_WIFI_WIDTH_320 = 320,                /**< 320 MHz */
} ta_wifi_width;

/** Supported WiFi modes */
typedef enum ta_wifi_mode {
    TA_WIFI_MODE_AP = 0,                /**< Access point mode */
    TA_WIFI_MODE_STA,                   /**< STA mode */
} ta_wifi_mode;

/** Supported WiFi security */
typedef enum ta_wifi_security {
    TA_WIFI_SECURITY_OPEN = 0,          /**< No security (no password) */
    TA_WIFI_SECURITY_WEP,               /**< WEP security */
    TA_WIFI_SECURITY_WPA,               /**< WPA security */
    TA_WIFI_SECURITY_WPA2,              /**< WPA2 security */
    TA_WIFI_SECURITY_WPA3,              /**< WPA3 security */
} ta_wifi_security;

/** Supported WiFi protocols */
typedef enum ta_wifi_protocol {
    TA_WIFI_PROTOCOL_CCMP = 0,          /**< CCMP protocol */
    TA_WIFI_PROTOCOL_TKIP,              /**< TKIP protocol */
} ta_wifi_protocol;

/** Mapping of supported WiFi configurators */
static const te_enum_map wifi_configurator_mapping[] = {
    { .name = "auto", .value = TA_WIFI_CFG_AUTO },
    { .name = "hostapd_wpa_supplicant",
        .value = TA_WIFI_CFG_HOSTAPD_WPA_SUPPLICANT },
    { .name = "uci", .value = TA_WIFI_CFG_UCI },
    TE_ENUM_MAP_END
};

/** Mapping of supported WiFi standards */
static const te_enum_map wifi_standard_mapping[] = {
    { .name = "g", .value = TA_WIFI_STANDARD_G },
    { .name = "n", .value = TA_WIFI_STANDARD_N },
    { .name = "ac", .value = TA_WIFI_STANDARD_AC },
    { .name = "ax", .value = TA_WIFI_STANDARD_AX },
    TE_ENUM_MAP_END
};

/** Mapping of supported widths */
static const te_enum_map wifi_width_mapping[] = {
    { .name = "0", .value = TA_WIFI_WIDTH_NOT_SET },
    { .name = "20", .value = TA_WIFI_WIDTH_20 },
    { .name = "40", .value = TA_WIFI_WIDTH_40 },
    { .name = "80", .value = TA_WIFI_WIDTH_80 },
    { .name = "160", .value = TA_WIFI_WIDTH_160 },
    { .name = "320", .value = TA_WIFI_WIDTH_320 },
    TE_ENUM_MAP_END
};

/** Mapping of supported WiFi security */
static const te_enum_map wifi_security_mapping[] = {
    { .name = "open", .value = TA_WIFI_SECURITY_OPEN },
    { .name = "wep", .value = TA_WIFI_SECURITY_WEP },
    { .name = "wpa", .value = TA_WIFI_SECURITY_WPA },
    { .name = "wpa2", .value = TA_WIFI_SECURITY_WPA2 },
    { .name = "wpa3", .value = TA_WIFI_SECURITY_WPA3 },
    TE_ENUM_MAP_END
};

/** Mapping of supported WiFi modes */
static const te_enum_map wifi_mode_mapping[] = {
    { .name = "ap", .value = TA_WIFI_MODE_AP },
    { .name = "sta", .value = TA_WIFI_MODE_STA },
    TE_ENUM_MAP_END
};

/** Mapping of supported WiFi protocols */
static const te_enum_map wifi_protocol_mapping[] = {
    { .name = "ccmp", .value = TA_WIFI_PROTOCOL_CCMP },
    { .name = "tkip", .value = TA_WIFI_PROTOCOL_TKIP },
    TE_ENUM_MAP_END
};

/**
 * Get string representation of the configurator
 *
 * @param value     Numeric representation of the configurator
 *
 * @return The pointer to the string representation
 */
static const char *ta_wifi_configurator_from_value(ta_wifi_mode value)
{
    return te_enum_map_from_value(wifi_configurator_mapping, value);
}

/**
 * Convert string representation of the configurator to the numeric one
 *
 * @param str       Pointer to string representation
 *
 * @return The security mode of @p ta_wifi_configurator enumeration
 */
static ta_wifi_mode ta_wifi_configurator_from_str(const char *str)
{
    return te_enum_map_from_str(wifi_configurator_mapping, str, -1);
}

/**
 * Get string representation of the WiFi standard
 *
 * @param value     Numeric representation of the standard
 *
 * @return The pointer to the string representation
 */
static const char *ta_wifi_standard_from_value(ta_wifi_standard value)
{
    return te_enum_map_from_value(vwifi_standard_mapping, alue);
}

/**
 * Convert string representation of the standard to a numeric one
 *
 * @param str       Pointer to string representation
 *
 * @return The WiFi standard of @p ta_wifi_standard enumeration
 */
static ta_wifi_standard ta_wifi_standard_from_str(const char *str)
{
    return te_enum_map_from_str(wifi_standard_mapping, str, -1);
}

/**
 * Get string representation of the band width
 *
 * @param value     Numeric representation of the width
 *
 * @return The pointer to the string representation
 */
static const char *ta_wifi_width_from_value(ta_wifi_width value)
{
    return te_enum_map_from_value(wifi_width_mapping, value);
}

/**
 * Convert string representation of the width to a numeric one
 *
 * @param str       Pointer to string representation
 *
 * @return The band width of @p ta_wifi_width enumeration
 */
static ta_wifi_standard ta_wifi_width_from_str(const char *str)
{
    return te_enum_map_from_str(wifi_width_mapping, str, -1);
}

/**
 * Get string representation of the WiFi operation mode
 *
 * @param value     Numeric representation of the mode
 *
 * @return The pointer to the string representation
 */
static const char *ta_wifi_mode_from_value(ta_wifi_mode value)
{
    return te_enum_map_from_value(wifi_mode_mapping, value);
}

/**
 * Convert string representation of the operation mode to the numeric one
 *
 * @param str       Pointer to string representation
 *
 * @return The security mode of @p ta_wifi_mode enumeration
 */
static ta_wifi_mode ta_wifi_mode_from_str(const char *str)
{
    return te_enum_map_from_str(wifi_mode_mapping, str, -1);
}

/**
 * Get string representation of the WiFi security mode
 *
 * @param value     Numeric representation of the security mode
 *
 * @return The pointer to the string representation
 */
static const char *ta_wifi_security_from_value(ta_wifi_security value)
{
    return te_enum_map_from_value(wifi_security_mapping, value);
}

/**
 * Convert string representation of the security mode to a numeric one
 *
 * @param str       Pointer to string representation
 *
 * @return The security mode of @p ta_wifi_security enumeration
 */
static ta_wifi_security ta_wifi_security_from_str(const char *str)
{
    return te_enum_map_from_str(wifi_security_mapping, str, -1);
}

/**
 * Get string representation of the protocol
 *
 * @param value     Numeric representation of the protocol
 *
 * @return The pointer to the string representation
 */
static const char *ta_wifi_protocol_from_value(ta_wifi_mode value)
{
    return te_enum_map_from_value(wifi_protocol_mapping, value);
}

/**
 * Convert string representation of the protocol to the numeric one
 *
 * @param str       Pointer to string representation
 *
 * @return The security mode of @p ta_wifi_protocol enumeration
 */
static ta_wifi_mode ta_wifi_protocol_from_str(const char *str)
{
    return te_enum_map_from_str(wifi_protocol_mapping, str, -1);
}

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TA_WIFI_DEF_H__ */
