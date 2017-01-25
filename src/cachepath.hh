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
