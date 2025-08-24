/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2025 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief WiFi agent library - WPA supplicant/hostapd support
 *
 * The library provides ability to write & apply WPA supplicant configuration
 */

#define TE_LGR_USER "TA WiFi WPAS/HA"

#include "ta_wifi.h"
#include "ta_wifi_internal.h"
#include "ta_wifi_uci.h"
#include "te_str.h"
#include "logger_api.h"
#include "agentlib.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if defined(HAVE_SYS_STAT_H)
#include <sys/stat.h>
#endif

#if HAVE_FCNTL_H
#include <fcntl.h>
#endif

#if HAVE_SIGNAL_H
#include <signal.h>
#endif

/** WPA supplicant configuration format */
#define WPA_SUPPLICANT_CONF_FMT "/tmp/wpa_supplicant.%s.conf"
#define WPA_SUPPLICANT_PID_FMT "/var/run/wpa_supplicant.%s.pid"

#define FILE_PATH_SIZE  (200)
#define CMD_SIZE        (500)

/** WPA supplicant/hostapd configuration generation context */
typedef struct ta_wifi_wh_context {
    FILE         *f;    /**< File where to put the context data */
    ta_wifi_port *port; /**< WiFi port */
} ta_wifi_wh_context;

/* WPA supplicant/hostapd context initializer */
#define TA_WH_CONTEXT_INIT() { \
    NULL, NULL \
}

/* fprintf that would propagate error to TE */
#define CHECKED_FPRINTF(__x...) do {                                        \
        int ___retval = fprintf(__x);                                       \
        if (___retval < 0)                                                  \
        {                                                                   \
            int __err = errno;                                              \
            ERROR("Failed to write configuration: %s", strerror(__err));    \
            ret = TE_OS_RC(TE_TA_UNIX, __err);                              \
            goto err;                                                       \
        }                                                                   \
    } while (0)

/*
 * Convert channel to an actual frequency
 *
 * @param standard Target WiFi standard
 * @param ch       Target channel
 *
 * @return The frequency, or @c -1 in case of unspecified conversion.
 */
static int
wifi_channel_to_freq(ta_wifi_standard standard, unsigned ch)
{
    if (ch <= 0)
        return -1;

    switch (standard)
    {
        case TA_WIFI_STANDARD_G:
        case TA_WIFI_STANDARD_N:
        {
            if (ch >= 1 && ch <= 13)
                return 2412 + 5 * (ch - 1);
            if (ch == 14)
                return 2484;
            return -1;
        }

        case TA_WIFI_STANDARD_AC:
        case TA_WIFI_STANDARD_AX:
        {
            if (ch >= 7 && ch <= 196)
                return 5000 + 5 * ch;
            return -1;
        }
        case TA_WIFI_STANDARD_BE:
        {
            if (ch >= 1 && ch <= 233)
                return 5950 + 5 * ch;
            return -1;
        }
        default:
        {
            return -1;
        }
    }
}

