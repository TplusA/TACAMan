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

#ifndef CACHETYPES_HH
#define CACHETYPES_HH

#include <string>
#include <vector>

/*!
 * \addtogroup cache
 */
/*!@{*/

namespace ArtCache
{

struct StreamPrioPair
{
  public:
    std::string stream_key_;
    uint8_t priority_;

    StreamPrioPair(const StreamPrioPair &) = delete;
    StreamPrioPair(StreamPrioPair &&) = default;
    StreamPrioPair &operator=(const StreamPrioPair &) = delete;

    explicit StreamPrioPair(const std::string &stream_key, uint8_t priority):
        stream_key_(stream_key),
        priority_(priority)
    {}

    explicit StreamPrioPair(std::string &&stream_key, uint8_t priority):
        stream_key_(std::move(stream_key)),
        priority_(priority)
    {}
};

class Object
{
  public:
    const uint8_t priority_;
    const std::string hash_;

  private:
    std::vector<uint8_t> data_;

  public:
    Object(const Object &) = delete;
    Object &operator=(const Object &) = delete;

    explicit Object():
        priority_(UINT8_MAX)
    {}

    explicit Object(uint8_t priority, const std::string &hash):
        priority_(priority),
        hash_(hash)
    {}

    explicit Object(uint8_t priority, const std::string &hash,
                    const uint8_t *objdata, size_t length);

    explicit Object(uint8_t priority, std::string &&hash,
                    const uint8_t *objdata, size_t length);

    const std::vector<uint8_t> data() const { return data_; }
};

}

/*!@}*/

#endif /* !CACHETYPES_HH */
