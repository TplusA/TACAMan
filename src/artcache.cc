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

#include <cstring>
#include <algorithm>
#include <dirent.h>

#include "artcache.hh"
#include "os.hh"
#include "messages.h"

static const std::string REFFILE_NAME(".ref");

struct TraverseData
{
    std::string temp_path_;
    const size_t temp_path_original_len_;

    TraverseData(const TraverseData &) = delete;
    TraverseData &operator=(const TraverseData &) = delete;

    explicit TraverseData(const std::string &root):
        temp_path_(root),
        temp_path_original_len_(temp_path_.length())
    {}

    explicit TraverseData(std::string &&root):
        temp_path_(std::move(root)),
        temp_path_original_len_(temp_path_.length())
    {}

    virtual ~TraverseData() {}
};

struct CountData: public TraverseData
{
    size_t count_;

    explicit CountData(const std::string &root):
        TraverseData(root),
        count_(0)
    {}

    explicit CountData(std::string &&root):
        TraverseData(std::move(root)),
        count_(0)
    {}
};

struct CollectMinMaxTimestampsData: public TraverseData
{
    const std::string *const append_filename_;

    size_t count_;
    struct timespec min_;
    struct timespec max_;

    explicit CollectMinMaxTimestampsData(const std::string &root,
                                         const std::string *append_filename):
        TraverseData(root),
        append_filename_(append_filename),
        count_(0),
        min_{ std::numeric_limits<decltype(timespec::tv_sec)>::max(),
              std::numeric_limits<decltype(timespec::tv_nsec)>::max() },
        max_{ std::numeric_limits<decltype(timespec::tv_sec)>::min(),
              std::numeric_limits<decltype(timespec::tv_nsec)>::min() }
    {}

    explicit CollectMinMaxTimestampsData(std::string &&root,
                                         const std::string *append_filename):
        TraverseData(std::move(root)),
        append_filename_(append_filename),
        count_(0),
        min_{ std::numeric_limits<decltype(timespec::tv_sec)>::max(),
              std::numeric_limits<decltype(timespec::tv_nsec)>::max() },
        max_{ std::numeric_limits<decltype(timespec::tv_sec)>::min(),
              std::numeric_limits<decltype(timespec::tv_nsec)>::min() }
    {}
};

struct CollectTimestampsData: public TraverseData
{
    const std::string *const append_filename_;
    std::vector<std::pair<std::string, uint64_t>> access_times_;

    explicit CollectTimestampsData(const std::string &root, const std::string *append_filename):
        TraverseData(root),
        append_filename_(append_filename)
    {}

    explicit CollectTimestampsData(std::string &&root, const std::string *append_filename):
        TraverseData(std::move(root)),
        append_filename_(append_filename)
    {}
};

static inline bool is_valid_hexchar(const char &ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
}

bool ArtCache::is_valid_hash(const char *str)
{
    while(*str != '\0')
    {
        if(!is_valid_hexchar(*str++))
            return false;
    }

    return true;
}

bool ArtCache::is_valid_hash(const char *str, size_t len)
{
    for(size_t i = 0; i < len; ++i)
    {
        if(!is_valid_hexchar(str[i]))
            return false;
    }

    return true;
}

template <typename T>
struct TraverseTraits;

template <>
struct TraverseTraits<struct CountData>
{
    static inline int traverse_sub_failed(CountData &cd)
    {
        msg_error(errno, LOG_ALERT, "Failed counting hashes in cache");
        return -1;
    }

    static inline int traverse_found_hashdir(CountData &cd,
                                             const char *path,
                                             unsigned char dtype)
    {
        ++cd.count_;
        return 0;
    }
};

template <>
struct TraverseTraits<struct CollectMinMaxTimestampsData>
{
    static inline int traverse_sub_failed(CollectMinMaxTimestampsData &cd)
    {
        msg_error(errno, LOG_ALERT, "Failed collecting timestamps below %s",
                  cd.temp_path_.c_str());
        return 0;
    }

    static inline int traverse_found_hashdir(CollectMinMaxTimestampsData &cd,
                                             const char *path,
                                             unsigned char dtype)
    {
        std::string p(cd.temp_path_ + '/' + path);

        if(cd.append_filename_ != nullptr)
        {
            if(dtype != DT_DIR)
            {
                BUG("Path %s is not a directory", p.c_str());
                return 0;
            }

            p += '/' + *cd.append_filename_;
        }

        struct stat buf;

        if(os_lstat(p.c_str(), &buf) < 0)
            return 0;

        const auto &t(buf.st_atim);

        if(t.tv_sec < cd.min_.tv_sec ||
           (t.tv_sec == cd.min_.tv_sec && t.tv_nsec < cd.min_.tv_nsec))
        {
            cd.min_ = t;
        }

        if(t.tv_sec > cd.max_.tv_sec ||
           (t.tv_sec == cd.max_.tv_sec && t.tv_nsec > cd.max_.tv_nsec))
        {
            cd.max_ = t;
        }

        ++cd.count_;

        return 0;
    }
};

template <>
struct TraverseTraits<struct CollectTimestampsData>
{
    static inline int traverse_sub_failed(CollectTimestampsData &cd)
    {
        msg_error(errno, LOG_ALERT, "Failed collecting timestamps below %s",
                  cd.temp_path_.c_str());
        return 0;
    }

    static inline int traverse_found_hashdir(CollectTimestampsData &cd,
                                             const char *path,
                                             unsigned char dtype)
    {
        const size_t hash_start_offset(cd.temp_path_.length() - 2);
        std::string p(cd.temp_path_ + '/' + path);

        if(cd.append_filename_ != nullptr)
        {
            if(dtype != DT_DIR)
            {
                BUG("Path %s is not a directory", p.c_str());
                return 0;
            }

            p += '/' + *cd.append_filename_;
        }

        struct stat buf;

        if(os_lstat(p.c_str(), &buf) < 0)
            return 0;

        cd.access_times_.emplace_back(std::make_pair(
                std::move(cd.temp_path_.substr(hash_start_offset, 2) + path),
                buf.st_atim.tv_sec * 1000UL * 1000UL + buf.st_atim.tv_nsec / 1000));

        return 0;
    }
};

template <typename T, typename Traits = TraverseTraits<T>>
static int traverse_sub(const char *path, unsigned char dtype, void *user_data)
{
    if(ArtCache::is_valid_hash(path))
    {
        auto &cd = *static_cast<T *>(user_data);
        const int ret(Traits::traverse_found_hashdir(cd, path, dtype));

        if(ret != 0)
            return ret;
    }

    return 0;
}

template <typename T, typename Traits = TraverseTraits<T>>
static int traverse_top(const char *path, unsigned char dtype, void *user_data)
{
    if(dtype != DT_DIR)
        return 0;

    auto &cd = *static_cast<T *>(user_data);

    if(!ArtCache::is_valid_hash(path, 2) || path[2] != '\0')
        return 0;

    cd.temp_path_.resize(cd.temp_path_original_len_);
    cd.temp_path_ += path;

    if(os_foreach_in_path(cd.temp_path_.c_str(), traverse_sub<T>, user_data) != 0)
        return Traits::traverse_sub_failed(cd);

    return 0;
}

static bool count_cached_hashes(std::string path, size_t &count)
{
    CountData cd(path.c_str());

    if(os_foreach_in_path(path.c_str(), traverse_top<CountData>, &cd) != 0)
    {
        msg_error(errno, LOG_ALERT,
                  "Failed reading cache below \"%s\"", path.c_str());
        count = 0;
        return false;
    }

    count = cd.count_;

    return true;
}