/* Write out SSID to wpa supplicant configuration */
static te_errno
ta_wifi_wh_apply_wpas_ssid(ta_wifi_wh_context *ctx, ta_wifi_ssid *ssid)
{
    te_errno      ret;
    ta_wifi_port *port;
    int           freq;

    assert(ctx != NULL);
    assert(ctx->port != NULL);
    assert(ssid != NULL);
    assert(ssid->aname != NULL);

    port = ctx->port;

    CHECKED_FPRINTF(ctx->f, "network={\n");
    CHECKED_FPRINTF(ctx->f, "ssid=\"%s\"\n", ssid->aname);

    if (ssid->security != TA_WIFI_SECURITY_OPEN)
    {
        if (port->standard == TA_WIFI_STANDARD_BE)
        {
            CHECKED_FPRINTF(ctx->f, "key_mgmt=SAE\n");
            CHECKED_FPRINTF(ctx->f, "sae_password=\"%s\"\n", ssid->passphrase);
            CHECKED_FPRINTF(ctx->f, "ieee80211w=2\n");
        }
        else
        {
            CHECKED_FPRINTF(ctx->f, "key_mgmt=WPA-PSK\n");
            CHECKED_FPRINTF(ctx->f, "psk=\"%s\"\n", ssid->passphrase);
        }
    }
    else
    {
        CHECKED_FPRINTF(ctx->f, "key_mgmt=NONE\n");
    }

    CHECKED_FPRINTF(ctx->f, "scan_ssid=1\n");

    freq = wifi_channel_to_freq(port->standard, port->channel);
    if (freq != -1)
    {
        CHECKED_FPRINTF(ctx->f, "freq_list=%d\n", freq);
    }

    /* TODO: support HT modes */
    switch (port->standard)
    {
        case TA_WIFI_STANDARD_G:
        {
            if (freq == -1)
            {
                CHECKED_FPRINTF(ctx->f,
                    "freq_list=2412 2417 2422 2427 2432 2437 2442 "
                    "2447 2452 2457 2462 2467 2472\n");
            }
            CHECKED_FPRINTF(ctx->f, "disable_ht=1\n");
            CHECKED_FPRINTF(ctx->f, "disable_vht=1\n");
            break;
        }
        case TA_WIFI_STANDARD_N:
        {
            if (freq == -1)
            {
                CHECKED_FPRINTF(ctx->f,
                    "freq_list=2412 2417 2422 2427 2432 "
                    "2437 2442 2447 2452 2457 2462 2467 2472\n");
            }
            CHECKED_FPRINTF(ctx->f, "disable_ht=0\n");
            CHECKED_FPRINTF(ctx->f, "disable_vht=1\n");
            break;
        }
        case TA_WIFI_STANDARD_AC:
        {
            if (freq == -1)
            {
                CHECKED_FPRINTF(ctx->f, "freq_list=5180 5200 5220 5240 5260 "
                    "5280 5300 5320 5500 5520 5540 5560 5580 5600 5620 5640 "
                    "5660 5680 5700 5720 5745 5765 5785 5805 5825\n");
            }
            CHECKED_FPRINTF(ctx->f, "disable_ht=0\n");
            CHECKED_FPRINTF(ctx->f, "disable_vht=0\n");
            break;
        }
        case TA_WIFI_STANDARD_AX:
        {
            if (freq == -1)
            {
                CHECKED_FPRINTF(ctx->f, "freq_list=5180 5200 5220 5240 5260 "
                    "5280 5300 5320 5500 5520 5540 5560 5580 5600 5620 5640 "
                    "5660 5680 5700 5720 5745 5765 5785 5805 5825\n");
            }
            CHECKED_FPRINTF(ctx->f, "disable_ht=0\n");
            CHECKED_FPRINTF(ctx->f, "disable_vht=0\n");
            CHECKED_FPRINTF(ctx->f, "disable_he=0\n");
            CHECKED_FPRINTF(ctx->f, "disable_eht=1\n");
            break;
        }
        case TA_WIFI_STANDARD_BE:
        {
            if (freq == -1)
            {
                CHECKED_FPRINTF(ctx->f, "freq_list=5955 6115 6275 6435 6595 "
                    "6755 6915 7075\n");
            }
            CHECKED_FPRINTF(ctx->f, "disable_ht=0\n");
            CHECKED_FPRINTF(ctx->f, "disable_vht=0\n");
            CHECKED_FPRINTF(ctx->f, "disable_he=0\n");
            CHECKED_FPRINTF(ctx->f, "disable_eht=0\n");
            break;
        }
    }

    CHECKED_FPRINTF(ctx->f, "}\n");

    ret = 0;

err:
    return ret;
}

/* Kill the pid by given pid file */
static te_errno
try_kill_pid(const char *pid_path)
{
    FILE *f;
    int   pid;
    int   ret;

    assert(pid_path != NULL);

    f = fopen(pid_path, "r");
    if (f == NULL)
    {
        /* No pid file - no issue */
        return 0;
    }

    if (fscanf(f, "%d", &pid) != 1)
    {
        fclose(f);
        return TE_RC(TE_TA_UNIX, TE_EIO);
    }

    fclose(f);

    ret = kill(pid, SIGTERM);
    if (ret != 0)
        return TE_OS_RC(TE_TA_UNIX, errno);

    return 0;
}

