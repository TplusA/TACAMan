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

#include <sstream>

#include "converterqueue.hh"
#include "messages.h"

Converter::Job::State Converter::Job::get_state() const
{
    std::lock_guard<std::mutex> lock(lock_);
    return state_;
}

void Converter::Job::add_pending_key(const ArtCache::StreamPrioPair &sp)
{
    std::lock_guard<std::mutex> lock(lock_);

    switch(state_)
    {
      case State::DOWNLOAD_IDLE:
      case State::DOWNLOADING_AND_CONVERTING:
      case State::CONVERT_IDLE:
      case State::CONVERTING:
        break;

      case State::DONE_OK:
      case State::DONE_ERROR:
        BUG("Cannot add pending key in state %u", static_cast<unsigned int>(state_));
        return;
    }

    pending_stream_keys_.emplace_back(std::move(
            std::make_pair(std::move(ArtCache::StreamPrioPair(sp.stream_key_,
                                                              sp.priority_)),
                           ArtCache::AddKeyResult::SOURCE_UNKNOWN)));
}

static void append_snippet(std::ostringstream &os, const std::string &workdir)
{
    os << "#! /bin/sh\ncd '" << workdir << "'\n";
}

static void append_snippet(std::ostringstream &os, const Converter::DownloadData *const dldata)
{
    if(dldata == nullptr)
        return;

    os << "wget -qO '" << dldata->output_file_name_ << "' '" << dldata->source_uri_ << "'\n"
       << "test $? -eq 0 || exit 2\n"
       << "test -f '" << dldata->output_file_name_ << "' || exit 1\n"
       << "test -s '" << dldata->output_file_name_ << "' || exit 3\n";
}

static void append_snippet(std::ostringstream &os, const Converter::ConvertData *const cdata)
{
    if(cdata == nullptr)
        return;

    for(const auto &outfmt : cdata->output_formats_)
        os << "nice -n " << cdata->niceness_
           << " convert '" << cdata->input_file_name_
           << "' -resize " << outfmt.dimensions_
           << " -colors 255 -dither FloydSteinberg -background transparent '"
           << outfmt.format_spec_ << ':' << outfmt.filename_ << "' &\n";

    os << "for i in `seq " << cdata->output_formats_.size() << "`\ndo\n"
       << "    wait\n"
       << "done\n";

    for(const auto &outfmt : cdata->output_formats_)
       os << "test -s '" << outfmt.filename_ << "' || exit 4\n";

    os << "exit 0\n";
}

static Converter::Job::State
generate_script(const std::string &script_name,
                const Converter::DownloadData *const dldata,
                const Converter::ConvertData *const cdata,
                Converter::Job::Result &result)
{
    log_assert(cdata != nullptr);

    switch(os_path_get_type(script_name.c_str()))
    {
      case OS_PATH_TYPE_IO_ERROR:
        /* OK, file does not exist */
        break;

      case OS_PATH_TYPE_FILE:
        BUG("Found orphaned script \"%s\", replacing", script_name.c_str());
        break;

      case OS_PATH_TYPE_DIRECTORY:
      case OS_PATH_TYPE_OTHER:
        BUG("Found non-file path \"%s\", cannot continue", script_name.c_str());
        result = Converter::Job::Result::INTERNAL_ERROR;
        return Converter::Job::State::DONE_ERROR;
    }

    result = Converter::Job::Result::OK;

    msg_info("Generate job script \"%s\"", script_name.c_str());

    std::ostringstream os;
    append_snippet(os, cdata->output_directory_);
    append_snippet(os, dldata);
    append_snippet(os, cdata);

    bool failed = false;
    int fd = os_file_new(script_name.c_str());

    if(fd < 0)
        failed = true;
    else
    {
        const auto &str(os.str());
        failed = (os_write_from_buffer(str.c_str(), str.length(), fd) < 0);

        os_file_close(fd);

        if(failed)
            os_file_delete(script_name.c_str());
    }

    if(failed)
    {
        result = Converter::Job::Result::IO_ERROR;
        return Converter::Job::State::DONE_ERROR;
    }

    os_system_formatted("chmod +x %s", script_name.c_str());

    return dldata != nullptr
        ? Converter::Job::State::DOWNLOADING_AND_CONVERTING
        : Converter::Job::State::CONVERTING;
}

static Converter::Job::Result handle_script_exit_code(int exit_code)
{
    switch(exit_code)
    {
      case 0:
        return Converter::Job::Result::OK;

      case 1:
        return Converter::Job::Result::IO_ERROR;

      case 2:
        return Converter::Job::Result::DOWNLOAD_ERROR;

      case 3:
        return Converter::Job::Result::INPUT_ERROR;

      case 4:
        return Converter::Job::Result::CONVERSION_ERROR;

      default:
        BUG("Unhandled script exit code %d", exit_code);
        break;
    }

    return Converter::Job::Result::INTERNAL_ERROR;
}

