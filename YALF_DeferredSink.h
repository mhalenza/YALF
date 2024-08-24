// Copyright (c) 2024 Matt M Halenza
// SPDX-License-Identifier: MIT
#pragma once
#include "YALF.h"
#include <condition_variable>
#include <thread>
#include <mutex>
#include <queue>

namespace YALF {

struct DeferredLogEntry {
    LogLevel level;
    std::string domain;
    std::optional<std::string> instance;
    std::source_location source_location;
    LogEntryTimestamp timestamp;
    std::string message;
};

class DeferredSink : public Sink
{
public:
    DeferredSink(std::unique_ptr<Sink> underlying_)
        : Sink()
        , underlying(std::move(underlying_))
        , stop_requested(false)
        , mtx{}
        , cv()
        , queue{}
        , worker(&DeferredSink::doBackgroundWork, this)
    {}

    ~DeferredSink()
    {
        this->stop_requested = true;
        this->cv.notify_one();
        this->worker.join();
    }

    virtual bool checkFilter(EntryMetadata const& entry) const override
    {
        return this->underlying->checkFilter(entry);
    }
    virtual void setDefaultLogLevel(LogLevel level) override
    {
        return this->underlying->setDefaultLogLevel(level);
    }
    virtual void setDomainLogLevel(std::string_view domain, LogLevel level) override
    {
        return this->underlying->setDomainLogLevel(domain, level);
    }
    virtual void clearDomainLogLevel(std::string_view domain) override
    {
        return this->underlying->clearDomainLogLevel(domain);
    }

    virtual void log(EntryMetadata const& meta, std::string_view msg) override
    {
        DeferredLogEntry dle = {
            .level = meta.level,
            .domain = std::string{meta.domain},
            .instance = [&]() -> std::optional<std::string> { if (meta.instance){ return std::string{meta.instance.value()}; } else { return std::nullopt; } }(),
            .source_location = meta.source_location,
            .timestamp = meta.timestamp,
            .message = std::string{msg},
        };
        {
            std::lock_guard lg {this->mtx};
            this->queue.push(std::move(dle));
        }
        this->cv.notify_one();
    }

private:
    void doBackgroundWork()
    {
        while (!this->stop_requested){
            std::unique_lock lg {this->mtx};
            this->cv.wait(lg, [&]{ return this->stop_requested || !this->queue.empty(); });
            while (!this->queue.empty()){
                auto const& entry = this->queue.front();
                lg.unlock();
                auto const instance = [&]() -> std::optional<std::string> { if (entry.instance){ return entry.instance.value(); } else { return std::nullopt; }}();
                EntryMetadata const meta = {
                    .level = entry.level,
                    .domain = entry.domain,
                    .instance = instance,
                    .source_location = entry.source_location,
                    .timestamp = entry.timestamp,
                };
                this->underlying->log(meta, entry.message);
                lg.lock();
                this->queue.pop();
            }
        }
    }

private:
    std::unique_ptr<Sink> underlying;
    std::atomic_bool stop_requested;
    std::mutex mtx; // cv, queue
    std::condition_variable cv;
    std::queue<DeferredLogEntry> queue;
    std::thread worker;
};

}
