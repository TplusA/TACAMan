/*
 * Copyright (C) 2017, 2020, 2022  T+A elektroakustik GmbH & Co. KG
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

#include <glib.h>
#include <algorithm>

#include "converterqueue.hh"
#include "dbus_handlers.hh"
#include "dbus_iface_deep.h"
#include "de_tahifi_artcache_errors.hh"
#include "os.hh"
#include "messages.h"

static const std::string empty_string;

static std::string compute_uri_hash(const char *uri)
{
    ArtCache::Manager::Hash hash;
    ArtCache::compute_hash(hash, uri);

    std::string result;
    ArtCache::hash_to_string(hash, result);

    return result;
}

static std::string compute_data_hash(const uint8_t *data, size_t length)
{
    ArtCache::Manager::Hash hash;
    ArtCache::compute_hash(hash, data, length);

    std::string result;
    ArtCache::hash_to_string(hash, result);

    return result;
}

void Converter::Queue::worker_main()
{
    std::unique_lock<std::mutex> qlock(lock_, std::defer_lock);

    while(1)
    {
        qlock.lock();

        job_available_.wait(qlock,
                            [this]()
                            {
                                return shutdown_request_ || !jobs_.empty();
                            });

        if(shutdown_request_)
            break;

        /* move job data to our own stack, unlock the queue,
         * execute the job --- IN THIS ORDER! */
        running_job_ = std::move(jobs_.front());
        jobs_.pop_front();

        qlock.unlock();

        running_job_->execute();

        qlock.lock();
        running_job_->finalize(*this);
        running_job_ = nullptr;
        qlock.unlock();
    }
}

void Converter::Queue::init()
{
    os_mkdir_hierarchy(temp_dir_.c_str(), false);
    worker_ = std::thread(&Converter::Queue::worker_main, this);
}

void Converter::Queue::shutdown()
{
    if(shutdown_request_.exchange(true))
        return;

    {
        std::lock_guard<std::mutex> lock(lock_);

        if(worker_.get_id() == std::thread::id())
            return;

        job_available_.notify_all();
    }

    worker_.join();
    worker_ = std::thread();
}

void Converter::Queue::add_to_cache_by_uri(ArtCache::Manager &cache_manager,
                                           ArtCache::StreamPrioPair &&sp,
                                           const char *uri)
{
    msg_log_assert(!sp.stream_key_.empty());
    msg_log_assert(sp.priority_ > 0);
    msg_log_assert(uri != nullptr);
    msg_log_assert(uri[0] != '\0');

    const auto source_hash_string(compute_uri_hash(uri));

    std::lock_guard<std::mutex> lock(lock_);
    auto addguard(pdata_.earmark_add_source(source_hash_string));

    const auto result(cache_manager.add_stream_key_for_source(sp, source_hash_string));

    if(result != ArtCache::AddKeyResult::SOURCE_UNKNOWN)
    {
        notify_pending_key_processed(sp, source_hash_string, result,
                                     cache_manager);
        return;
    }

    msg_vinfo(MESSAGE_LEVEL_DEBUG,
                "Source \"%s\" (%s) for key %s, prio %u not in cache",
                uri, source_hash_string.c_str(),
                sp.stream_key_.c_str(), sp.priority_);

    ArtCache::StreamPrioPair sp_copy(
            static_cast<const ArtCache::StreamPrioPair &>(sp).stream_key_,
            sp.priority_);

    static const std::string temp_filename("original_downloaded");

    auto workdir(temp_dir_ + '/' + source_hash_string);

    if(queue(std::move(std::make_shared<Job>(std::move(workdir), temp_filename,
                                             uri, std::string(source_hash_string),
                                             std::move(sp), cache_manager))))
    {
        tdbus_art_cache_monitor_emit_associated(dbus_get_artcache_monitor_iface(),
                                                DBus::hexstring_to_variant(sp_copy.stream_key_),
                                                sp_copy.priority_);
        return;
    }

    notify_pending_key_processed(sp_copy, source_hash_string, result, cache_manager);
}

