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

#include "cachepath.hh"
#include "os.hh"
#include "messages.h"

std::string ArtCache::Path::dirstr() const
{
    if(is_file_)
        return path_.substr(0, dir_part_length_);
    else
        return path_;
}

ArtCache::Path &ArtCache::Path::append_hash(const std::string &s, bool as_file)
{
    if(is_file_)
    {
        BUG("Cannot append hash to file name");
        return *this;
    }

    if(s.length() < 3)
    {
        if(s.empty())
            BUG("Cannot append empty hash to path");
        else
            BUG("Hash \"%s\" too short", s.c_str());

        return *this;
    }

    std::copy(s.begin(), s.begin() + 2, std::back_inserter(path_));
    path_ += '/';

    std::copy(s.begin() + 2, s.end(), std::back_inserter(path_));

    if(!as_file)
    {
        path_ += '/';
        dir_part_length_ = path_.length();
    }
    else
    {
        is_file_ = true;
        dir_part_length_ += 3;
    }

    return *this;
}

ArtCache::Path &ArtCache::Path::append_hash(const char *s, bool as_file)
{
    log_assert(s != nullptr);

    if(is_file_)
    {
        BUG("Cannot append hash to file name");
        return *this;
    }

    if(s[0] == '\0')
    {
        BUG("Cannot append empty hash to path");
        return *this;
    }

    if(s[1] == '\0' || s[2] == '\0')
    {
        BUG("Hash too short");
        return *this;
    }

    std::copy(s, s + 2, std::back_inserter(path_));
    path_ += '/';

    s += 2;
    const size_t slen(strlen(s));
    std::copy(s, s + slen, std::back_inserter(path_));

    if(!as_file)
    {
        path_ += '/';
        dir_part_length_ = path_.length();
    }
    else
    {
        is_file_ = true;
        dir_part_length_ += 3;
    }

    return *this;
}

ArtCache::Path &ArtCache::Path::append_part(const std::string &s, bool as_file)
{
    if(is_file_)
    {
        BUG("Cannot append part to file name");
        return *this;
    }

    if(s.empty())
    {
        BUG("Cannot append empty part to path");
        return *this;
    }

    path_ += s;

    if(!as_file)
    {
        path_ += '/';
        dir_part_length_ = path_.length();
    }
    else
        is_file_ = true;

    return *this;
}

ArtCache::Path &ArtCache::Path::append_part(const char *s, bool as_file)
{
    log_assert(s != nullptr);

    if(is_file_)
    {
        BUG("Cannot append part to file name");
        return *this;
    }

    if(s[0] == '\0')
    {
        BUG("Cannot append empty part to path");
        return *this;
    }

    path_ += s;

    if(!as_file)
    {
        path_ += '/';
        dir_part_length_ = path_.length();
    }
    else
        is_file_ = true;

    return *this;
}

bool ArtCache::Path::exists() const
{
    OS::SuppressErrorsGuard suppress_errors;

    switch(os_path_get_type(path_.c_str()))
    {
      case OS_PATH_TYPE_DIRECTORY:
        return !is_file_;

      case OS_PATH_TYPE_IO_ERROR:
        return false;

      case OS_PATH_TYPE_FILE:
        return is_file_;

      case OS_PATH_TYPE_OTHER:
        break;
    }

    BUG("Unexpected type of path %s", path_.c_str());

    return false;
}
