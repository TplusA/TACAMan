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

#include <glib.h>

#include "converterqueue.hh"
#include "dbus_handlers.hh"
#include "dbus_iface_deep.h"
#include "de_tahifi_artcache_errors.hh"
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
    log_assert(!sp.stream_key_.empty());
    log_assert(sp.priority_ > 0);
    log_assert(uri != nullptr);
    log_assert(uri[0] != '\0');

    std::string source_hash_string(compute_uri_hash(uri));

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
                "Source \"%s\" (%s) for key \"%s\", prio %u not in cache",
                uri, source_hash_string.c_str(),
                sp.stream_key_.c_str(), sp.priority_);

    ArtCache::StreamPrioPair sp_copy(
            static_cast<const ArtCache::StreamPrioPair &>(sp).stream_key_,
            sp.priority_);

    if(queue(std::move(std::shared_ptr<Job>(
            new Job(std::move(std::string(temp_dir_ + '/' + source_hash_string)),
                    uri, std::move(source_hash_string),
                    std::move(sp), cache_manager)))))
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
    log_assert(!sp.stream_key_.empty());
    log_assert(sp.priority_ > 0);
    log_assert(data != nullptr);
    log_assert(length > 0);

    ArtCache::Manager::Hash source_hash;
    ArtCache::compute_hash(source_hash, data, length);

    std::lock_guard<std::mutex> lock(lock_);

    msg_vinfo(MESSAGE_LEVEL_DEBUG,
              "Add key \"%s\", prio %u for raw data of length %zu",
              sp.stream_key_.c_str(), sp.priority_, length);

    BUG("%s(): not implemented yet", __func__);

    tdbus_art_cache_monitor_emit_failed(dbus_get_artcache_monitor_iface(),
                                        DBus::hexstring_to_variant(sp.stream_key_),
                                        sp.priority_,
                                        ArtCache::MonitorError::Code::INTERNAL);
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

    for(const auto &job : jobs_)
    {
        if(job->source_hash_ == source_hash)
            return true;
    }

    return false;
}

bool Converter::Queue::add_key_to_pending_source(const ArtCache::StreamPrioPair &stream_key,
                                                 const std::string &source_hash)
{
    if(running_job_ != nullptr && running_job_->source_hash_ == source_hash)
    {
        running_job_->add_pending_key(stream_key);
        return true;
    }

    for(auto &job : jobs_)
    {
        if(job->source_hash_ == source_hash)
        {
            job->add_pending_key(stream_key);
            return true;
        }
    }

    return false;
}

// cppcheck-suppress functionStatic
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
    log_assert(job != nullptr);
    log_assert(job->get_state() == Job::State::DOWNLOAD_IDLE ||
               job->get_state() == Job::State::CONVERT_IDLE);
    log_assert(pdata_.adding_source_hash_ != nullptr);

    jobs_.emplace_back(std::move(job));
    job_available_.notify_one();

    return true;
}