void Converter::Queue::add_to_cache_by_data(ArtCache::Manager &cache_manager,
                                            ArtCache::StreamPrioPair &&sp,
                                            const uint8_t *data, size_t length)
{
    msg_log_assert(!sp.stream_key_.empty());
    msg_log_assert(sp.priority_ > 0);
    msg_log_assert(data != nullptr);
    msg_log_assert(length > 0);

    const auto source_hash_string(compute_data_hash(data, length));

    std::lock_guard<std::mutex> lock(lock_);
    auto addguard(pdata_.earmark_add_source(source_hash_string));

    auto result(cache_manager.add_stream_key_for_source(sp, source_hash_string));

    if(result != ArtCache::AddKeyResult::SOURCE_UNKNOWN)
    {
        notify_pending_key_processed(sp, source_hash_string, result,
                                     cache_manager);
        return;
    }

    msg_vinfo(MESSAGE_LEVEL_DEBUG,
              "Source %s for key %s, prio %u not in cache",
              source_hash_string.c_str(),
              sp.stream_key_.c_str(), sp.priority_);

    ArtCache::StreamPrioPair sp_copy(
            static_cast<const ArtCache::StreamPrioPair &>(sp).stream_key_,
            sp.priority_);

    auto workdir(temp_dir_ + '/' + source_hash_string);

    {
        OS::SuppressErrorsGuard suppress_errors;

        if(!os_mkdir_hierarchy(workdir.c_str(), true))
            result = (errno == EEXIST)
                ? ArtCache::AddKeyResult::SOURCE_PENDING
                : ArtCache::AddKeyResult::IO_ERROR;
    }

    static const std::string temp_filename("original_raw");

    if(result == ArtCache::AddKeyResult::SOURCE_UNKNOWN &&
       !Converter::Job::write_data_to_file(data, length,
                                           workdir + '/' + temp_filename))
    {
        result = ArtCache::AddKeyResult::IO_ERROR;
        Converter::Job::clean_up(workdir);
    }

    if(result == ArtCache::AddKeyResult::SOURCE_UNKNOWN &&
       queue(std::move(std::make_shared<Job>(std::move(workdir), temp_filename,
                                             std::string(source_hash_string),
                                             std::move(sp), cache_manager))))
    {
        tdbus_art_cache_monitor_emit_associated(dbus_get_artcache_monitor_iface(),
                                                DBus::hexstring_to_variant(sp_copy.stream_key_),
                                                sp_copy.priority_);
        return;
    }

    notify_pending_key_processed(sp_copy, source_hash_string, result, cache_manager);
}

bool Converter::Queue::is_source_pending(const std::string &source_hash,
                                         bool exclude_current) const
{
    std::lock_guard<std::mutex> lock(lock_);
    return is_source_pending__unlocked(source_hash, exclude_current);
}

bool Converter::Queue::is_source_pending__unlocked(const std::string &source_hash,
                                                   bool exclude_current) const
{
    if(!exclude_current)
    {
        if(pdata_.adding_source_hash_ != nullptr &&
           source_hash == *pdata_.adding_source_hash_)
            return true;

        if(running_job_ != nullptr && running_job_->source_hash_ == source_hash)
            return true;
    }

    return std::any_of(jobs_.begin(), jobs_.end(),
                       [&source_hash] (const auto &j) { return j->source_hash_ == source_hash; });
}

bool Converter::Queue::add_key_to_pending_source(const ArtCache::StreamPrioPair &stream_key,
                                                 const std::string &source_hash)
{
    if(running_job_ != nullptr && running_job_->source_hash_ == source_hash)
    {
        running_job_->add_pending_key(stream_key);
        return true;
    }

    const auto &it(std::find_if(jobs_.begin(), jobs_.end(),
                                [&source_hash] (const auto &j) { return j->source_hash_ == source_hash; }));

    if(it == jobs_.end())
        return false;

    (*it)->add_pending_key(stream_key);
    return true;
}