ArtCache::Object::Object(uint8_t priority, const std::string &hash,
                         const uint8_t *objdata, size_t length):
    priority_(priority),
    hash_(hash)
{
    log_assert(objdata != nullptr);
    log_assert(length > 0);

    std::copy(objdata, objdata + length, std::back_inserter(data_));
}

ArtCache::Object::Object(uint8_t priority, std::string &&hash,
                         const uint8_t *objdata, size_t length):
    priority_(priority),
    hash_(std::move(hash))
{
    log_assert(objdata != nullptr);
    log_assert(length > 0);

    std::copy(objdata, objdata + length, std::back_inserter(data_));
}

void ArtCache::Statistics::dump(const char *what) const
{
    static constexpr char plural[] = "s";

    msg_vinfo(MESSAGE_LEVEL_INFO_MIN,
              "%s: %zu object%s, %zu source%s, %zu stream key%s, %schanged",
              what,
              number_of_objects_, number_of_objects_ != 1 ? plural : "",
              number_of_sources_, number_of_sources_ != 1 ? plural : "",
              number_of_stream_keys_, number_of_stream_keys_ != 1 ? plural : "",
              changed_ ? "" : "not ");
}

bool ArtCache::Timestamp::reset(const ArtCache::Path &path)
{
    struct stat buf;

    if(os_lstat(path.str().c_str(), &buf) < 0)
    {
        reset();
        return false;
    }

    timestamps_[0].tv_sec = buf.st_atim.tv_sec;
    timestamps_[0].tv_usec = buf.st_atim.tv_nsec / 1000;

    return true;
}

bool ArtCache::Timestamp::set_access_time(const ArtCache::Path &path) const
{
    return os_path_utimes(path.str().c_str(), timestamps_);
}

bool ArtCache::Timestamp::set_access_time(const std::string &path) const
{
    return os_path_utimes(path.c_str(), timestamps_);
}

bool ArtCache::Manager::init()
{
    background_task_.start();

    const bool object_path_exists(timestamp_for_hot_path_.reset(objects_path_));

    if(!os_mkdir_hierarchy(sources_path_.str().c_str(), false) ||
       !os_mkdir_hierarchy(objects_path_.str().c_str(), false))
    {
        reset();
        return false;
    }

    if(!object_path_exists)
    {
        background_task_.reset_all_timestamps();
        background_task_.sync();
    }

    msg_vinfo(MESSAGE_LEVEL_DIAG, "Root \"%s\"", cache_root_.c_str());

    size_t keys, sources, objects;

    if(!count_cached_hashes(cache_root_ + '/', keys) ||
       !count_cached_hashes(sources_path_.str(), sources) ||
       !count_cached_hashes(objects_path_.str(), objects))
        reset();
    else
        statistics_.set(keys, sources, objects);

    statistics_.mark_unchanged();

    switch(gc())
    {
      case GCResult::NOT_REQUIRED:
      case GCResult::NOT_POSSIBLE:
        statistics_.dump("Cache statistics");
        break;

      case GCResult::DEFLATED:
      case GCResult::SCHEDULED:
        break;

      case GCResult::IO_ERROR:
        reset();
        break;
    }

    return true;
}

void ArtCache::Manager::reset()
{
    os_system_formatted(false, "rm -r '%s'", cache_root_.c_str());
    statistics_.reset();
    timestamp_for_hot_path_.reset();
}

static inline ArtCache::Path mk_stream_key_dirname(const std::string &cache_root,
                                                   const std::string &stream_key,
                                                   uint8_t priority)
{
    char priority_str[4];
    snprintf(priority_str, sizeof(priority_str), "%03u", priority);

    ArtCache::Path temp(cache_root);
    temp.append_hash(stream_key).append_part(priority_str);

    return temp;
}

static inline ArtCache::Path mk_stream_key_dirname(const std::string &cache_root,
                                                   const ArtCache::StreamPrioPair &stream_key)
{
    return mk_stream_key_dirname(cache_root,
                                 stream_key.stream_key_, stream_key.priority_);
}

static inline ArtCache::Path mk_source_file_name(const ArtCache::Path &root,
                                                 const std::string &source_hash,
                                                 const std::string &name)
{
    ArtCache::Path temp(root);
    return temp.append_hash(source_hash).append_part(name, true);
}

static inline ArtCache::Path mk_source_dir_name(const ArtCache::Path &root,
                                                const std::string &source_hash)
{
    ArtCache::Path temp(root);
    return temp.append_hash(source_hash);
}

static inline ArtCache::Path mk_source_reffile_name(const ArtCache::Path &root,
                                                    const std::string &source_hash)
{
    return mk_source_file_name(root, source_hash, REFFILE_NAME);
}

static ArtCache::AddKeyResult mk_stream_key_entry(const ArtCache::Path &stream_key_dirname)
{
    OS::SuppressErrorsGuard suppress_errors;

    if(os_mkdir_hierarchy(stream_key_dirname.str().c_str(), true))
        return ArtCache::AddKeyResult::INSERTED;

    return (errno == EEXIST)
        ? ArtCache::AddKeyResult::NOT_CHANGED
        : ArtCache::AddKeyResult::IO_ERROR;
}

template <typename T>
static T touch(const std::string &path, const ArtCache::Timestamp &timestamp,
               const T retval_on_success, const T retval_on_disk_full,
               const T retval_on_io_error)
{
    int fd = os_file_new(path.c_str());

    if(fd < 0)
        return (errno == EDQUOT || errno == ENOSPC)
            ? retval_on_disk_full
            : retval_on_io_error;

    os_file_close(fd);

    timestamp.set_access_time(path);

    return retval_on_success;
}

template <typename T>
static T link(const std::string &newpath, const std::string &src,
              const T retval_on_success,
              const T retval_on_disk_full, const T retval_on_io_error)
{
    if(os_link_new(src.c_str(), newpath.c_str()))
        return retval_on_success;

    return (errno == EDQUOT || errno == ENOSPC)
        ? retval_on_disk_full
        : retval_on_io_error;
}

static int have_linked_outputs(const char *path, unsigned char dtype, void *user_data)
{
    if(dtype != DT_REG)
        return 0;

    if((path == REFFILE_NAME))
        return 0;

    *static_cast<bool *>(user_data) = true;

    return 1;
}

static int delete_file(const char *path, unsigned char dtype, void *user_data)
{
    if(dtype != DT_REG)
        return 0;

    auto temp(*static_cast<const ArtCache::Path *>(user_data));
    temp.append_part(path, true);

    msg_vinfo(MESSAGE_LEVEL_DIAG, "Delete \"%s\"", temp.str().c_str());
    os_file_delete(temp.str().c_str());

    return 0;
}

static ArtCache::AddSourceResult mk_source_entry(const ArtCache::Path &sources_root,
                                                 const std::string &source_hash,
                                                 const ArtCache::Timestamp &timestamp)
{
    ArtCache::Path temp(sources_root);
    temp.append_hash(source_hash);

    bool created;

    {
        OS::SuppressErrorsGuard suppress_errors;

        if(os_mkdir_hierarchy(temp.str().c_str(), true))
            created = true;
        else if(errno == EEXIST)
            created = false;
        else
            return ArtCache::AddSourceResult::IO_ERROR;
    }

    if(created)
        temp.append_part(REFFILE_NAME, true);
    else
    {
        ArtCache::Path srcdir(temp);

        temp.append_part(REFFILE_NAME, true);

        if(temp.exists())
        {
            bool found = false;

            os_foreach_in_path(srcdir.str().c_str(), have_linked_outputs, &found);

            return found
                ? ArtCache::AddSourceResult::NOT_CHANGED
                : ArtCache::AddSourceResult::EMPTY;
        }

        os_foreach_in_path(srcdir.str().c_str(), delete_file, &srcdir);
    }

    return touch(temp.str().c_str(), timestamp,
                 ArtCache::AddSourceResult::INSERTED,
                 ArtCache::AddSourceResult::DISK_FULL,
                 ArtCache::AddSourceResult::IO_ERROR);
}

