/*
 * Copyright (C) 2017  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of TACAMan.
 *
 * TACAMan is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3 as
 * published by the Free Software Foundation.
 *
 * TACAMan is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TACAMan.  If not, see <http://www.gnu.org/licenses/>.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cstring>
#include <cerrno>

#include "dbus_handlers.h"
#include "dbus_handlers.hh"
#include "de_tahifi_artcache_errors.hh"
#include "md5.hh"
#include "messages.h"

static char nibble_to_char(const uint8_t &ch)
{
    static const char chars[] =
    {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    static_assert(sizeof(chars) == 16, "Wrong table size");

    return chars[ch & 0x0f];
}

static uint8_t char_to_nibble(const char &ch)
{
    return (ch >= '0' && ch <= '9'
            ? ch - '0'
            : (ch >= 'a' && ch <= 'f'
               ? ch - 'a' + 10
               : 0));
}

void DBus::binary_to_hexstring(std::string &dest,
                               const uint8_t *data, size_t len)
{
    log_assert(data != nullptr);
    log_assert(len > 0);

    dest.reserve(len * 2);

    for(size_t i = 0; i < len; ++i)
    {
        dest.push_back(nibble_to_char(data[i] >> 4));
        dest.push_back(nibble_to_char(data[i]));
    }
}

void DBus::hexstring_to_binary(std::uint8_t *dest, const std::string &str)
{
    log_assert(dest != nullptr);

    const size_t len = str.size() / 2;

    for(size_t i = 0; i < len; ++i)
        dest[i] = char_to_nibble(str[2 * i + 0]) << 4 |
                  char_to_nibble(str[2 * i + 1]);
}

GVariant *DBus::hexstring_to_variant(const std::string &str)
{
    if(str.size() >= 2)
    {
        uint8_t buffer[str.size() / 2];
        hexstring_to_binary(buffer, str);
        return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                         buffer, sizeof(buffer), sizeof(buffer[0]));
    }
    else
    {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("ay"));
        return g_variant_builder_end(&builder);
    }
}

static bool check_priority(GDBusMethodInvocation *invocation,
                           guchar image_priority)
{
    if(image_priority > 0)
        return true;

    g_dbus_method_invocation_return_error_literal(invocation,
                                                  G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                                  "Priority must be positive");

    return false;
}

static bool check_hash_param(GDBusMethodInvocation *invocation,
                             GVariant *stream_key,
                             size_t minimum_length, size_t maximum_length,
                             bool is_empty_allowed, const char *what,
                             gconstpointer &stream_key_bytes,
                             gsize &stream_key_length)
{
    stream_key_bytes = nullptr;
    stream_key_length = 0;

    gsize len;
    gconstpointer key = g_variant_get_fixed_array(stream_key, &len,
                                                  sizeof(uint8_t));

    if(len < minimum_length && (len > 0 || !is_empty_allowed))
    {
        g_dbus_method_invocation_return_error(invocation,
                                              G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                              "%s too short", what);
        return false;
    }

    if(len > maximum_length)
    {
        g_dbus_method_invocation_return_error(invocation,
                                              G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                              "%s too long", what);
        return false;
    }

    stream_key_bytes = key;
    stream_key_length = len;

    return true;
}

static inline bool check_key_param(GDBusMethodInvocation *invocation,
                                   GVariant *stream_key,
                                   gconstpointer &stream_key_bytes,
                                   gsize &stream_key_length)
{
    return check_hash_param(invocation, stream_key,
                            2, SIZE_MAX, false,
                            "Stream key",
                            stream_key_bytes, stream_key_length);
}

static inline bool check_object_hash_param(GDBusMethodInvocation *invocation,
                                           GVariant *stream_key,
                                           gconstpointer &stream_key_bytes,
                                           gsize &stream_key_length)
{
    return check_hash_param(invocation, stream_key,
                            sizeof(MD5::Hash), sizeof(MD5::Hash), true,
                            "Object hash",
                            stream_key_bytes, stream_key_length);
}

static void enter_artcache_read_handler(GDBusMethodInvocation *invocation)
{
    static const char iface_name[] = "de.tahifi.ArtCache.Read";

    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s method invocation from '%s': %s",
              iface_name, g_dbus_method_invocation_get_sender(invocation),
              g_dbus_method_invocation_get_method_name(invocation));
}

gboolean dbusmethod_cache_get_scaled_image(tdbusArtCacheRead *object,
                                           GDBusMethodInvocation *invocation,
                                           GVariant *stream_key, const char *format,
                                           GVariant *hash, gpointer user_data)
{
    enter_artcache_read_handler(invocation);

    gconstpointer stream_key_bytes;
    gsize stream_key_length;

    if(!check_key_param(invocation, stream_key,
                        stream_key_bytes, stream_key_length))
        return TRUE;

    gconstpointer object_hash_bytes;
    gsize object_hash_length;

    if(!check_object_hash_param(invocation, hash,
                                object_hash_bytes, object_hash_length))
        return TRUE;

    /* parameters seem to be OK, convert BLOBs to strings and attempt to lookup
     * the requested image */
    auto *data = static_cast<DBus::SignalData *>(user_data);
    log_assert(data != nullptr);

    std::string key_string;
    DBus::binary_to_hexstring(key_string,
                              static_cast<const uint8_t *>(stream_key_bytes),
                              stream_key_length);

    std::string object_hash_string;

    if(object_hash_length > 0)
        DBus::binary_to_hexstring(object_hash_string,
                                  static_cast<const uint8_t *>(object_hash_bytes),
                                  object_hash_length);

    auto error_code = ArtCache::ReadError::Code::INTERNAL;
    guchar priority = 0;

    std::unique_ptr<ArtCache::Object> obj;

    switch(data->cache_manager_.lookup(key_string, object_hash_string,
                                       format, obj))
    {
      case ArtCache::LookupResult::FOUND:
        log_assert(obj != nullptr);
        error_code = (obj->data().empty()
                      ? ArtCache::ReadError::Code::OK
                      : ArtCache::ReadError::Code::UNCACHED);
        priority = obj->priority_;
        break;

      case ArtCache::LookupResult::KEY_UNKNOWN:
        log_assert(obj == nullptr);
        error_code = ArtCache::ReadError::Code::KEY_UNKNOWN;
        break;

      case ArtCache::LookupResult::PENDING:
        log_assert(obj == nullptr);
        error_code = ArtCache::ReadError::Code::BUSY;
        break;

      case ArtCache::LookupResult::FORMAT_NOT_SUPPORTED:
        log_assert(obj == nullptr);
        error_code = ArtCache::ReadError::Code::FORMAT_NOT_SUPPORTED;
        break;

      case ArtCache::LookupResult::ORPHANED:
        log_assert(obj == nullptr);
        msg_info("Orphaned key %s", key_string.c_str());
        error_code = ArtCache::ReadError::Code::KEY_UNKNOWN;
        break;

      case ArtCache::LookupResult::IO_ERROR:
        log_assert(obj == nullptr);
        error_code = ArtCache::ReadError::Code::IO_FAILURE;
        break;
    }

    GVariant *hash_variant;
    GVariant *data_variant;

    if(obj == nullptr || obj->data().empty())
    {
        static const std::string empty;
        hash_variant = DBus::hexstring_to_variant(empty);
        data_variant = DBus::hexstring_to_variant(empty);
    }
    else
    {
        log_assert(!obj->hash_.empty());

        hash_variant = DBus::hexstring_to_variant(obj->hash_);
        data_variant = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                                 obj->data().data(),
                                                 obj->data().size(),
                                                 sizeof(uint8_t));
    }

    tdbus_art_cache_read_complete_get_scaled_image_data(object, invocation,
                                                        error_code, priority,
                                                        hash_variant,
                                                        data_variant);

    return TRUE;
}

