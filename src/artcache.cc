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
#include <algorithm>

#include "artcache.hh"
#include "os.h"
#include "messages.h"

static const std::string REFFILE_NAME(".ref");

struct CountData
{
    CountData(const CountData &) = delete;
    CountData &operator=(const CountData &) = delete;

    std::string temp_path_;
    std::string temp_subpath_;
    const size_t temp_path_original_len_;

    size_t count_;

    explicit CountData(const std::string &root):
        temp_path_(root),
        temp_path_original_len_(root.length()),
        count_(0)
    {}

    explicit CountData(std::string &&root):
        temp_path_(root),
        temp_path_original_len_(root.length()),
        count_(0)
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

static int count_hashes_sub(const char *path, void *user_data)
{
    auto &cd = *static_cast<CountData *>(user_data);

    if(ArtCache::is_valid_hash(path))
        ++cd.count_;

    return 0;
}

static int count_hashes_top(const char *path, void *user_data)
{
    auto &cd = *static_cast<CountData *>(user_data);

    if(!ArtCache::is_valid_hash(path, 2) || path[2] != '\0')
        return 0;

    cd.temp_path_.resize(cd.temp_path_original_len_);
    cd.temp_path_ += path;
    cd.temp_subpath_ = cd.temp_path_;

    if(os_foreach_in_path(cd.temp_path_.c_str(), count_hashes_sub, user_data) != 0)
    {
        msg_error(errno, LOG_ALERT, "Failed counting hashes in cache");
        return -1;
    }

    return 0;
}

static bool count_cached_hashes(std::string path, size_t &count)
{
    CountData cd(path.c_str());

    if(os_foreach_in_path(path.c_str(), count_hashes_top, &cd) != 0)
    {
        msg_error(errno, LOG_ALERT,
                  "Failed reading cache below \"%s\"", path.c_str());
        return -1;
    }

    count = cd.count_;

    return true;
}

void ArtCache::Statistics::dump(const char *what) const
{
    static constexpr char plural[] = "s";

    msg_vinfo(MESSAGE_LEVEL_INFO_MIN,
              "%s: %zu object%s, %zu source%s, %zu stream key%s",
              what,
              number_of_objects_, number_of_objects_ != 1 ? plural : "",
              number_of_sources_, number_of_sources_ != 1 ? plural : "",
              number_of_stream_keys_, number_of_stream_keys_ != 1 ? plural : "");
}

bool ArtCache::Manager::init()
{
    if(!os_mkdir_hierarchy(sources_path_.str().c_str(), false) ||
       !os_mkdir_hierarchy(objects_path_.str().c_str(), false))
    {
        reset();
        return false;
    }

    msg_vinfo(MESSAGE_LEVEL_DIAG, "Root \"%s\"", cache_root_.c_str());

    if(!count_cached_hashes(cache_root_ + '/', statistics_.number_of_stream_keys_) ||
       !count_cached_hashes(sources_path_.str(), statistics_.number_of_sources_) ||
       !count_cached_hashes(objects_path_.str(), statistics_.number_of_objects_))
        reset();

    switch(gc())
    {
      case GCResult::NOT_REQUIRED:
      case GCResult::NOT_POSSIBLE:
        statistics_.dump("Cache statistics");
        break;

      case GCResult::DEFLATED:
        break;

      case GCResult::IO_ERROR:
        reset();
        break;
    }

    return true;
}

void ArtCache::Manager::reset()
{
    os_system_formatted("rm -r '%s'", cache_root_.c_str());
    statistics_.reset();
}

static inline ArtCache::Path mk_stream_key_dirname(const std::string &cache_root,
                                                   const ArtCache::StreamPrioPair &stream_key)
{
    char priority_str[4];
    snprintf(priority_str, sizeof(priority_str),
             "%03u", stream_key.priority_);

    ArtCache::Path temp(cache_root);
    temp.append_hash(stream_key.stream_key_).append_part(priority_str);

    return temp;
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
    if(os_mkdir_hierarchy(stream_key_dirname.str().c_str(), true))
        return ArtCache::AddKeyResult::INSERTED;

    return (errno == EEXIST)
        ? ArtCache::AddKeyResult::NOT_CHANGED
        : ArtCache::AddKeyResult::IO_ERROR;
}

template <typename T>
static T touch(const std::string &path, const T retval_on_success,
               const T retval_on_disk_full, const T retval_on_io_error)
{
    int fd = os_file_new(path.c_str());

    if(fd < 0)
        return (errno == EDQUOT || errno == ENOSPC)
            ? retval_on_disk_full
            : retval_on_io_error;

    os_file_close(fd);

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

static int have_linked_outputs(const char *path, void *user_data)
{
    if((path == REFFILE_NAME))
        return 0;

    *static_cast<bool *>(user_data) = true;
    return 1;
}

static int delete_all(const char *path, void *user_data)
{
    auto temp(*static_cast<const ArtCache::Path *>(user_data));
    temp.append_part(path, true);

    msg_vinfo(MESSAGE_LEVEL_DIAG, "Delete \"%s\"", temp.str().c_str());
    os_file_delete(temp.str().c_str());

    return 0;
}

static ArtCache::AddSourceResult mk_source_entry(const ArtCache::Path &sources_root,
                                                 const std::string &source_hash)
{
    ArtCache::Path temp(sources_root);
    temp.append_hash(source_hash);

    bool created;

    if(os_mkdir_hierarchy(temp.str().c_str(), true))
        created = true;
    else if(errno == EEXIST)
        created = false;
    else
        return ArtCache::AddSourceResult::IO_ERROR;

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

        os_foreach_in_path(srcdir.str().c_str(), delete_all, &srcdir);
    }

    return touch(temp.str().c_str(),
                 ArtCache::AddSourceResult::INSERTED,
                 ArtCache::AddSourceResult::DISK_FULL,
                 ArtCache::AddSourceResult::IO_ERROR);
}

static int find_src_file(const char *path, void *user_data)
{
    if(strncmp(path, "src:", 4) != 0)
        return 0;

    *static_cast<std::string *>(user_data) = path;
    return 1;
}

static std::string get_stream_key_source_link(const ArtCache::Path &stream_key_dirname)
{
    std::string link_name;

    os_foreach_in_path(stream_key_dirname.str().c_str(), find_src_file,
                       &link_name);

    if(!link_name.empty())
        link_name.erase(0, 4);

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

    ArtCache::AddSourceResult src_result = mk_source_entry(sources_path_,
                                                           source_hash);
    bool have_new_source = false;

    switch(src_result)
    {
      case AddSourceResult::INSERTED:
        have_new_source = true;
        ++statistics_.number_of_sources_;
        break;

      case AddSourceResult::NOT_CHANGED:
        break;

      case AddSourceResult::EMPTY:
        if(!pending_.is_source_pending__unlocked(source_hash, true))
        {
            msg_info("Resuming pending source \"%s\"", source_hash.c_str());
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
        ++statistics_.number_of_stream_keys_;
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

    if(!os_mkdir_hierarchy(object_name.dirstr().c_str(), true) &&
       errno != EEXIST)
        return ArtCache::AddObjectResult::IO_ERROR;

    if(os_file_rename(source_object_name.c_str(), object_name.str().c_str()))
        return ArtCache::AddObjectResult::INSERTED;

    return (errno == EDQUOT || errno == ENOSPC)
        ? ArtCache::AddObjectResult::DISK_FULL
        : ArtCache::AddObjectResult::IO_ERROR;
}

struct FindFormatLinkData
{
    const std::string format_name_;
    std::string found_;
};

static int find_link_for_format(const char *path, void *user_data)
{
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
            ++statistics.number_of_objects_;
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

        struct FindFormatLinkData find_data { std::string(&*(plain_name - 1)) };
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

ArtCache::LookupResult
ArtCache::Manager::lookup(const std::string &stream_key, uint8_t priority,
                          const std::string &object_hash, ArtCache::Object *&obj) const
{
    log_assert(!stream_key.empty());

    std::lock_guard<std::mutex> lock(lock_);

    if(object_hash.empty())
        msg_vinfo(MESSAGE_LEVEL_DEBUG,
                  "Lookup key \"%s\" prio %u (unconditional)",
                  stream_key.c_str(), priority);
    else
        msg_vinfo(MESSAGE_LEVEL_DEBUG,
                  "Lookup key \"%s\" prio %u, client version \"%s\"",
                  stream_key.c_str(), priority,  object_hash.c_str());

    BUG("%s(): not implemented yet", __func__);
    obj = nullptr;

    return LookupResult::KEY_UNKNOWN;
}

ArtCache::LookupResult
ArtCache::Manager::lookup(const std::string &stream_key,
                          const std::string &object_hash, ArtCache::Object *&obj) const
{
    log_assert(!stream_key.empty());

    std::lock_guard<std::mutex> lock(lock_);

    if(object_hash.empty())
        msg_vinfo(MESSAGE_LEVEL_DEBUG,
                  "Lookup best object for key \"%s\" (unconditional)",
                  stream_key.c_str());
    else
        msg_vinfo(MESSAGE_LEVEL_DEBUG,
                  "Lookup best object for key \"%s\", client version \"%s\"",
                  stream_key.c_str(), object_hash.c_str());

    BUG("%s(): not implemented yet", __func__);
    obj = nullptr;

    return LookupResult::KEY_UNKNOWN;
}

ArtCache::GCResult ArtCache::Manager::gc()
{
    std::lock_guard<std::mutex> lock(lock_);

    return statistics_.exceeds_limits(upper_limits_)
        ? do_gc()
        : GCResult::NOT_REQUIRED;
}

ArtCache::GCResult ArtCache::Manager::do_gc()
{
    BUG("%s(): not implemented", __func__);
    return GCResult::NOT_POSSIBLE;
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