static int find_src_file(const char *path, unsigned char dtype, void *user_data)
{
    if(dtype != DT_REG)
        return 0;

    if(strncmp(path, "src:", 4) != 0)
        return 0;

    *static_cast<std::string *>(user_data) = path;

    return 1;
}

static std::string get_stream_key_source_link(const ArtCache::Path &stream_key_dirname,
                                              std::string *source_link_filename = nullptr)
{
    std::string link_name;

    os_foreach_in_path(stream_key_dirname.str().c_str(), find_src_file,
                       &link_name);

    if(!link_name.empty())
    {
        if(source_link_filename != nullptr)
            *source_link_filename = link_name;

        link_name.erase(0, 4);
    }
    else if(source_link_filename != nullptr)
        source_link_filename->clear();

    return link_name;
}

static ArtCache::AddKeyResult
link_to_source(ArtCache::Path &stream_key_dirname,
               const ArtCache::Path &source_root,
               const std::string &source_hash,
               const ArtCache::AddKeyResult result_if_added)
{
    msg_vinfo(MESSAGE_LEVEL_DEBUG, "Link key %s to source %s",
              stream_key_dirname.str().c_str(), source_hash.c_str());

    std::string old_link_name;

    if(os_foreach_in_path(stream_key_dirname.str().c_str(),
                           find_src_file, &old_link_name) < 0)
        return (errno == ENOENT)
            ? ArtCache::AddKeyResult::INTERNAL_ERROR
            : ArtCache::AddKeyResult::IO_ERROR;

    const std::string new_link_name("src:" + source_hash);

    ArtCache::AddKeyResult result_on_success;

    if(old_link_name.empty())
        result_on_success = result_if_added;
    else if(old_link_name == new_link_name)
        return ArtCache::AddKeyResult::NOT_CHANGED;
    else
    {
        ArtCache::Path temp(stream_key_dirname);
        temp.append_part(old_link_name, true);
        os_file_delete(temp.str().c_str());
        result_on_success = ArtCache::AddKeyResult::REPLACED;
    }

    stream_key_dirname.append_part(new_link_name, true);

    auto const reffile(mk_source_reffile_name(source_root, source_hash));

    return link(stream_key_dirname.str(), reffile.str(),
                result_on_success,
                ArtCache::AddKeyResult::DISK_FULL,
                ArtCache::AddKeyResult::IO_ERROR);
}

ArtCache::AddKeyResult
ArtCache::Manager::add_stream_key_for_source(const ArtCache::StreamPrioPair &stream_key,
                                             const std::string &source_hash)
{
    std::lock_guard<std::mutex> lock(lock_);

    const ArtCache::AddSourceResult src_result =
        mk_source_entry(sources_path_, source_hash, timestamp_for_hot_path_);
    bool have_new_source = false;

    switch(src_result)
    {
      case AddSourceResult::INSERTED:
        have_new_source = true;
        statistics_.add_source();
        break;

      case AddSourceResult::NOT_CHANGED:
        break;

      case AddSourceResult::EMPTY:
        if(!pending_.is_source_pending__unlocked(source_hash, true))
        {
            msg_vinfo(MESSAGE_LEVEL_DEBUG,
                      "Resuming pending source \"%s\"", source_hash.c_str());
            have_new_source = true;
        }

        break;

      case AddSourceResult::IO_ERROR:
        return AddKeyResult::IO_ERROR;

      case AddSourceResult::DISK_FULL:
        return AddKeyResult::DISK_FULL;

      case AddSourceResult::INTERNAL_ERROR:
        return AddKeyResult::INTERNAL_ERROR;
    }

    ArtCache::Path stream_key_dir(mk_stream_key_dirname(cache_root_, stream_key));
    auto key_result = mk_stream_key_entry(stream_key_dir);

    switch(key_result)
    {
      case AddKeyResult::NOT_CHANGED:
        /* key exists already, so we need to keep it the way it is until the
         * new source is filled in */
        if(have_new_source)
            break;

        /* key exists and refers to another queued source that is about to be
         * filled in, so the key should be associated with the queued job */
        if(pending_.add_key_to_pending_source(stream_key,
                                              get_stream_key_source_link(stream_key_dir)))
            return AddKeyResult::SOURCE_PENDING;

        /* key exists and refers to some completely known source, so we can
         * replace the existing link by the new one */
        return link_to_source(stream_key_dir, sources_path_, source_hash,
                              AddKeyResult::INSERTED);

      case AddKeyResult::INSERTED:
        /* key didn't exist, so we can link to source entry right now */
        statistics_.add_stream();
        gc__unlocked();
        return link_to_source(stream_key_dir, sources_path_, source_hash,
                              have_new_source ? AddKeyResult::SOURCE_UNKNOWN : AddKeyResult::INSERTED);

      case AddKeyResult::REPLACED:
      case AddKeyResult::SOURCE_PENDING:
      case AddKeyResult::SOURCE_UNKNOWN:
        BUG("%s(): unreachable", __func__);
        return AddKeyResult::IO_ERROR;

      case AddKeyResult::IO_ERROR:
      case AddKeyResult::DISK_FULL:
      case AddKeyResult::INTERNAL_ERROR:
        return key_result;
    }

    return AddKeyResult::SOURCE_UNKNOWN;
}

static ArtCache::AddObjectResult mk_object_entry(ArtCache::Path &object_name,
                                                 const std::string &object_hash,
                                                 const std::string &source_object_name)
{
    object_name.append_hash(object_hash, true);

    if(object_name.exists())
        return ArtCache::AddObjectResult::EXISTS;

    {
        OS::SuppressErrorsGuard suppress_errors;

        if(!os_mkdir_hierarchy(object_name.dirstr().c_str(), true) &&
           errno != EEXIST)
            return ArtCache::AddObjectResult::IO_ERROR;
    }

    if(os_file_rename(source_object_name.c_str(), object_name.str().c_str()))
        return ArtCache::AddObjectResult::INSERTED;

    return (errno == EDQUOT || errno == ENOSPC)
        ? ArtCache::AddObjectResult::DISK_FULL
        : ArtCache::AddObjectResult::IO_ERROR;
}

struct FindFormatLinkData
{
    const std::string &format_name_;
    std::string found_;
};

static int find_link_for_format(const char *path, unsigned char dtype, void *user_data)
{
    if(dtype != DT_REG)
        return 0;

    auto *data(static_cast<struct FindFormatLinkData *>(user_data));

    if(strncmp(path,
               data->format_name_.c_str(), data->format_name_.length()) != 0)
        return 0;

    if(path[data->format_name_.length()] != ':' &&
       path[data->format_name_.length() + 1] != '\0')
        return 0;

    data->found_ = path;

    return 1;
}

static bool compute_file_content_hash(const std::string &fname,
                                      std::string &hash_string)
{
    struct os_mapped_file_data mapped;

    if(os_map_file_to_memory(&mapped, fname.c_str()) < 0)
        return false;

    ArtCache::Manager::Hash hash;
    ArtCache::compute_hash(hash, static_cast<const uint8_t *>(mapped.ptr), mapped.length);

    os_unmap_file(&mapped);

    ArtCache::hash_to_string(hash, hash_string);

    return true;
}

