/*
 * Copyright (C) 2017, 2020  T+A elektroakustik GmbH & Co. KG
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

#ifndef DBUS_HANDLERS_HH
#define DBUS_HANDLERS_HH

#include "artcache.hh"
#include "converterqueue.hh"

/*!
 * \addtogroup dbus_handlers DBus handlers for signals
 * \ingroup dbus
 */
/*!@{*/

namespace DBus
{

/*!
 * Data used in several D-Bus signal handlers.
 */
class SignalData
{
  public:
    SignalData(const SignalData &) = delete;
    SignalData &operator=(const SignalData &) = delete;
    SignalData(SignalData &&) = default;

    Converter::Queue &image_converter_queue_;
    ArtCache::Manager &cache_manager_;

    explicit SignalData(Converter::Queue &image_converter_queue,
                        ArtCache::Manager &cache_manager):
        image_converter_queue_(image_converter_queue),
        cache_manager_(cache_manager)
    {}
};

void binary_to_hexstring(std::string &dest, const uint8_t *data, size_t len);
void hexstring_to_binary(std::uint8_t *dest, const std::string &str);

#ifdef GLIB_CHECK_VERSION
GVariant *hexstring_to_variant(const std::string &str);
#endif /* GLIB_CHECK_VERSION */

}

/*!@}*/

#endif /* !DBUS_HANDLERS_HH */