/* Apply port's configuration and run necessary wpa_supplicant or hostapd */
static te_errno
ta_wifi_wh_apply_port(ta_wifi_wh_context *ctx, ta_wifi_port *port)
{
    te_errno      ret;
    char          conf_file[FILE_PATH_SIZE] = { 0 };
    char          pid_file[FILE_PATH_SIZE] = { 0 };
    char          cmd[CMD_SIZE] = { 0 };
    bool          found_wpas = false;
    bool          found_ha = false;
    ta_wifi_ssid *ssid;

    assert(port != NULL);

    if (port->ifname == NULL)
        return TE_RC(TE_TA_UNIX, TE_EINVAL);

    SLIST_FOREACH(ssid, &port->ssids, links)
    {
        assert(ssid != NULL);

        if (ssid->mode == TA_WIFI_MODE_STA)
        {
            found_wpas = true;
        }
        else if (ssid->mode == TA_WIFI_MODE_AP)
        {
            found_ha = true;
        }
    }

    if (!found_wpas && !found_ha)
        return TE_RC(TE_TA_UNIX, TE_ESKIP);

    if (found_wpas)
    {
        /* WPA supplicant configuration */
        ret = te_snprintf(conf_file,
                          sizeof(conf_file),
                          WPA_SUPPLICANT_CONF_FMT,
                          port->ifname);
        if (ret != 0)
            goto err;

        ret = te_snprintf(pid_file,
                          sizeof(pid_file),
                          WPA_SUPPLICANT_PID_FMT,
                          port->ifname);
        if (ret != 0)
            goto err;

        ctx->f = fopen(conf_file, "w");
        if (ctx->f == NULL)
            return TE_OS_RC(TE_TA_UNIX, errno);

        CHECKED_FPRINTF(ctx->f, "ctrl_interface=/var/run/wpa_supplicant\n");
        CHECKED_FPRINTF(ctx->f, "ap_scan=1\n");

        ctx->port = port;

        SLIST_FOREACH(ssid, &port->ssids, links)
        {
            assert(ssid != NULL);

            CHECKED_FPRINTF(ctx->f, "\n");
            if (ssid->mode == TA_WIFI_MODE_STA)
            {
                ret = ta_wifi_wh_apply_wpas_ssid(ctx, ssid);
                if (ret != 0)
                    goto err;
            }
        }

        if (ctx->f != NULL)
            fclose(ctx->f);
        ctx->f = NULL;

        try_kill_pid(pid_file);

        ret = te_snprintf(cmd, sizeof(cmd),
            "/usr/sbin/wpa_supplicant -i %s -B -c %s -P %s",
            port->ifname, conf_file, pid_file);
        if (ret != 0)
            goto err;

        if (ta_system(cmd) != 0)
        {
            ERROR("Failed to start WPA Supplicant");
            ret = TE_RC(TE_TA_UNIX, TE_ESHCMD);
            goto err;
        }
    }

err:
    if (ctx->f != NULL)
        fclose(ctx->f);
    ctx->f = NULL;

    return ret;
}

/* See the description in ta_wifi_wh.h */
te_errno
ta_wifi_wh_apply(ta_wifi *node)
{
    te_errno             ret;
    ta_wifi_wh_context   ctx = TA_WH_CONTEXT_INIT();
    ta_wifi_port        *port;

    assert(node != NULL);

    /* Apply ports */
    SLIST_FOREACH(port, &node->ports, links)
    {
        assert(port != NULL);

        ret = ta_wifi_wh_apply_port(&ctx, port);
        if (TE_RC_GET_ERROR(ret) == TE_ESKIP)
        {
            ret = 0;
            continue;
        }
        else if (ret != 0)
        {
            goto err;
        }
    }

    ret = 0;

err:

    return ret;
}

/* See the description in ta_wifi_wh.h */
te_errno
ta_wifi_wh_cancel(ta_wifi *node)
{
    int  ret;
    char pid_file[FILE_PATH_SIZE];
    ta_wifi_port *port;

    /* Kill all supplicants */

    SLIST_FOREACH(port, &node->ports, links)
    {
        ret = te_snprintf(pid_file,
                          sizeof(pid_file),
                          WPA_SUPPLICANT_PID_FMT,
                          port->ifname);
        if (ret != 0)
            return ret;

        try_kill_pid(pid_file);
    }

    return 0;
}