static ArtCache::UpdateSourceResult
move_objects_and_update_source(const std::vector<std::string> &import_objects,
                               const ArtCache::Path &objects_path,
                               const ArtCache::Path &source_path,
                               ArtCache::Statistics &statistics)
{
    bool added_objects = false;

    for(const auto &fname : import_objects)
    {
        std::string object_hash_string;
        if(!compute_file_content_hash(fname, object_hash_string))
        {
            msg_error(0, LOG_ERR,
                      "Cannot import object \"%s\" (ignored)", fname.c_str());
            continue;
        }

        ArtCache::Path object_name(objects_path);

        switch(mk_object_entry(object_name, object_hash_string, fname))
        {
          case ArtCache::AddObjectResult::EXISTS:
            msg_vinfo(MESSAGE_LEVEL_DEBUG, "Already have object %s (%s)",
                      object_hash_string.c_str(), fname.c_str());
            break;

          case ArtCache::AddObjectResult::INSERTED:
            msg_vinfo(MESSAGE_LEVEL_DEBUG, "New object %s (%s)",
                      object_hash_string.c_str(), fname.c_str());
            added_objects = true;
            statistics.add_object();
            break;

          case ArtCache::AddObjectResult::IO_ERROR:
            return ArtCache::UpdateSourceResult::IO_ERROR;

          case ArtCache::AddObjectResult::DISK_FULL:
            return ArtCache::UpdateSourceResult::DISK_FULL;

          case ArtCache::AddObjectResult::INTERNAL_ERROR:
            return ArtCache::UpdateSourceResult::INTERNAL_ERROR;
        }

        const auto plain_name(std::find(fname.rbegin(), fname.rend(), '/'));
        if(plain_name == fname.rend())
        {
            BUG("Expected absolute path, got \"%s\"", fname.c_str());
            return ArtCache::UpdateSourceResult::INTERNAL_ERROR;
        }

        const std::string format_name(&*(plain_name - 1));
        struct FindFormatLinkData find_data { format_name };
        ArtCache::Path link_path(source_path);
        link_path.append_part(find_data.format_name_ + ':' + object_hash_string, true);

        os_foreach_in_path(source_path.str().c_str(),
                           find_link_for_format, &find_data);

        if(find_data.found_ == link_path.str())
        {
            msg_vinfo(MESSAGE_LEVEL_DEBUG,
                      "Link \"%s\" up-to-date", link_path.str().c_str());
            continue;
        }

        if(find_data.found_.empty())
            msg_vinfo(MESSAGE_LEVEL_DEBUG,
                      "Create new link \"%s\"", link_path.str().c_str());
        else
        {
            msg_vinfo(MESSAGE_LEVEL_DEBUG,
                      "Replace link \"%s\" by \"%s\"",
                      find_data.found_.c_str(), link_path.str().c_str());
            os_file_delete(find_data.found_.c_str());
        }

        link(link_path.str(), object_name.str(),
             ArtCache::UpdateSourceResult::UPDATED_SOURCE_ONLY,
             ArtCache::UpdateSourceResult::DISK_FULL,
             ArtCache::UpdateSourceResult::IO_ERROR);
    }

    return added_objects
        ? ArtCache::UpdateSourceResult::UPDATED_SOURCE_ONLY
        : ArtCache::UpdateSourceResult::NOT_CHANGED;
}

static ArtCache::UpdateSourceResult
link_pending_keys_to_source(std::vector<std::pair<ArtCache::StreamPrioPair, ArtCache::AddKeyResult>> &pending_stream_keys,
                            const std::string &cache_root,
                            const ArtCache::Path &sources_path,
                            const std::string &source_hash,
                            bool is_source_object_updated)
{
    bool updated_keys = false;

    for(auto &key : pending_stream_keys)
    {
        log_assert(key.second == ArtCache::AddKeyResult::SOURCE_UNKNOWN);

        auto key_path(mk_stream_key_dirname(cache_root, key.first));

        if(!key_path.exists())
        {
            msg_error(0, LOG_NOTICE,
                      "Failed updating \"%s\", does not exist (ignored)",
                      key_path.str().c_str());
            continue;
        }

        key.second = link_to_source(key_path, sources_path, source_hash,
                                    ArtCache::AddKeyResult::INSERTED);

        switch(key.second)
        {
          case ArtCache::AddKeyResult::NOT_CHANGED:
            if(is_source_object_updated)
                key.second = ArtCache::AddKeyResult::INSERTED;

            msg_vinfo(MESSAGE_LEVEL_DEBUG, "Key %s[%u] still points to %s",
                      key.first.stream_key_.c_str(), key.first.priority_,
                      source_hash.c_str());
            break;

          case ArtCache::AddKeyResult::REPLACED:
            msg_vinfo(MESSAGE_LEVEL_DEBUG, "Updated key %s[%u] -> %s",
                      key.first.stream_key_.c_str(), key.first.priority_,
                      source_hash.c_str());
            updated_keys = true;
            break;

          case ArtCache::AddKeyResult::IO_ERROR:
            return ArtCache::UpdateSourceResult::IO_ERROR;

          case ArtCache::AddKeyResult::DISK_FULL:
            return ArtCache::UpdateSourceResult::DISK_FULL;

          case ArtCache::AddKeyResult::INTERNAL_ERROR:
            return ArtCache::UpdateSourceResult::INTERNAL_ERROR;

          case ArtCache::AddKeyResult::INSERTED:
          case ArtCache::AddKeyResult::SOURCE_PENDING:
          case ArtCache::AddKeyResult::SOURCE_UNKNOWN:
            BUG("%s(): unreachable", __func__);
            return ArtCache::UpdateSourceResult::INTERNAL_ERROR;
        }
    }

    return updated_keys
        ? ArtCache::UpdateSourceResult::UPDATED_KEYS_ONLY
        : ArtCache::UpdateSourceResult::NOT_CHANGED;
}

ArtCache::UpdateSourceResult
ArtCache::Manager::update_source(const std::string &source_hash,
                                 std::vector<std::string> &&import_objects,
                                 std::vector<std::pair<StreamPrioPair, AddKeyResult>> &pending_stream_keys)
{
    log_assert(!source_hash.empty());

    std::lock_guard<std::mutex> lock(lock_);

    const auto move_objects_result =
        move_objects_and_update_source(import_objects, objects_path_,
                                       mk_source_dir_name(sources_path_, source_hash),
                                       statistics_);

    if(move_objects_result != ArtCache::UpdateSourceResult::NOT_CHANGED &&
       move_objects_result != ArtCache::UpdateSourceResult::UPDATED_SOURCE_ONLY)
        return move_objects_result;

    const auto link_keys_result =
        link_pending_keys_to_source(pending_stream_keys, cache_root_,
                                    sources_path_, source_hash,
                                    move_objects_result != ArtCache::UpdateSourceResult::NOT_CHANGED);

    if(link_keys_result != ArtCache::UpdateSourceResult::NOT_CHANGED &&
       link_keys_result != ArtCache::UpdateSourceResult::UPDATED_KEYS_ONLY)
        return link_keys_result;

    return (move_objects_result == ArtCache::UpdateSourceResult::NOT_CHANGED
            ? link_keys_result
            : (link_keys_result == ArtCache::UpdateSourceResult::NOT_CHANGED
               ? move_objects_result
               : UpdateSourceResult::UPDATED_ALL));
}