static void enter_artcache_write_handler(GDBusMethodInvocation *invocation)
{
    static const char iface_name[] = "de.tahifi.ArtCache.Write";

    msg_vinfo(MESSAGE_LEVEL_TRACE, "%s method invocation from '%s': %s",
              iface_name, g_dbus_method_invocation_get_sender(invocation),
              g_dbus_method_invocation_get_method_name(invocation));
}

gboolean dbusmethod_cache_add_by_uri(tdbusArtCacheWrite *object,
                                     GDBusMethodInvocation *invocation,
                                     GVariant *stream_key,
                                     guchar image_priority,
                                     const gchar *image_uri,
                                     gpointer user_data)
{
    enter_artcache_write_handler(invocation);

    if(image_uri == nullptr || image_uri[0] == '\0')
    {
        g_dbus_method_invocation_return_error_literal(invocation,
                                                      G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                                      "Empty URI");
        return TRUE;
    }

    gconstpointer stream_key_bytes;
    gsize stream_key_length;

    if(!check_priority(invocation, image_priority) ||
       !check_key_param(invocation, stream_key,
                        stream_key_bytes, stream_key_length))
        return TRUE;

    tdbus_art_cache_write_complete_add_image_by_uri(object, invocation);

    auto *data = static_cast<DBus::SignalData *>(user_data);
    log_assert(data != nullptr);

    std::string key;
    DBus::binary_to_hexstring(key,
                              static_cast<const uint8_t *>(stream_key_bytes),
                              stream_key_length);

    data->image_converter_queue_.add_to_cache_by_uri(
        data->cache_manager_,
        std::move(ArtCache::StreamPrioPair(std::move(key), image_priority)),
        image_uri);

    return TRUE;
}

gboolean dbusmethod_cache_add_by_data(tdbusArtCacheWrite *object,
                                      GDBusMethodInvocation *invocation,
                                      GVariant *stream_key,
                                      guchar image_priority,
                                      GVariant *image_data,
                                      gpointer user_data)
{
    enter_artcache_write_handler(invocation);

    gsize image_length;
    gconstpointer image_bytes =
        g_variant_get_fixed_array(image_data, &image_length, sizeof(uint8_t));

    if(image_length == 0)
    {
        g_dbus_method_invocation_return_error_literal(invocation,
                                                      G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                                      "Empty data");
        return TRUE;
    }

    gconstpointer stream_key_bytes;
    gsize stream_key_length;

    if(!check_priority(invocation, image_priority) ||
       !check_key_param(invocation, stream_key,
                        stream_key_bytes, stream_key_length))
        return TRUE;

    tdbus_art_cache_write_complete_add_image_by_data(object, invocation);

    auto *data = static_cast<DBus::SignalData *>(user_data);
    log_assert(data != nullptr);

    std::string key;
    DBus::binary_to_hexstring(key,
                              static_cast<const uint8_t *>(stream_key_bytes),
                              stream_key_length);

    data->image_converter_queue_.add_to_cache_by_data(
        data->cache_manager_,
        std::move(ArtCache::StreamPrioPair(std::move(key), image_priority)),
        static_cast<const uint8_t *>(image_bytes),
        image_length);

    return TRUE;
}
