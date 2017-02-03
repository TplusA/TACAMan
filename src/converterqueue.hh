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

#ifndef CONVERTERQUEUE_HH
#define CONVERTERQUEUE_HH

#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <thread>

#include "artcache.hh"
#include "cachetypes.hh"
#include "pending.hh"
#include "formats.hh"

namespace Converter
{

class PendingData
{
  public:
    class Guard
    {
      private:
        const std::string *&string_ptr_;

      public:
        Guard(const Guard &) = delete;
        Guard(Guard &&) = default;
        Guard &operator=(const Guard &) = delete;

        explicit Guard(const std::string *&sptr): string_ptr_(sptr) {}
        ~Guard() { string_ptr_ = nullptr; }
    };

    /* set while a possibly new entry is about to be added to the cache */
    const std::string *adding_source_hash_;

    PendingData(const PendingData &) = delete;
    PendingData &operator=(const PendingData &) = delete;

    explicit PendingData():
        adding_source_hash_(nullptr)
    {}

    Guard earmark_add_source(const std::string &source_hash)
    {
        adding_source_hash_ = &source_hash;
        return Guard(adding_source_hash_);
    }
};

class DownloadData
{
  public:
    const std::string source_uri_;
    const std::string &output_file_name_;

    DownloadData(const DownloadData &) = delete;
    DownloadData(DownloadData &&) = default;
    DownloadData &operator=(const DownloadData &) = delete;

    explicit DownloadData(const std::string &outfile):
        output_file_name_(outfile)
    {}

    explicit DownloadData(const char *uri, const std::string &outfile):
        source_uri_(uri),
        output_file_name_(outfile)
    {}
};

class ConvertData
{
  public:
    const std::string &input_file_name_;
    const std::string output_directory_;
    const std::vector<OutputFormat> &output_formats_;
    const int niceness_;

    ConvertData(const ConvertData &) = delete;
    ConvertData(ConvertData &&) = default;
    ConvertData &operator=(const ConvertData &) = delete;

    explicit ConvertData(const std::string &infile, const std::string &outdir,
                         const std::vector<OutputFormat> &formats):
        input_file_name_(infile),
        output_directory_(outdir),
        output_formats_(formats),
        niceness_(19)
    {}
};

class Job
{
  public:
    enum class State
    {
        DOWNLOAD_IDLE,
        DOWNLOADING_AND_CONVERTING,
        CONVERT_IDLE,
        CONVERTING,
        DONE_OK,
        DONE_ERROR,
    };

    enum class Result
    {
        OK,
        IO_ERROR,
        DISK_FULL_ERROR,
        DOWNLOAD_ERROR,
        INPUT_ERROR,
        CONVERSION_ERROR,
        INTERNAL_ERROR,
    };

    const std::string source_hash_;

  private:
    mutable std::mutex lock_;

    State state_;

    ArtCache::Manager &cache_manager_;
    std::vector<std::pair<ArtCache::StreamPrioPair, ArtCache::AddKeyResult>> pending_stream_keys_;

    const std::string temp_file_name_;
    DownloadData download_data_;
    ConvertData convert_data_;
    const std::string script_name_;

  public:
    Job(const Job &) = delete;
    Job &operator=(const Job &) = delete;

    /*!
     * Download first, then convert.
     */
    explicit Job(std::string &&temp_dir, const char *uri,
                 std::string &&source_hash,
                 ArtCache::StreamPrioPair &&first_pending_key,
                 ArtCache::Manager &cache_manager):
        source_hash_(std::move(source_hash)),
        state_(State::DOWNLOAD_IDLE),
        cache_manager_(cache_manager),
        temp_file_name_("original_downloaded"),
        download_data_(uri, temp_file_name_),
        convert_data_(temp_file_name_, std::move(temp_dir),
                      get_output_format_list().get_formats()),
        script_name_(convert_data_.output_directory_ + "/job.sh")
    {
        pending_stream_keys_.emplace_back(std::move(std::make_pair(std::move(first_pending_key),
                                                                   ArtCache::AddKeyResult::SOURCE_UNKNOWN)));
    }

    /*!
     * For just converting data without prior download.
     */
    explicit Job(std::string &&temp_dir,
                 std::string &&source_hash,
                 ArtCache::StreamPrioPair &&first_pending_key,
                 ArtCache::Manager &cache_manager):
        source_hash_(std::move(source_hash)),
        state_(State::CONVERT_IDLE),
        cache_manager_(cache_manager),
        temp_file_name_("original_raw"),
        download_data_(temp_file_name_),
        convert_data_(temp_file_name_, std::move(temp_dir),
                      get_output_format_list().get_formats()),
        script_name_(convert_data_.output_directory_ + "/job.sh")
    {
        pending_stream_keys_.emplace_back(std::move(std::make_pair(std::move(first_pending_key),
                                                                   ArtCache::AddKeyResult::SOURCE_UNKNOWN)));
    }

    State get_state() const;

    void add_pending_key(const ArtCache::StreamPrioPair &sp);

    void execute();
    void finalize(ArtCache::PendingIface &pending);

  private:
    Result do_execute(std::unique_lock<std::mutex> &lock);

  public:
    static Result clean_up(const std::string &workdir);
    static bool write_data_to_file(const uint8_t *data, size_t length,
                                   const std::string &filename);
};

class Queue: public ArtCache::PendingIface
{
  private:
    mutable std::mutex lock_;

    std::condition_variable job_available_;
    std::deque<std::shared_ptr<Job>> jobs_;
    std::atomic<bool> shutdown_request_;

    std::shared_ptr<Job> running_job_;
    std::thread worker_;

    const std::string temp_dir_;
    PendingData pdata_;

  public:
    Queue(const Queue &) = delete;
    Queue &operator=(const Queue &) = delete;

    explicit Queue(const char *cache_root):
        shutdown_request_(false),
        running_job_(nullptr),
        temp_dir_(std::string(cache_root) + "/.tmp")
    {}

    void init();
    void shutdown();

    void add_to_cache_by_uri(ArtCache::Manager &cache_manager,
                             ArtCache::StreamPrioPair &&sp, const char *uri);
    void add_to_cache_by_data(ArtCache::Manager &cache_manager,
                              ArtCache::StreamPrioPair &&sp,
                              const uint8_t *data, size_t length);

    bool is_source_pending(const std::string &source_hash, bool exclude_current) const override;
    bool is_source_pending__unlocked(const std::string &source_hash, bool exclude_current) const override;
    bool add_key_to_pending_source(const ArtCache::StreamPrioPair &stream_key,
                                   const std::string &source_hash) override;
    void notify_pending_key_processed(const ArtCache::StreamPrioPair &stream_key,
                                      const std::string &source_hash,
                                      ArtCache::AddKeyResult result,
                                      ArtCache::Manager &cache_manager) override;

  private:
    bool queue(std::shared_ptr<Job> &&job);

    void worker_main();
};

}

#endif /* !CONVERTERQUEUE_HH */