void ArtCache::Manager::delete_key(const StreamPrioPair &stream_key)
{
    std::lock_guard<std::mutex> lock(lock_);

    const Path p(mk_stream_key_dirname(cache_root_, stream_key));

    if(!p.exists())
    {
        BUG("Cannot delete key %s[%u], does not exist",
            stream_key.stream_key_.c_str(), stream_key.priority_);
        return;
    }

    std::string linked_file;
    const std::string source_hash(get_stream_key_source_link(p, &linked_file));

    if(!linked_file.empty())
    {
        Path temp(p);
        temp.append_part(linked_file, true);
        os_file_delete(temp.str().c_str());
        (void)delete_source(source_hash);
    }

    if(!os_rmdir(p.str().c_str(), true))
    {
        BUG("Failed deleting key %s[%u]",
            stream_key.stream_key_.c_str(), stream_key.priority_);
        return;
    }

    statistics_.remove_stream();

    msg_vinfo(MESSAGE_LEVEL_DIAG, "Deleted key %s[%u]",
              stream_key.stream_key_.c_str(), stream_key.priority_);
}

static bool must_keep_file(const ArtCache::Path &ref,
                           const char *what, const std::string &name)
{
    const size_t refcount(os_path_get_number_of_hard_links(ref.str().c_str()));

    if(refcount == 0)
    {
        BUG("Cannot delete %s %s, does not exist", what, name.c_str());
        return true;
    }
    else if(refcount < 2)
        return false;
    else
    {
        msg_vinfo(MESSAGE_LEVEL_DEBUG,
                  "Not deleting %s %s with refcount %zu",
                  what, name.c_str(), refcount);
        return true;
    }
}

int ArtCache::Manager::delete_unreferenced_objects(const char *path,
                                                   unsigned char dtype,
                                                   void *user_data)
{
    if(dtype != DT_REG)
        return 0;

    if((path == REFFILE_NAME))
        return 0;

    static_cast<ArtCache::Manager *>(user_data)->delete_object(path);

    return 0;
}

bool ArtCache::Manager::delete_source(const std::string &source_hash)
{
    const Path ref(mk_source_reffile_name(sources_path_, source_hash));

    if(must_keep_file(ref, "source", source_hash))
        return false;

    const std::string srcdir(ref.dirstr());
    os_foreach_in_path(srcdir.c_str(), delete_unreferenced_objects, this);

    os_file_delete(ref.str().c_str());

    if(!os_rmdir(srcdir.c_str(), true))
    {
        BUG("Failed deleting source %s", source_hash.c_str());
        return false;
    }

    statistics_.remove_source();

    msg_vinfo(MESSAGE_LEVEL_DIAG, "Deleted source %s", source_hash.c_str());

    return true;
}

bool ArtCache::Manager::delete_object(const std::string &object_hash)
{
    ArtCache::Path p(objects_path_);
    p.append_hash(object_hash, true);

    if(must_keep_file(p, "object", object_hash))
        return false;

    os_file_delete(p.str().c_str());

    if(!os_rmdir(p.dirstr().c_str(), true))
    {
        BUG("Failed deleting object %s", object_hash.c_str());
        return false;
    }

    statistics_.remove_object();

    msg_vinfo(MESSAGE_LEVEL_DIAG, "Deleted object %s", object_hash.c_str());

    return true;
}

static ArtCache::LookupResult log_lookup(const ArtCache::LookupResult ret,
                                         const std::string &stream_key,
                                         uint8_t priority,
                                         const std::string &object_hash,
                                         const std::string &format)
{
    static constexpr const char *names_[] =
    {
        "FOUND",
        "KEY_UNKNOWN",
        "PENDING",
        "FORMAT_NOT_SUPPORTED",
        "ORPHANED",
        "IO_ERROR",
    };

    static_assert(sizeof(names_) / sizeof(names_[0]) == static_cast<size_t>(ArtCache::LookupResult::LAST_LOOKUP_RESULT) + 1U,
                  "Mismatch between lookup result enum and result strings");

    const char *result_string = static_cast<size_t>(ret) < sizeof(names_) / sizeof(names_[0])
        ? names_[static_cast<size_t>(ret)]
        : "***Invalid ArtCache::LookupResult code***";

    if(object_hash.empty())
        msg_info("Lookup key %s prio %u format %s -> %s",
                 stream_key.c_str(), priority, format.c_str(), result_string);
    else
        msg_info("Lookup key %s prio %u format %s, client version %s -> %s",
                 stream_key.c_str(), priority, format.c_str(),
                 object_hash.c_str(), result_string);

    return ret;
}

ArtCache::LookupResult
ArtCache::Manager::lookup(const ArtCache::StreamPrioPair &stream_key,
                          const std::string &object_hash,
                          const std::string &format,
                          std::unique_ptr<Object> &obj) const
{
    log_assert(!stream_key.stream_key_.empty());
    log_assert(stream_key.priority_ > 0);

    std::lock_guard<std::mutex> lock(lock_);

    return log_lookup(do_lookup(stream_key.stream_key_, stream_key.priority_,
                                object_hash, format, obj),
                      stream_key.stream_key_, stream_key.priority_,
                      object_hash, format);
}

static int find_highest(const char *path, unsigned char dtype, void *user_data)
{
    if(dtype != DT_DIR)
        return 0;

    auto *prio = static_cast<uint8_t *>(user_data);

    unsigned int temp = 0;

    for(char ch = *path++; ch != '\0'; ch = *path++)
    {
        if(ch < '0' || ch > '9')
            return 0;

        temp *= 10;
        temp += ch - '0';

        if(temp > UINT8_MAX)
            return 0;
    }

    if(temp > *prio)
        *prio = temp;

    return 0;
}

static uint8_t find_highest_priority(const std::string &cache_root,
                                     const std::string &stream_key,
                                     ArtCache::LookupResult &result_on_fail)
{
    ArtCache::Path p(cache_root);
    p.append_hash(stream_key);

    uint8_t prio = 0;

    if(os_foreach_in_path(p.str().c_str(), find_highest, &prio) < 0)
        result_on_fail = ArtCache::LookupResult::IO_ERROR;
    else
        result_on_fail = ArtCache::LookupResult::KEY_UNKNOWN;

    return prio;
}

ArtCache::LookupResult
ArtCache::Manager::lookup(const std::string &stream_key,
                          const std::string &object_hash,
                          const std::string &format,
                          std::unique_ptr<Object> &obj) const
{
    log_assert(!stream_key.empty());

    std::lock_guard<std::mutex> lock(lock_);

    ArtCache::LookupResult result;
    const uint8_t prio =
        find_highest_priority(cache_root_, stream_key, result);

    const auto ret = (prio > 0)
        ? do_lookup(stream_key, prio, object_hash, format, obj)
        : result;

    return log_lookup(ret, stream_key, prio, object_hash, format);
}

void ArtCache::Manager::mark_hot_path(const std::string &stream_key,
                                      const std::string &source_hash,
                                      const std::string &object_hash) const
{
    timestamp_for_hot_path_.increment();

    {
        timestamp_for_hot_path_.set_access_time(objects_path_);

        ArtCache::Path p(objects_path_);
        p.append_hash(object_hash, true);
        timestamp_for_hot_path_.set_access_time(p);
    }

    {
        ArtCache::Path p(cache_root_);
        p.append_hash(stream_key);
        timestamp_for_hot_path_.set_access_time(p);
    }

    {
        const ArtCache::Path p(mk_source_reffile_name(sources_path_, source_hash));
        timestamp_for_hot_path_.set_access_time(p);
    }
}

