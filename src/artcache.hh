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

#ifndef ARTCACHE_HH
#define ARTCACHE_HH

#include <mutex>
#include <memory>

#include "cachetypes.hh"
#include "cachepath.hh"
#include "pending.hh"
#include "md5.hh"

/*!
 * \addtogroup cache Cover Art cache management
 */
/*!@{*/

namespace ArtCache
{

bool is_valid_hash(const char *str);
bool is_valid_hash(const char *str, size_t len);

enum class AddSourceResult
{
    NOT_CHANGED,
    INSERTED,
    EMPTY,
    IO_ERROR,
    DISK_FULL,
    INTERNAL_ERROR,
};

enum class UpdateSourceResult
{
    NOT_CHANGED,
    UPDATED_SOURCE_ONLY,
    UPDATED_KEYS_ONLY,
    UPDATED_ALL,
    IO_ERROR,
    DISK_FULL,
    INTERNAL_ERROR,
};

enum class AddObjectResult
{
    EXISTS,
    INSERTED,
    IO_ERROR,
    DISK_FULL,
    INTERNAL_ERROR,
};

enum class LookupResult
{
    FOUND,
    KEY_UNKNOWN,
    PENDING,
    FORMAT_NOT_SUPPORTED,
    ORPHANED,
    IO_ERROR,
};

enum class GCResult
{
    NOT_REQUIRED,
    NOT_POSSIBLE,
    DEFLATED,
    IO_ERROR,
};

class Statistics
{
  public:
    size_t number_of_stream_keys_;
    size_t number_of_sources_;
    size_t number_of_objects_;

    Statistics(const Statistics &) = delete;
    Statistics &operator=(const Statistics &) = delete;

    explicit Statistics():
        number_of_stream_keys_(0),
        number_of_sources_(0),
        number_of_objects_(0)
    {}

    explicit Statistics(size_t number_of_stream_keys,
                        size_t number_of_sources,
                        size_t number_of_objects):
        number_of_stream_keys_(number_of_stream_keys),
        number_of_sources_(number_of_sources),
        number_of_objects_(number_of_objects)
    {}

    Statistics(const Statistics &src, uint8_t percentage)
    {
        if(percentage > 100)
            percentage = 100;

        number_of_stream_keys_ = (src.number_of_stream_keys_ * percentage) / 100U;
        number_of_sources_ = (src.number_of_sources_ * percentage) / 100U;
        number_of_objects_ = (src.number_of_objects_ * percentage) / 100U;
    }

    void reset()
    {
        number_of_stream_keys_ = 0;
        number_of_sources_ = 0;
        number_of_objects_ = 0;
    }

    bool exceeds_limits(const Statistics &limits) const
    {
        return number_of_stream_keys_ > limits.number_of_stream_keys_ ||
               number_of_sources_ > limits.number_of_sources_ ||
               number_of_objects_ > limits.number_of_objects_;
    }

    void dump(const char *what) const;
};

class Manager
{
  public:
    using Hash = MD5::Hash;

  private:
    mutable std::mutex lock_;

    const std::string cache_root_;
    const Path sources_path_;
    const Path objects_path_;

    Statistics statistics_;
    const Statistics &upper_limits_;
    const Statistics lower_limits_;

    PendingIface &pending_;

  public:
    Manager(const Manager &) = delete;
    Manager &operator=(const Manager &) = delete;

    explicit Manager(const char *cache_root, const Statistics &upper_limits,
                     PendingIface &pending):
        cache_root_(cache_root),
        sources_path_(cache_root_ + "/.src"),
        objects_path_(cache_root_ + "/.obj"),
        upper_limits_(upper_limits),
        lower_limits_(upper_limits_, 60),
        pending_(pending)
    {}

    bool init();

    /*!
     * Add key/prio pair if it doesn't exist, and associate with source.
     *
     * In case the source is already stored in cache, the key/prio pair will be
     * inserted or updated if needed.
     *
     * In case the source is unknown, the source entry will be added to the
     * cache, but it will not refer to any converted image. The source may be
     * downloaded/converted in the background and filled in later. (While
     * empty, the source entry is a candidate for cache purging; see
     * #ArtCache::PendingIface for how to avoid this).
     *
     * While the source is not filled in, a possibly existing key/prio pair
     * will remain unchanged. This makes sure that follow-up queries can be
     * answered while the new source is still unavailable. If the key/prio pair
     * didn't exist, then it will be created and associated with the pending
     * source before the download starts.
     */
    AddKeyResult add_stream_key_for_source(const StreamPrioPair &stream_key,
                                           const std::string &source_hash);

    /*!
     * Update data for given source hash after download/conversion.
     *
     * When all converted images are available, store them in cache and update
     * stream keys that should now refer to the new source.
     *
     * \param source_hash
     *     Which source to update.
     * \param import_objects
     *     List of files that contain objects to be associated with the given
     *     source. All given files are moved to the object cached using
     *     \c rename(2), and linked with the source using \c link(2).
     * \param pending_stream_keys
     *     A list of stream key/priority pairs that should be updated to point
     *     to the given source after it has been updated. Keys that do not
     *     exist are ignored.
     */
    UpdateSourceResult update_source(const std::string &source_hash,
                                     std::vector<std::string> &&import_objects,
                                     std::vector<std::pair<StreamPrioPair, AddKeyResult>> &pending_stream_keys);

    /*!
     * Remove key/prio pair, remove source if it would be left unreferenced.
     *
     * This function is typically called for clean up actions after download
     * failures or similar issues that would render the key useless.
     *
     * \note
     *     This function should \e not be called for garbage collection. It's
     *     deletion policies are too aggressive for this purpose.
     */
    void delete_key(const StreamPrioPair &stream_key);

    LookupResult lookup(const StreamPrioPair &stream_key,
                        const std::string &source_hash,
                        const std::string &format,
                        std::unique_ptr<Object> &obj) const;
    LookupResult lookup(const std::string &stream_key,
                        const std::string &source_hash,
                        const std::string &format,
                        std::unique_ptr<Object> &obj) const;

    GCResult gc();

  private:
    static int delete_unreferenced_objects(const char *path, unsigned char dtype,
                                           void *user_data);

    /*!
     * Remove unreferenced source, remove objects that would be left
     * unreferenced.
     *
     * This function will not remove a source if its reference count is greater
     * than 1. That is, it will not leave keys behind with a shared reference
     * to a common, non-existent source because it would make detection of
     * dangling keys harder, thus more costly.
     *
     * \note
     *     This function must be called only while holding the object lock.
     */
    bool delete_source(const std::string &source_hash);

    /*!
     * Remove unreferenced object.
     *
     * This function will not remove an object if its reference count is
     * greater than 1.
     *
     * \note
     *     This function must be called only while holding the object lock.
     */
    bool delete_object(const std::string &object_hash);

    LookupResult do_lookup(const std::string &stream_key, uint8_t priority,
                           const std::string &source_hash,
                           const std::string &format,
                           std::unique_ptr<Object> &obj) const;

    GCResult do_gc();

    void reset();
};

void compute_hash(Manager::Hash &hash, const char *str);
void compute_hash(Manager::Hash &hash, const uint8_t *data, size_t length);
void hash_to_string(const Manager::Hash &hash, std::string &hash_string);

}

/*!@}*/

#endif /* !ARTCACHE_HH */