void Converter::Queue::notify_pending_key_processed(const ArtCache::StreamPrioPair &stream_key,
                                                    const std::string &source_hash,
                                                    ArtCache::AddKeyResult result,
                                                    ArtCache::Manager &cache_manager)
{
    auto error_code(ArtCache::MonitorError::Code::INTERNAL);

    switch(result)
    {
      case ArtCache::AddKeyResult::NOT_CHANGED:
        msg_vinfo(MESSAGE_LEVEL_DEBUG, "Key \"%s\", prio %u unchanged for %s",
                  stream_key.stream_key_.c_str(), stream_key.priority_,
                  source_hash.c_str());
        tdbus_art_cache_monitor_emit_added(dbus_get_artcache_monitor_iface(),
                                           DBus::hexstring_to_variant(stream_key.stream_key_),
                                           stream_key.priority_, FALSE);
        return;

      case ArtCache::AddKeyResult::INSERTED:
        msg_vinfo(MESSAGE_LEVEL_DEBUG, "Added key \"%s\", prio %u for %s",
                  stream_key.stream_key_.c_str(), stream_key.priority_,
                  source_hash.c_str());
        tdbus_art_cache_monitor_emit_added(dbus_get_artcache_monitor_iface(),
                                           DBus::hexstring_to_variant(stream_key.stream_key_),
                                           stream_key.priority_, TRUE);
        return;

      case ArtCache::AddKeyResult::REPLACED:
        msg_vinfo(MESSAGE_LEVEL_DEBUG, "Replaced key \"%s\", prio %u, now %s",
                  stream_key.stream_key_.c_str(), stream_key.priority_,
                  source_hash.c_str());
        tdbus_art_cache_monitor_emit_added(dbus_get_artcache_monitor_iface(),
                                           DBus::hexstring_to_variant(stream_key.stream_key_),
                                           stream_key.priority_, TRUE);
        return;

      case ArtCache::AddKeyResult::SOURCE_PENDING:
        msg_vinfo(MESSAGE_LEVEL_DEBUG, "Added key \"%s\", prio %u, to pending source",
                  stream_key.stream_key_.c_str(), stream_key.priority_);
        tdbus_art_cache_monitor_emit_associated(dbus_get_artcache_monitor_iface(),
                                                DBus::hexstring_to_variant(stream_key.stream_key_),
                                                stream_key.priority_);
        return;

      case ArtCache::AddKeyResult::SOURCE_UNKNOWN:
        error_code = ArtCache::MonitorError::Code::DOWNLOAD_ERROR;
        break;

      case ArtCache::AddKeyResult::IO_ERROR:
        error_code = ArtCache::MonitorError::Code::IO_FAILURE;
        break;

      case ArtCache::AddKeyResult::DISK_FULL:
        error_code = ArtCache::MonitorError::Code::NO_SPACE_ON_DISK;
        break;

      case ArtCache::AddKeyResult::INTERNAL_ERROR:
        error_code = ArtCache::MonitorError::Code::INTERNAL;
        break;
    }

    cache_manager.delete_key(stream_key);

    tdbus_art_cache_monitor_emit_failed(dbus_get_artcache_monitor_iface(),
                                        DBus::hexstring_to_variant(stream_key.stream_key_),
                                        stream_key.priority_, error_code);
}

bool Converter::Queue::queue(std::shared_ptr<Converter::Job> &&job)
{
    msg_log_assert(job != nullptr);
    msg_log_assert(job->get_state() == Job::State::DOWNLOAD_IDLE ||
                   job->get_state() == Job::State::CONVERT_IDLE);
    msg_log_assert(pdata_.adding_source_hash_ != nullptr);

    jobs_.emplace_back(std::move(job));
    job_available_.notify_one();

    return true;
}