ArtCache::LookupResult
ArtCache::Manager::do_lookup(const std::string &stream_key, uint8_t priority,
                             const std::string &object_hash,
                             const std::string &format,
                             std::unique_ptr<ArtCache::Object> &obj) const
{
    obj = nullptr;

    const Path p(mk_stream_key_dirname(cache_root_, stream_key, priority));
    if(!p.exists())
        return LookupResult::KEY_UNKNOWN;

    const std::string source_hash(get_stream_key_source_link(p));
    if(source_hash.empty())
        return LookupResult::ORPHANED;

    Path src(mk_source_dir_name(sources_path_, source_hash));
    if(!src.exists())
        return pending_.is_source_pending(source_hash, false)
            ? LookupResult::PENDING
            : LookupResult::ORPHANED;

    if(!object_hash.empty())
    {
        /* caller has provided a hint that he knows the data for the given
         * object hash, so don't read anything from file if the object hasn't
         * changed */
        Path temp(src);
        temp.append_part(std::string(format) + ':' + object_hash, true);

        if(temp.exists())
        {
            msg_vinfo(MESSAGE_LEVEL_DIAG,
                      "Object has not changed for key %s prio %u format %s",
                      stream_key.c_str(), priority, format.c_str());

            obj.reset(new ArtCache::Object(priority, object_hash));

            if(obj != nullptr)
            {
                mark_hot_path(stream_key, source_hash, obj->hash_);
                statistics_.mark_for_gc();
            }

            return LookupResult::FOUND;
        }
    }


    struct FindFormatLinkData find_data { format };
    if(os_foreach_in_path(src.str().c_str(),
                          find_link_for_format, &find_data) < 0)
        return LookupResult::IO_ERROR;

    if(find_data.found_.empty())
        return pending_.is_source_pending(source_hash, false)
            ? LookupResult::PENDING
            : LookupResult::FORMAT_NOT_SUPPORTED;

    msg_vinfo(MESSAGE_LEVEL_DIAG,
              "Returning %s for key %s prio %u format %s",
              find_data.found_.c_str(), stream_key.c_str(), priority,
              format.c_str());

    src.append_part(find_data.found_, true);

    struct os_mapped_file_data mapped;
    if(os_map_file_to_memory(&mapped, src.str().c_str()) < 0)
        return LookupResult::IO_ERROR;

    obj.reset(new ArtCache::Object(priority,
                                   find_data.found_.substr(format.length() + 1,
                                                           std::string::npos),
                                   static_cast<const uint8_t *>(mapped.ptr),
                                   mapped.length));

    os_unmap_file(&mapped);

    if(obj != nullptr)
    {
        mark_hot_path(stream_key, source_hash, obj->hash_);
        statistics_.mark_for_gc();
    }

    if(timestamp_for_hot_path_.is_overflown())
        background_task_.reset_all_timestamps();

    return (obj != nullptr) ? LookupResult::FOUND : LookupResult::IO_ERROR;
}

ArtCache::GCResult ArtCache::Manager::gc__unlocked()
{
    if(!statistics_.exceeds_limits(upper_limits_))
        return GCResult::NOT_REQUIRED;

    background_task_.garbage_collection();

    return GCResult::SCHEDULED;
}

void ArtCache::compute_hash(ArtCache::Manager::Hash &hash, const char *str)
{
    compute_hash(hash,
                 static_cast<const uint8_t *>(static_cast<const void *>(str)),
                 strlen(str));
}

void ArtCache::compute_hash(ArtCache::Manager::Hash &hash,
                            const uint8_t *data, size_t length)
{
    MD5::Context ctx;
    MD5::init(ctx);
    MD5::update(ctx, data, length);
    MD5::finish(ctx, hash);
}

void ArtCache::hash_to_string(const ArtCache::Manager::Hash &hash,
                              std::string &hash_string)
{
    MD5::to_string(hash, hash_string);
}

static std::chrono::nanoseconds delta_us(const struct timespec &a, const struct timespec &b)
{
    if(b.tv_sec < a.tv_sec)
        return std::chrono::nanoseconds(0);

    if(a.tv_sec == b.tv_sec)
        return (b.tv_nsec > a.tv_nsec
                ? std::chrono::nanoseconds(b.tv_nsec - a.tv_nsec)
                : std::chrono::nanoseconds(0));

    if(b.tv_nsec >= a.tv_nsec)
        return std::chrono::seconds(b.tv_sec - a.tv_sec) +
               std::chrono::nanoseconds(b.tv_nsec - a.tv_nsec);
    else
        return std::chrono::seconds(b.tv_sec - a.tv_sec - 1) +
               (std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(1)) -
                std::chrono::nanoseconds(a.tv_nsec - b.tv_nsec));
}

static void add_to_timespec(struct timespec &t,
                            const std::chrono::microseconds &us)
{
    const std::chrono::microseconds remainder_us(us % (1000UL * 1000UL));

    t.tv_sec += std::chrono::duration_cast<std::chrono::seconds>(us).count();

    const std::chrono::nanoseconds nanos_until_overflow(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(1)) -
            remainder_us);

    if(nanos_until_overflow.count() > t.tv_nsec)
        t.tv_nsec += std::chrono::duration_cast<std::chrono::nanoseconds>(remainder_us).count();
    else
    {
        ++t.tv_sec;
        t.tv_nsec -= nanos_until_overflow.count();
    }
}

static bool operator>=(const struct timespec &a, const struct timespec &b)
{
    return (a.tv_sec > b.tv_sec) ||
           (a.tv_sec == b.tv_sec && a.tv_nsec >= b.tv_nsec);
}

static bool operator>(const struct timespec &a, const struct timespec &b)
{
    return (a.tv_sec > b.tv_sec) ||
           (a.tv_sec == b.tv_sec && a.tv_nsec > b.tv_nsec);
}

static void compute_threshold(const CollectMinMaxTimestampsData &cd,
                              struct timespec &threshold,
                              bool removed_anything_in_previous_round,
                              bool check_expected_count, size_t expected_count,
                              const char *what)
{
    static constexpr uint8_t BIAS = 10;
    static constexpr uint8_t APPROACHING_PERCENTAGE = 20;
    static_assert(ArtCache::Manager::LIMITS_LOW_HI_PERCENTAGE + BIAS <= 100,
                  "Bias too large for threshold computation");
    static_assert(ArtCache::Manager::LIMITS_LOW_HI_PERCENTAGE >= APPROACHING_PERCENTAGE,
                  "Approaching percentage too big");

    const auto delta = delta_us(cd.min_, cd.max_);

    /*
     * The estimate below is a relative file age. Files older than this
     * estimate (in relation to the oldest file) are subject to removal.
     *
     * We assume uniform distribution of timestamps within the delta range
     * determined above. Under this assumption, a good estimate for a threshold
     * is the age of the oldest file plus a fraction of the difference between
     * oldest and youngest file. The fraction is the percentage that our limits
     * have been derived from.
     *
     * Such a threshold should bring us back to just below our limits after
     * decimating files below that threshold, but we add a little, arbitrary
     * bias to the percentage value to increase the likelihood that this will
     * happen after the first iteration.
     *
     * On successive iterations, a much less aggressive estimate is used in
     * case any objects have been removed in the preceeding round. This is
     * because we generally assume that the first round does the job pretty
     * well already, and should either succeed or come close to the lower
     * limits. Thus, only a few more objects should be removed to finish the
     * job on next round.
     */
    const uint8_t percentage(removed_anything_in_previous_round
                             ? APPROACHING_PERCENTAGE
                             : (ArtCache::Manager::LIMITS_LOW_HI_PERCENTAGE + BIAS));
    const std::chrono::microseconds estimate(
            (std::chrono::duration_cast<std::chrono::microseconds>(delta) * percentage) / 100);

    threshold = cd.min_;
    add_to_timespec(threshold, estimate);

    msg_vinfo(MESSAGE_LEVEL_DEBUG,
              "GC: %5zu %s, min %10lu.%09lus max %10lu.%09lus -> threshold %lu.%lus",
              cd.count_, what,
              cd.min_.tv_sec, cd.min_.tv_nsec,
              cd.max_.tv_sec, cd.max_.tv_nsec,
              threshold.tv_sec, threshold.tv_nsec);

    if(check_expected_count && cd.count_ != expected_count)
        BUG("GC: expected %zu %s, but found %zu",
            expected_count, what, cd.count_);
}

