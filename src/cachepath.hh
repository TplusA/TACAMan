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

#ifndef CACHEPATH_HH
#define CACHEPATH_HH

#include <string>

/*!
 * \addtogroup cache
 */
/*!@{*/

namespace ArtCache
{

class Path
{
  private:
    std::string path_;
    bool is_file_;
    size_t dir_part_length_;

  public:
    Path(const Path &) = default;
    Path(Path &&) = default;
    Path &operator=(const Path &) = delete;

    explicit Path(const std::string &path):
        path_(path + '/'),
        is_file_(false),
        dir_part_length_(path_.length())
    {}

    explicit Path(const char *path):
        Path(std::string(path))
    {}

    const std::string &str() const { return path_; }

    std::string dirstr() const;

    Path &append_hash(const std::string &s, bool as_file = false);
    Path &append_hash(const char *s, bool as_file = false);

    Path &append_part(const std::string &s, bool as_file = false);
    Path &append_part(const char *s, bool as_file = false);

    bool exists() const;
};

}

/*!@}*/

#endif /* !CACHEPATH_HH */
