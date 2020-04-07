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

#ifndef PENDING_HH
#define PENDING_HH

#include <vector>
#include <string>

namespace ArtCache
{

struct StreamPrioPair;
class Manager;

enum class AddKeyResult
{
    NOT_CHANGED,
    INSERTED,
    REPLACED,
    SOURCE_PENDING,
    SOURCE_UNKNOWN,
    IO_ERROR,
    DISK_FULL,
    INTERNAL_ERROR,
};

class PendingIface
{
  protected:
    explicit PendingIface() {}

  public:
    PendingIface(const PendingIface &) = delete;
    PendingIface &operator=(const PendingIface &) = delete;

    virtual ~PendingIface() {}

    virtual bool is_source_pending(const std::string &source_hash,
                                   bool exclude_current = false) const = 0;
    virtual bool is_source_pending__unlocked(const std::string &source_hash,
                                             bool exclude_current = false) const = 0;
    virtual bool add_key_to_pending_source(const ArtCache::StreamPrioPair &stream_key,
                                           const std::string &source_hash) = 0;
    virtual void notify_pending_key_processed(const ArtCache::StreamPrioPair &stream_key,
                                              const std::string &source_hash,
                                              ArtCache::AddKeyResult result,
                                              Manager &cache_manager) = 0;
};

}

#endif /* !PENDING_HH */
