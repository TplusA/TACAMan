/*
 * Copyright (C) 2017, 2020, 2021, 2022  T+A elektroakustik GmbH & Co. KG
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

#ifndef ARTCACHE_HH
#define ARTCACHE_HH

#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>
#include <memory>

#include "cachetypes.hh"
#include "cachepath.hh"
#include "pending.hh"
#include "md5.hh"
#include "messages.h"

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

    LAST_LOOKUP_RESULT = IO_ERROR,
};

enum class GCResult
{
    NOT_REQUIRED,
    NOT_POSSIBLE,
    SCHEDULED,
    DEFLATED,
    IO_ERROR,
};

class Statistics
{
  private:
    size_t number_of_stream_keys_;
    size_t number_of_sources_;
    size_t number_of_objects_;

    bool changed_;

  public:
    Statistics(const Statistics &) = delete;
    Statistics &operator=(const Statistics &) = delete;

    explicit Statistics():
        number_of_stream_keys_(0),
        number_of_sources_(0),
        number_of_objects_(0),
        changed_(false)
    {}

    explicit Statistics(size_t number_of_stream_keys,
                        size_t number_of_sources,
                        size_t number_of_objects):
        number_of_stream_keys_(number_of_stream_keys),
        number_of_sources_(number_of_sources),
        number_of_objects_(number_of_objects),
        changed_(false)
    {}

    explicit Statistics(const Statistics &src, uint8_t percentage):
        changed_(src.changed_)
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
        changed_ = false;
    }

    void set(size_t number_of_stream_keys, size_t number_of_sources,
             size_t number_of_objects)
    {
        number_of_stream_keys_ = number_of_stream_keys;
        number_of_sources_ = number_of_sources;
        number_of_objects_ = number_of_objects;
        changed_ = true;
    }

    bool mark_unchanged()
    {
        if(!changed_)
            return false;

        changed_ = false;

        return true;
    }

    void mark_for_gc() { changed_ = true; }

    bool exceeds_limits(const Statistics &limits) const
    {
        return number_of_stream_keys_ > limits.number_of_stream_keys_ ||
               number_of_sources_ > limits.number_of_sources_ ||
               number_of_objects_ > limits.number_of_objects_;
    }

    size_t get_number_of_stream_keys() const { return number_of_stream_keys_; }
    size_t get_number_of_sources() const     { return number_of_sources_; }
    size_t get_number_of_objects() const     { return number_of_objects_; }

    void add_stream() { add_to_counter(number_of_stream_keys_); }
    void add_source() { add_to_counter(number_of_sources_); }
    void add_object() { add_to_counter(number_of_objects_); }
    void remove_stream(bool is_gc = false) { sub_from_counter(number_of_stream_keys_, is_gc); }
    void remove_source(bool is_gc = false) { sub_from_counter(number_of_sources_, is_gc); }
    void remove_object(bool is_gc = false) { sub_from_counter(number_of_objects_, is_gc); }

    void dump(const char *what) const;

  private:
    void add_to_counter(size_t &counter)
    {
        ++counter;
        changed_ = true;
    }

    void sub_from_counter(size_t &counter, bool is_gc)
    {
        msg_log_assert(counter > 0);
        --counter;

        if(!is_gc)
            changed_ = true;
    }
};

class Timestamp
{
  private:
    struct timeval timestamps_[2];
    bool overflown_;

  public:
    Timestamp(const Timestamp &) = delete;
    Timestamp &operator=(const Timestamp &) = delete;

    explicit Timestamp():
        timestamps_{},
        overflown_(false)
    {}

    void reset()
    {
        timestamps_[0].tv_sec = 0;
        timestamps_[0].tv_usec = 0;
        overflown_ = false;
    }

    bool reset(const Path &path);

    bool increment()
    {
        struct timeval &access_time(timestamps_[0]);

        if(overflown_)
            return false;

        if(++access_time.tv_usec < 1000L * 1000L)
            return true;

        access_time.tv_usec = 0;

        if(access_time.tv_sec < std::numeric_limits<long>::max())
            ++access_time.tv_sec;
        else if(!overflown_)
        {
            msg_info("TIMESTAMP OVERFLOW");
            overflown_ = true;
        }

        return !overflown_;
    }

    bool is_overflown() const { return overflown_; }

    bool set_access_time(const Path &path) const;
    bool set_access_time(const std::string &path) const;
};

class Manager;

class BackgroundTask
{
  private:
    enum class Action
    {
        SHUTDOWN,
        RESET_TIMESTAMPS,
        GC,
    };

    std::thread th_;

    std::mutex lock_;
    std::condition_variable have_work_;
    std::condition_variable all_work_done_;
    std::deque<Action> pending_actions_;

    Manager &manager_;

  public:
    BackgroundTask(const BackgroundTask &) = delete;
    BackgroundTask &operator=(const BackgroundTask &) = delete;

    explicit BackgroundTask(Manager &manager): manager_(manager) {}

    ~BackgroundTask() { shutdown(true); }

    void start();
    void shutdown(bool is_high_priority);
    void sync();

    bool garbage_collection() { return append_action(Action::GC); }
    bool reset_all_timestamps() { return append_action(Action::RESET_TIMESTAMPS); }

  private:
    void task_main();
    bool append_action(Action action);
};

class Manager
{
  public:
    using Hash = MD5::Hash;

    static constexpr uint8_t LIMITS_LOW_HI_PERCENTAGE = 60;

  private:
    mutable std::mutex lock_;

    const std::string cache_root_;
    const Path sources_path_;
    const Path objects_path_;

    mutable Statistics statistics_;
    const Statistics &upper_limits_;
    const Statistics lower_limits_;

    PendingIface &pending_;

    mutable Timestamp timestamp_for_hot_path_;
    mutable BackgroundTask background_task_;

  public:
    Manager(const Manager &) = delete;
    Manager &operator=(const Manager &) = delete;

    explicit Manager(const char *cache_root, const Statistics &upper_limits,
                     PendingIface &pending):
        cache_root_(cache_root),
        sources_path_(cache_root_ + "/.src"),
        objects_path_(cache_root_ + "/.obj"),
        upper_limits_(upper_limits),
        lower_limits_(upper_limits_, LIMITS_LOW_HI_PERCENTAGE),
        pending_(pending),
        background_task_(*this)
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
     *     source. All given files are moved to the object cache using
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
                        const std::string &object_hash,
                        const std::string &format,
                        std::unique_ptr<Object> &obj) const;
    LookupResult lookup(const std::string &stream_key,
                        const std::string &object_hash,
                        const std::string &format,
                        std::unique_ptr<Object> &obj) const;

    GCResult gc()
    {
        std::lock_guard<std::mutex> lock(lock_);
        return gc__unlocked();
    }

  private:
    GCResult gc__unlocked();

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
                           const std::string &object_hash,
                           const std::string &format,
                           std::unique_ptr<Object> &obj) const;

    void mark_hot_path(const std::string &stream_key,
                       const std::string &source_hash,
                       const std::string &object_hash) const;

    void reset();

    GCResult do_gc();
    void do_reset_all_timestamps();

  public:
    struct BackgroundActions
    {
      private:
        static GCResult gc(Manager &manager) { return manager.do_gc(); }
        static void reset_all_timestamps(Manager &manager) { manager.do_reset_all_timestamps(); }

        friend class BackgroundTask;
    };
};

void compute_hash(Manager::Hash &hash, const char *str);
void compute_hash(Manager::Hash &hash, const uint8_t *data, size_t length);
void hash_to_string(const Manager::Hash &hash, std::string &hash_string);

}

/*!@}*/

#endif /* !ARTCACHE_HH */
