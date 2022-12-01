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

#include <algorithm>

#include "artcache.hh"
#include "messages.h"

void ArtCache::BackgroundTask::task_main()
{
    while(true)
    {
        std::unique_lock<std::mutex> lock(lock_);

        if(pending_actions_.empty())
        {
            lock.unlock();
            all_work_done_.notify_all();
            lock.lock();
        }

        have_work_.wait(lock, [this] { return !pending_actions_.empty(); });

        const auto current_action(pending_actions_.front());
        pending_actions_.pop_front();

        lock.unlock();

        switch(current_action)
        {
          case Action::SHUTDOWN:
            all_work_done_.notify_all();
            return;

          case Action::GC:
            Manager::BackgroundActions::gc(manager_);
            break;

          case Action::RESET_TIMESTAMPS:
            Manager::BackgroundActions::reset_all_timestamps(manager_);
            break;
        }
    }
}

void ArtCache::BackgroundTask::sync()
{
    std::unique_lock<std::mutex> lock(lock_);
    all_work_done_.wait(lock, [this] { return pending_actions_.empty(); });
}

void ArtCache::BackgroundTask::start()
{
    msg_log_assert(th_.get_id() == std::thread::id());
    th_ = std::thread(&BackgroundTask::task_main, this);
}

void ArtCache::BackgroundTask::shutdown(bool is_high_priority)
{
    msg_log_assert(th_.get_id() != std::thread::id());
    msg_log_assert(th_.joinable());

    if(is_high_priority)
    {
        pending_actions_.clear();
        pending_actions_.push_back(Action::SHUTDOWN);
        have_work_.notify_one();
    }
    else
        append_action(Action::SHUTDOWN);

    th_.join();
    th_ = std::thread();
}

bool ArtCache::BackgroundTask::append_action(ArtCache::BackgroundTask::Action action)
{
    std::unique_lock<std::mutex> lock(lock_);

    if(std::find(pending_actions_.begin(), pending_actions_.end(), action) != pending_actions_.end())
        return false;

    pending_actions_.push_back(action);

    lock.unlock();
    have_work_.notify_one();

    return true;
}