static void collect_statistics(CollectMinMaxTimestampsData &cd,
                               const std::string &path)
{
    msg_vinfo(MESSAGE_LEVEL_DIAG, "GC: traversing path \"%s\"", path.c_str());
    os_foreach_in_path(path.c_str(),
                       traverse_top<CollectMinMaxTimestampsData>, &cd);
    msg_vinfo(MESSAGE_LEVEL_DIAG, "GC: path traversal done");
}

struct DeletedCounts
{
    size_t streams_;
    size_t sources_;
    size_t objects_;

    explicit DeletedCounts():
        streams_(0),
        sources_(0),
        objects_(0)
    {}
};

enum class DecimateType
{
    STREAMS,
    SOURCES,
    OBJECTS,
};

template <DecimateType DT>
struct DecimateCacheEntriesData: public TraverseData
{
    const CollectMinMaxTimestampsData &cd_;
    const struct timespec &threshold_;

    struct timespec oldest_remaining_;

    std::mutex &manager_lock_;
    DeletedCounts &deleted_;
    ArtCache::Statistics &statistics_;

    explicit DecimateCacheEntriesData(const std::string &root,
                                      CollectMinMaxTimestampsData &cd,
                                      const struct timespec &threshold,
                                      DeletedCounts &deleted,
                                      ArtCache::Statistics &statistics,
                                      std::mutex &manager_lock):
        TraverseData(root),
        cd_(cd),
        threshold_(threshold),
        oldest_remaining_{ std::numeric_limits<decltype(timespec::tv_sec)>::max(),
                           std::numeric_limits<decltype(timespec::tv_nsec)>::max() },
        manager_lock_(manager_lock),
        deleted_(deleted),
        statistics_(statistics)
    {}

    explicit DecimateCacheEntriesData(std::string &&root,
                                      CollectMinMaxTimestampsData &cd,
                                      const struct timespec &threshold,
                                      DeletedCounts &deleted,
                                      ArtCache::Statistics &statistics,
                                      std::mutex &manager_lock):
        TraverseData(std::move(root)),
        cd_(cd),
        threshold_(threshold),
        oldest_remaining_{ std::numeric_limits<decltype(timespec::tv_sec)>::max(),
                           std::numeric_limits<decltype(timespec::tv_nsec)>::max() },
        manager_lock_(manager_lock),
        deleted_(deleted),
        statistics_(statistics)
    {}
};

template <>
struct TraverseTraits<struct DecimateCacheEntriesData<DecimateType::STREAMS>>
{
    using DT = struct DecimateCacheEntriesData<DecimateType::STREAMS>;

    static inline int traverse_sub_failed(DT &cd) { return 0; }

    static inline int traverse_found_hashdir(DT &cd, const char *path,
                                             unsigned char dtype)
    {
        if(dtype != DT_DIR)
            return 0;

        const std::string p(cd.temp_path_ + '/' + path);

        std::lock_guard<std::mutex> lock(cd.manager_lock_);

        struct stat buf;
        if(os_lstat(p.c_str(), &buf) < 0)
            return 0;

        if(buf.st_atim >= cd.threshold_)
        {
            msg_vinfo(MESSAGE_LEVEL_TRACE, "GC: keeping stream key %s", p.c_str());

            if(cd.oldest_remaining_ > buf.st_atim)
                cd.oldest_remaining_ = buf.st_atim;
        }
        else
        {
            msg_vinfo(MESSAGE_LEVEL_DEBUG, "GC: remove stream key %s", p.c_str());
            os_system_formatted(false, "rm -r '%s'", p.c_str());

            ++cd.deleted_.streams_;
            cd.statistics_.remove_stream(true);
        }

        return 0;
    }
};

template <>
struct TraverseTraits<struct DecimateCacheEntriesData<DecimateType::SOURCES>>
{
    using DT = struct DecimateCacheEntriesData<DecimateType::SOURCES>;

    static inline int traverse_sub_failed(DT &cd) { return 0; }

    static inline int traverse_found_hashdir(DT &cd, const char *path,
                                             unsigned char dtype)
    {
        if(dtype != DT_DIR)
            return 0;

        const std::string p(cd.temp_path_ + '/' + path);

        ArtCache::Path ref(p);
        ref.append_part(REFFILE_NAME, true);

        std::lock_guard<std::mutex> lock(cd.manager_lock_);

        struct stat buf;

        if(os_lstat(ref.str().c_str(), &buf) == 0 &&
           (buf.st_nlink > 1 || buf.st_atim >= cd.threshold_))
        {
            msg_vinfo(MESSAGE_LEVEL_TRACE, "GC: keeping source %s", p.c_str());

            if(cd.oldest_remaining_ > buf.st_atim)
                cd.oldest_remaining_ = buf.st_atim;
        }
        else
        {
            msg_vinfo(MESSAGE_LEVEL_DEBUG, "GC: remove source %s", p.c_str());
            os_system_formatted(false, "rm -r '%s'", p.c_str());

            ++cd.deleted_.sources_;
            cd.statistics_.remove_source(true);
        }

        return 0;
    }
};

template <>
struct TraverseTraits<struct DecimateCacheEntriesData<DecimateType::OBJECTS>>
{
    using DT = struct DecimateCacheEntriesData<DecimateType::OBJECTS>;

    static inline int traverse_sub_failed(DT &cd) { return 0; }

    static inline int traverse_found_hashdir(DT &cd, const char *path,
                                             unsigned char dtype)
    {
        if(dtype != DT_REG)
            return 0;

        const std::string p(cd.temp_path_ + '/' + path);

        std::lock_guard<std::mutex> lock(cd.manager_lock_);

        struct stat buf;

        if(os_lstat(p.c_str(), &buf) == 0 &&
           (buf.st_nlink > 1 || buf.st_atim >= cd.threshold_))
        {
            msg_vinfo(MESSAGE_LEVEL_TRACE, "GC: keeping object %s", p.c_str());

            if(cd.oldest_remaining_ > buf.st_atim)
                cd.oldest_remaining_ = buf.st_atim;
        }
        else
        {
            msg_vinfo(MESSAGE_LEVEL_DEBUG, "GC: remove object %s", p.c_str());
            os_file_delete(p.c_str());

            ++cd.deleted_.objects_;
            cd.statistics_.remove_object(true);
        }

        return 0;
    }
};

template <DecimateType DT>
static void decimate(CollectMinMaxTimestampsData &cd,
                     const struct timespec &threshold,
                     DeletedCounts &deleted_counts,
                     ArtCache::Statistics &statistics, const std::string &path,
                     std::mutex &manager_lock)
{
    cd.temp_path_.resize(cd.temp_path_original_len_);

    DecimateCacheEntriesData<DT> data(cd.temp_path_, cd, threshold,
                                      deleted_counts, statistics,
                                      manager_lock);

    if(os_foreach_in_path(path.c_str(), traverse_top<decltype(data)>, &data) == 0 &&
       data.oldest_remaining_.tv_sec < std::numeric_limits<decltype(timespec::tv_sec)>::max())
        cd.min_ = data.oldest_remaining_;
}

static int contains_anything(const char *path, unsigned char dtype, void *user_data)
{
    return 1;
}

