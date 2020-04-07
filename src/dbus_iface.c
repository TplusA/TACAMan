/*
 * Copyright (C) 2017, 2020  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of TACAMan.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <errno.h>

#include "dbus_iface.h"
#include "dbus_iface_deep.h"
#include "dbus_handlers.h"
#include "messages.h"
#include "messages_dbus.h"

struct dbus_data
{
    guint owner_id;
    int name_acquired;

    void *handler_data;

    tdbusArtCacheRead *artcache_read_iface;
    tdbusArtCacheWrite *artcache_write_iface;
    tdbusArtCacheMonitor *artcache_monitor_iface;

    tdbusdebugLogging *debug_logging_iface;
    tdbusdebugLoggingConfig *debug_logging_config_proxy;
};

static bool handle_dbus_error(GError **error)
{
    if(*error == NULL)
        return true;

    if((*error)->message != NULL)
        msg_error(0, LOG_EMERG, "Got D-Bus error: %s", (*error)->message);
    else
        msg_error(0, LOG_EMERG, "Got D-Bus error without any message");

    g_error_free(*error);
    *error = NULL;

    return false;
}

static void try_export_iface(GDBusConnection *connection,
                             GDBusInterfaceSkeleton *iface)
{
    GError *error = NULL;

    g_dbus_interface_skeleton_export(iface, connection, "/de/tahifi/TACAMan", &error);

    handle_dbus_error(&error);
}

static void bus_acquired(GDBusConnection *connection,
                         const gchar *name, gpointer user_data)
{
    struct dbus_data *data = user_data;

    msg_info("D-Bus \"%s\" acquired", name);

    data->artcache_read_iface = tdbus_art_cache_read_skeleton_new();
    data->artcache_write_iface = tdbus_art_cache_write_skeleton_new();
    data->artcache_monitor_iface = tdbus_art_cache_monitor_skeleton_new();
    data->debug_logging_iface = tdbus_debug_logging_skeleton_new();

    g_signal_connect(data->artcache_read_iface, "handle-get-scaled-image-data",
                     G_CALLBACK(dbusmethod_cache_get_scaled_image), data->handler_data);

    g_signal_connect(data->artcache_write_iface, "handle-add-image-by-uri",
                     G_CALLBACK(dbusmethod_cache_add_by_uri), data->handler_data);
    g_signal_connect(data->artcache_write_iface, "handle-add-image-by-data",
                     G_CALLBACK(dbusmethod_cache_add_by_data), data->handler_data);

    g_signal_connect(data->debug_logging_iface,
                     "handle-debug-level",
                     G_CALLBACK(msg_dbus_handle_debug_level), NULL);

    try_export_iface(connection, G_DBUS_INTERFACE_SKELETON(data->artcache_read_iface));
    try_export_iface(connection, G_DBUS_INTERFACE_SKELETON(data->artcache_write_iface));
    try_export_iface(connection, G_DBUS_INTERFACE_SKELETON(data->artcache_monitor_iface));
    try_export_iface(connection, G_DBUS_INTERFACE_SKELETON(data->debug_logging_iface));
}

static void created_debug_config_proxy(GObject *source_object, GAsyncResult *res,
                                       gpointer user_data)
{
    struct dbus_data *data = user_data;
    GError *error = NULL;

    data->debug_logging_config_proxy =
        tdbus_debug_logging_config_proxy_new_finish(res, &error);

    if(handle_dbus_error(&error))
        g_signal_connect(data->debug_logging_config_proxy, "g-signal",
                         G_CALLBACK(msg_dbus_handle_global_debug_level_changed),
                         NULL);
}

static void connect_signals_debug(GDBusConnection *connection,
                                  struct dbus_data *data, GDBusProxyFlags flags,
                                  const char *bus_name, const char *object_path)
{
    GError *error = NULL;

    data->debug_logging_config_proxy = NULL;
    tdbus_debug_logging_config_proxy_new(connection, flags,
                                         bus_name, object_path, NULL,
                                         created_debug_config_proxy, data);
    handle_dbus_error(&error);
}

static void name_acquired(GDBusConnection *connection,
                          const gchar *name, gpointer user_data)
{
    struct dbus_data *data = user_data;

    msg_info("D-Bus name \"%s\" acquired", name);
    data->name_acquired = 1;

    connect_signals_debug(connection, data, G_DBUS_PROXY_FLAGS_NONE,
                          "de.tahifi.Dcpd", "/de/tahifi/Dcpd");
}

static void name_lost(GDBusConnection *connection,
                      const gchar *name, gpointer user_data)
{
    struct dbus_data *data = user_data;

    msg_vinfo(MESSAGE_LEVEL_IMPORTANT, "D-Bus name \"%s\" lost", name);
    data->name_acquired = -1;
}

static void destroy_notification(gpointer data)
{
    msg_vinfo(MESSAGE_LEVEL_IMPORTANT, "Bus destroyed.");
}

static struct dbus_data dbus_data;

int dbus_setup(GMainLoop *loop, bool connect_to_session_bus,
               void *dbus_signal_data_for_dbus_handlers)
{
#if !GLIB_CHECK_VERSION(2, 36, 0)
    g_type_init();
#endif

    memset(&dbus_data, 0, sizeof(dbus_data));

    GBusType bus_type =
        connect_to_session_bus ? G_BUS_TYPE_SESSION : G_BUS_TYPE_SYSTEM;

    static const char bus_name[] = "de.tahifi.TACAMan";

    dbus_data.handler_data = dbus_signal_data_for_dbus_handlers;
    dbus_data.owner_id =
        g_bus_own_name(bus_type, bus_name, G_BUS_NAME_OWNER_FLAGS_NONE,
                       bus_acquired, name_acquired, name_lost, &dbus_data,
                       destroy_notification);

    while(dbus_data.name_acquired == 0)
    {
        /* do whatever has to be done behind the scenes until one of the
         * guaranteed callbacks gets called */
        g_main_context_iteration(NULL, TRUE);
    }

    if(dbus_data.name_acquired < 0)
    {
        msg_error(0, LOG_EMERG, "Failed acquiring D-Bus name");
        return -1;
    }

    log_assert(dbus_data.artcache_read_iface != NULL);
    log_assert(dbus_data.artcache_write_iface != NULL);
    log_assert(dbus_data.artcache_monitor_iface != NULL);
    log_assert(dbus_data.debug_logging_iface != NULL);

    g_main_loop_ref(loop);

    return 0;
}

void dbus_shutdown(GMainLoop *loop)
{
    if(loop == NULL)
        return;

    g_bus_unown_name(dbus_data.owner_id);

    g_object_unref(dbus_data.artcache_read_iface);
    g_object_unref(dbus_data.artcache_write_iface);
    g_object_unref(dbus_data.artcache_monitor_iface);
    g_object_unref(dbus_data.debug_logging_iface);

    if(dbus_data.debug_logging_config_proxy != NULL)
        g_object_unref(dbus_data.debug_logging_config_proxy);

    g_main_loop_unref(loop);
}

tdbusArtCacheMonitor *dbus_get_artcache_monitor_iface(void)
{
    return dbus_data.artcache_monitor_iface;
}