static Converter::Job::Result
move_files_to_cache(ArtCache::Manager &cache_manager, Converter::ConvertData &cdata,
                    const std::string &source_hash,
                    std::vector<std::pair<ArtCache::StreamPrioPair, ArtCache::AddKeyResult>> &pending_stream_keys)
{
    std::vector<std::string> output_files;

    for(const auto &outfmt : cdata.output_formats_)
        output_files.emplace_back(cdata.output_directory_ + "/" + outfmt.filename_);

    auto result(Converter::Job::Result::INTERNAL_ERROR);

    switch(cache_manager.update_source(source_hash, std::move(output_files),
                                       pending_stream_keys))
    {
      case ArtCache::UpdateSourceResult::NOT_CHANGED:
      case ArtCache::UpdateSourceResult::UPDATED_SOURCE_ONLY:
      case ArtCache::UpdateSourceResult::UPDATED_KEYS_ONLY:
      case ArtCache::UpdateSourceResult::UPDATED_ALL:
        result = Converter::Job::Result::OK;
        break;

      case ArtCache::UpdateSourceResult::IO_ERROR:
        result = Converter::Job::Result::IO_ERROR;
        break;

      case ArtCache::UpdateSourceResult::DISK_FULL:
        result = Converter::Job::Result::DISK_FULL_ERROR;
        break;

      case ArtCache::UpdateSourceResult::INTERNAL_ERROR:
        break;
    }

    return result;
}

static int delete_all(const char *path, void *user_data)
{
    auto temp(*static_cast<const std::string *>(user_data));
    temp += '/';
    temp += path;

    msg_vinfo(MESSAGE_LEVEL_DIAG, "Delete \"%s\"", temp.c_str());
    os_file_delete(temp.c_str());

    return 0;
}

static Converter::Job::Result clean_up(const std::string &workdir)
{
    os_foreach_in_path(workdir.c_str(), delete_all,
                       const_cast<std::string *>(&workdir));

    if(!os_rmdir(workdir.c_str(), true) && errno != ENOENT)
        return (errno == EACCES || errno == EBUSY || errno == EPERM || errno == EROFS)
            ? Converter::Job::Result::IO_ERROR
            : Converter::Job::Result::INTERNAL_ERROR;

    return Converter::Job::Result::OK;
}

static Converter::Job::Result create_empty_workdir(const std::string &workdir)
{
    if(os_mkdir_hierarchy(workdir.c_str(), true))
        return Converter::Job::Result::OK;
    else if(errno != EEXIST)
        return Converter::Job::Result::IO_ERROR;

    auto result(clean_up(workdir));
    if(result != Converter::Job::Result::OK)
        return result;

    if(os_mkdir_hierarchy(workdir.c_str(), true))
        return Converter::Job::Result::OK;
    else if(errno != EEXIST)
        return Converter::Job::Result::IO_ERROR;

    return Converter::Job::Result::INTERNAL_ERROR;
}

void Converter::Job::execute()
{
    std::unique_lock<std::mutex> lock(lock_);

    switch(do_execute(lock))
    {
      case Result::OK:
        state_ = State::DONE_OK;
        break;

      case Result::IO_ERROR:
      case Result::DISK_FULL_ERROR:
      case Result::DOWNLOAD_ERROR:
      case Result::INPUT_ERROR:
      case Result::CONVERSION_ERROR:
      case Result::INTERNAL_ERROR:
        state_ = State::DONE_ERROR;
        break;
    }
}

Converter::Job::Result Converter::Job::do_execute(std::unique_lock<std::mutex> &lock)
{
    Result result(create_empty_workdir(convert_data_.output_directory_));

    if(result != Result::OK)
        return result;

    switch(state_)
    {
      case State::DOWNLOAD_IDLE:
        state_ = generate_script(script_name_, &download_data_, &convert_data_, result);
        break;

      case State::CONVERT_IDLE:
        state_ = generate_script(script_name_, nullptr, &convert_data_, result);
        break;

      case State::DOWNLOADING_AND_CONVERTING:
      case State::CONVERTING:
      case State::DONE_OK:
      case State::DONE_ERROR:
        BUG("Prepare job in state %u", static_cast<unsigned int>(state_));
        result = Result::INTERNAL_ERROR;
        break;
    }

    if(result != Result::OK)
        return result;

    /* allow state queries */
    lock.unlock();

    /* this is where most of the time will be spent */
    result = handle_script_exit_code(os_system(script_name_.c_str()));

    /* lock again for data juggling below */
    lock.lock();

    if(result != Result::OK)
        return result;

    switch(state_)
    {
      case State::DOWNLOADING_AND_CONVERTING:
      case State::CONVERTING:
        result = move_files_to_cache(cache_manager_, convert_data_,
                                     source_hash_, pending_stream_keys_);
        break;

      case State::DONE_ERROR:
        break;

      case State::DOWNLOAD_IDLE:
      case State::CONVERT_IDLE:
      case State::DONE_OK:
        BUG("State %u after script execution", static_cast<unsigned int>(state_));
        result = Result::INTERNAL_ERROR;
        break;
    }

    return result;
}

void Converter::Job::finalize(ArtCache::PendingIface &pending)
{
    std::lock_guard<std::mutex> lock(lock_);

    for(const auto &key : pending_stream_keys_)
        pending.notify_pending_key_processed(key.first, source_hash_, key.second);

    /* attempt cleaning up the nice way, file by file */
    os_file_delete(script_name_.c_str());
    os_file_delete(std::string(convert_data_.output_directory_ + '/' + temp_file_name_).c_str());;

    /* clean up the safe way in case nice way didn't serve us well */
    if(!os_rmdir(convert_data_.output_directory_.c_str(), true))
        clean_up(convert_data_.output_directory_);
}