static int delete_empty_cache_directory(const char *path,
                                        unsigned char dtype, void *user_data)
{
    if(dtype != DT_DIR)
        return 0;

    if(!ArtCache::is_valid_hash(path, 2) || path[2] != '\0')
        return 0;

    auto temp(*static_cast<const ArtCache::Path *>(user_data));
    temp.append_part(path);

    if(os_foreach_in_path(temp.str().c_str(), contains_anything, nullptr) == 0)
    {
        msg_vinfo(MESSAGE_LEVEL_DEBUG, "GC: delete dir \"%s\"", temp.str().c_str());
        os_rmdir(temp.str().c_str(), true);
    }
    else
        msg_vinfo(MESSAGE_LEVEL_TRACE, "GC: keep dir \"%s\"", temp.str().c_str());

    return 0;
}

static void delete_empty_middle_directories(const ArtCache::Path &path)
{
    os_foreach_in_path(path.str().c_str(), delete_empty_cache_directory,
                       const_cast<ArtCache::Path *>(&path));
}

ArtCache::GCResult ArtCache::Manager::do_gc()
{
    std::unique_lock<std::mutex> lock(lock_);

    bool need_new_statistics = true;

    CollectMinMaxTimestampsData streams_minmax(std::move(std::string(cache_root_ + '/')), nullptr);
    CollectMinMaxTimestampsData sources_minmax(sources_path_.str(), &REFFILE_NAME);
    CollectMinMaxTimestampsData objects_minmax(objects_path_.str(), nullptr);

    struct timespec streams_threshold;
    struct timespec sources_threshold;
    struct timespec objects_threshold;

    static constexpr int MAX_FAIL_ROUNDS = 2;
    int fail_rounds_left = MAX_FAIL_ROUNDS;
    bool removed_anything = false;

    do
    {
        if(need_new_statistics)
            msg_info("GC: Collecting cache statistics");

        const bool streams_changed(statistics_.mark_unchanged());
        const size_t streams_expected(statistics_.get_number_of_stream_keys());

        if(need_new_statistics)
            collect_statistics(streams_minmax, cache_root_);

        lock.unlock();
        std::this_thread::yield();
        lock.lock();

        const bool sources_changed(statistics_.mark_unchanged());
        const size_t sources_expected(statistics_.get_number_of_sources());

        if(need_new_statistics)
            collect_statistics(sources_minmax, sources_path_.str());

        lock.unlock();
        std::this_thread::yield();
        lock.lock();

        const bool objects_changed(statistics_.mark_unchanged());
        const size_t objects_expected(statistics_.get_number_of_objects());

        if(need_new_statistics)
            collect_statistics(objects_minmax, objects_path_.str());

        lock.unlock();
        std::this_thread::yield();

        compute_threshold(streams_minmax, streams_threshold, removed_anything,
                          streams_changed, streams_expected, "streams");
        compute_threshold(sources_minmax, sources_threshold, removed_anything,
                          sources_changed, sources_expected, "sources");
        compute_threshold(objects_minmax, objects_threshold, removed_anything,
                          objects_changed, objects_expected, "objects");

        need_new_statistics = streams_changed || sources_changed || objects_changed;

        msg_info("GC: Removing objects");
        DeletedCounts deleted_counts;

        /* keep this order for most effective decimation */
        decimate<DecimateType::STREAMS>(streams_minmax, streams_threshold,
                                        deleted_counts, statistics_,
                                        cache_root_, lock_);
        decimate<DecimateType::SOURCES>(sources_minmax, sources_threshold,
                                        deleted_counts, statistics_,
                                        sources_path_.str(), lock_);
        decimate<DecimateType::OBJECTS>(objects_minmax, objects_threshold,
                                        deleted_counts, statistics_,
                                        objects_path_.str(), lock_);

        lock.lock();
        delete_empty_middle_directories(Path(cache_root_));
        delete_empty_middle_directories(sources_path_);
        delete_empty_middle_directories(objects_path_);
        lock.unlock();

        if(deleted_counts.streams_ > 0 || deleted_counts.sources_ > 0 || deleted_counts.objects_ > 0)
        {
            fail_rounds_left = MAX_FAIL_ROUNDS;
            removed_anything = true;
            msg_info("GC: Removed %zu streams, %zu sources, %zu objects",
                     deleted_counts.streams_, deleted_counts.sources_,
                     deleted_counts.objects_);
        }
        else
        {
            msg_info("GC: Failed removing anything, %u rounds left", fail_rounds_left);
            --fail_rounds_left;
        }

        lock.lock();
    }
    while(fail_rounds_left >= 0 && statistics_.exceeds_limits(lower_limits_));

    if(removed_anything)
        statistics_.dump("Cache statistics after garbage collection");

    return removed_anything ? GCResult::DEFLATED : GCResult::NOT_POSSIBLE;
}

struct ResetTimestampsData: public TraverseData
{
    const std::string *const append_filename_;
    const ArtCache::Timestamp &timestamp_;
    size_t success_count_;
    size_t failure_count_;

    explicit ResetTimestampsData(const std::string &root,
                                 const std::string *append_filename,
                                 const ArtCache::Timestamp &timestamp):
        TraverseData(root),
        append_filename_(append_filename),
        timestamp_(timestamp),
        success_count_(0),
        failure_count_(0)
    {}

    explicit ResetTimestampsData(std::string &&root,
                                 const std::string *append_filename,
                                 const ArtCache::Timestamp &timestamp):
        TraverseData(std::move(root)),
        append_filename_(append_filename),
        timestamp_(timestamp),
        success_count_(0),
        failure_count_(0)
    {}
};

template <>
struct TraverseTraits<struct ResetTimestampsData>
{
    static inline int traverse_sub_failed(ResetTimestampsData &rd) { return 0; }

    static inline int traverse_found_hashdir(ResetTimestampsData &rd,
                                             const char *path,
                                             unsigned char dtype)
    {
        std::string p(rd.temp_path_ + '/' + path);

        if(rd.append_filename_ != nullptr)
        {
            if(dtype != DT_DIR)
            {
                BUG("Path %s is not a directory", p.c_str());
                return 0;
            }

            p += '/' + *rd.append_filename_;
        }

        msg_vinfo(MESSAGE_LEVEL_TRACE, "Reset timestamp for \"%s\"", p.c_str());

        if(rd.timestamp_.set_access_time(p))
            ++rd.success_count_;
        else
            ++rd.failure_count_;

        return 0;
    }
};

static void reset_timestamps(const std::string &path,
                             const ArtCache::Timestamp &timestamp,
                             size_t &success_count, size_t &failure_count,
                             const std::string *append_filename = nullptr)
{
    ResetTimestampsData rd(path, append_filename, timestamp);
    os_foreach_in_path(path.c_str(), traverse_top<ResetTimestampsData>, &rd);

    success_count += rd.success_count_;
    failure_count += rd.failure_count_;
}

void ArtCache::Manager::do_reset_all_timestamps()
{
    msg_info("Resetting all timestamps");

    std::lock_guard<std::mutex> lock(lock_);

    timestamp_for_hot_path_.reset();
    timestamp_for_hot_path_.set_access_time(objects_path_);

    size_t success_count = 0;
    size_t failure_count = 0;
    reset_timestamps(std::move(std::string(cache_root_ + '/')), timestamp_for_hot_path_,
                     success_count, failure_count);
    reset_timestamps(sources_path_.str(), timestamp_for_hot_path_,
                     success_count, failure_count, &REFFILE_NAME);
    reset_timestamps(objects_path_.str(), timestamp_for_hot_path_,
                     success_count, failure_count);

    msg_info("Resetting timestamps done (%zu set, %zu failed)",
             success_count, failure_count);
}
