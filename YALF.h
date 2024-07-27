// Copyright (c) 2024 Matt M Halenza
// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <assert.h>

namespace YALF {

enum class LogLevel
{
    Fatal, // Errors that need to halt the program immediately
    Critical, // Errors that MUST be corrected, but do not terminate the program
    Error, // Errors
    Warning, // Warnings
    Info, // Informational
    Debug, // Debug Messages
    Noise, // Debugging Messages that are usually ignored
};

inline
std::optional<LogLevel> parseLogLevelString(std::string_view str)
{
    if (str == "Fatal") return LogLevel::Fatal;
    if (str == "Critical") return LogLevel::Critical;
    if (str == "Error") return LogLevel::Error;
    if (str == "Warning") return LogLevel::Warning;
    if (str == "Info") return LogLevel::Info;
    if (str == "Debug") return LogLevel::Debug;
    if (str == "Noise") return LogLevel::Noise;
    return std::nullopt;
}

inline
std::string_view getLogLevelString(LogLevel level)
{
    using namespace std::string_view_literals;
    switch (level) {
        case LogLevel::Fatal: return "Fatal"sv;
        case LogLevel::Critical: return "Critical"sv;
        case LogLevel::Error: return "Error"sv;
        case LogLevel::Warning: return "Warning"sv;
        case LogLevel::Info: return "Info"sv;
        case LogLevel::Debug: return "Debug"sv;
        case LogLevel::Noise: return "Noise"sv;
    }
    return "<invalid>"sv;
}

#ifndef YALF_TIMESTAMP_RESOLUTION
#define YALF_TIMESTAMP_RESOLUTION std::micro
#endif
#ifndef YALF_TIMESTAMP_CLOCK
#define YALF_TIMESTAMP_CLOCK std::chrono::system_clock
#endif
using LogEntryTimestampResolution = YALF_TIMESTAMP_RESOLUTION;
using LogEntryTimestampClock = YALF_TIMESTAMP_CLOCK;
using LogEntryTimestampDuration = std::chrono::duration<LogEntryTimestampClock::rep, LogEntryTimestampResolution>;
using LogEntryTimestamp = std::chrono::time_point<LogEntryTimestampClock, LogEntryTimestampDuration>;
struct EntryMetadata
{
    LogLevel level;
    std::string_view domain;
    std::optional<std::string_view> instance;
    std::source_location source_location;
    LogEntryTimestamp timestamp;
};

template <typename ObjectType>
concept HasGetName = requires(ObjectType const* obj)
{
    { obj->getName() } -> std::convertible_to<std::string_view>;
};

template <typename ObjectType>
concept HasInstanceGetDomain = requires(ObjectType const* obj)
{
    { obj->getDomain() } -> std::convertible_to<std::string_view>;
};

template <typename ObjectType>
concept HasClassGetDomain = requires
{
    { ObjectType::getDomain() } -> std::convertible_to<std::string_view>;
};

class Filter
{
protected:
    Filter() = default;
public:
    virtual ~Filter() = default;

    virtual bool checkFilter(EntryMetadata const& entry) const
    {
        auto const it = std::ranges::find_if(
            this->domains,
            [&](std::string const& domain) -> bool { return entry.domain.compare(domain) == 0; },
            &decltype(this->domains)::value_type::first);
        if (it != this->domains.end())
            return entry.level <= it->second;
        else
            return entry.level <= this->default_level;
    }

    virtual void setDefaultLogLevel(LogLevel level){ this->default_level = level; }
    virtual void setDomainLogLevel(std::string_view domain, LogLevel level){ this->domains[std::string{ domain }] = level; }
    virtual void clearDomainLogLevel(std::string_view domain){ this->domains.erase(std::string{ domain }); }

private:
    LogLevel default_level = LogLevel::Info;
    std::unordered_map<std::string, LogLevel> domains;
};

class Sink : public Filter
{
public:
    Sink() = default;
    virtual void log(EntryMetadata const& meta, std::string_view msg) = 0;
};

inline
std::string_view truncateFilename(std::string_view filename)
{
    #ifdef _MSC_VER
    filename.remove_prefix(filename.find_last_of('\\') + 1);
    #else
    filename.remove_prefix(filename.find_last_of('/') + 1);
    #endif
    return filename;
}
class FormattedStringSink : public Sink
{
public:
    FormattedStringSink()
        : Sink()
        , default_fmt("%H:%M:%S %F:%l %D[%I] %L:  %x%R%n")
        , fmts()
    {}

    void setFormat(std::string_view fmt)
    {
        this->default_fmt = std::string{ fmt };
    }
    void setFormat(LogLevel level, std::string_view fmt)
    {
        this->fmts[level] = std::string{ fmt };
    }
    void clearFormat(LogLevel level)
    {
        this->fmts.erase(level);
    }

protected:
    std::string_view getFormatString(LogLevel level)
    {
        auto it = this->fmts.find(level);
        if (it != this->fmts.end())
            return it->second;
        return this->default_fmt;
    }
    std::string formatEntry(EntryMetadata const& meta, std::string_view msg)
    {
        std::string_view fmt = this->getFormatString(meta.level);
        std::string out;
        out.reserve(fmt.size());
        auto out_it = std::back_inserter(out);

        #ifdef YALF_USE_LOCALTIME
        auto const local_timestamp = std::chrono::zoned_time{ std::chrono::current_zone(), meta.timestamp }.get_local_time();
        #else
        auto const local_timestamp = meta.timestamp;
        #endif

        size_t s = 0;
        while (s < fmt.size()) {
            if (fmt[s] != '%') {
                auto p = fmt.find('%', s);
                if (p == std::string_view::npos) {
                    out += fmt.substr(s);
                    s = fmt.size();
                }
                else {
                    out += fmt.substr(s, p - s);
                    s += (p - s);
                }
            }
            else if (s < fmt.size() - 1) {
                char const fc = fmt[s + 1];
                switch (fc) {
                    case '%': out_it = '%'; break;
                    case 'n':
                        #ifdef _MSC_VER
                        out_it = '\r';
                        #endif
                        out_it = '\n';
                        break;
                    // Timestamps
                    case 'y': std::format_to(out_it, "{:%y}", local_timestamp); break;
                    case 'Y': std::format_to(out_it, "{:%Y}", local_timestamp); break;
                    case 'b': std::format_to(out_it, "{:%b}", local_timestamp); break;
                    case 'B': std::format_to(out_it, "{:%B}", local_timestamp); break;
                    case 'm': std::format_to(out_it, "{:%m}", local_timestamp); break;
                    case 'd': std::format_to(out_it, "{:%d}", local_timestamp); break;
                    case 'e': std::format_to(out_it, "{:%e}", local_timestamp); break;
                    case 'a': std::format_to(out_it, "{:%a}", local_timestamp); break;
                    case 'A': std::format_to(out_it, "{:%A}", local_timestamp); break;
                    case 'H': std::format_to(out_it, "{:%H}", local_timestamp); break;
                    case 'M': std::format_to(out_it, "{:%M}", local_timestamp); break;
                    case 'S': std::format_to(out_it, "{:%S}", local_timestamp); break;
                    // Source Location
                    case 'F': out += truncateFilename(meta.source_location.file_name()); break;
                    case 'f': out += meta.source_location.function_name(); break;
                    case 'l': out += std::to_string(meta.source_location.line()); break;
                    case 'c': out += std::to_string(meta.source_location.column()); break;
                    // Domain, Instance, Level, Msg
                    case 'D': out += meta.domain; break;
                    case 'I': out += meta.instance.value_or(std::string_view{ "" }); break;
                    case 'L': std::format_to(out_it, "{: >8}", getLogLevelString(meta.level)); break;
                    case 'x': out += msg; break;
                    // Colors
                    case 'R': out += "\033[0m"; break; // Reset colors
                    case 'C': // Foreground Colors
                        if (s < fmt.size() - 2) {
                            char const cc = fmt[s + 2];
                            switch (cc) {
                                case 'x': out += "\033[30m"; break; // %Cx = Black
                                case 'r': out += "\033[31m"; break; // %Cr = Red
                                case 'g': out += "\033[32m"; break; // %Cg = Green
                                case 'y': out += "\033[33m"; break; // %Cy = Yellow 
                                case 'b': out += "\033[34m"; break; // %Cb = Blue
                                case 'm': out += "\033[35m"; break; // %Cm = Magenta
                                case 'c': out += "\033[36m"; break; // %Cc = Cyan
                                case 'w': out += "\033[37m"; break; // %Cw = White (Light Gray)
                                case 'X': out += "\033[90m"; break; // %CX = Bright Black (Dark Gray)
                                case 'R': out += "\033[91m"; break; // %CR = Bright Red
                                case 'G': out += "\033[92m"; break; // %CG = Bright Green
                                case 'Y': out += "\033[93m"; break; // %CY = Bright Yellow
                                case 'B': out += "\033[94m"; break; // %CB = Bright Blue
                                case 'M': out += "\033[95m"; break; // %CM = Bright Magenta
                                case 'C': out += "\033[96m"; break; // %CC = Bright Cyan
                                case 'W': out += "\033[97m"; break; // %CW = Bright White
                                default: break;
                            }
                            s++;
                        }
                        break;
                    case 'Q': // Background Colors
                        if (s < fmt.size() - 2) {
                            char const cc = fmt[s + 2];
                            switch (cc) {
                                case 'x': out += "\033[40m"; break;
                                case 'r': out += "\033[41m"; break;
                                case 'g': out += "\033[42m"; break;
                                case 'y': out += "\033[43m"; break;
                                case 'b': out += "\033[44m"; break;
                                case 'm': out += "\033[45m"; break;
                                case 'c': out += "\033[46m"; break;
                                case 'w': out += "\033[47m"; break;
                                case 'X': out += "\033[100m"; break;
                                case 'R': out += "\033[101m"; break;
                                case 'G': out += "\033[102m"; break;
                                case 'Y': out += "\033[103m"; break;
                                case 'B': out += "\033[104m"; break;
                                case 'M': out += "\033[105m"; break;
                                case 'C': out += "\033[106m"; break;
                                case 'W': out += "\033[107m"; break;
                                default: break;
                            }
                            s++;
                        }
                        break;
                    default: break;
                }
                s += 2;
            }
        }
        return out;
    }
private:
    std::string default_fmt;
    std::unordered_map<LogLevel, std::string> fmts;
};

class ConsoleSink : public FormattedStringSink
{
public:
    ConsoleSink()
        : FormattedStringSink()
        , m()
    {}
    virtual void log(EntryMetadata const& meta, std::string_view msg) override
    {
        std::string const str = this->formatEntry(meta, msg);
        std::lock_guard g{ this->m };
        std::cout.write(str.c_str(), str.length());
    }
private:
    std::mutex m;
};
inline
std::unique_ptr<FormattedStringSink> makeConsoleSink()
{
    return std::make_unique<ConsoleSink>();
}

class FileSink : public FormattedStringSink
{
public:
    FileSink(std::filesystem::path filename)
        : FormattedStringSink()
        , m()
        , of(filename, std::ios_base::out | std::ios_base::ate | std::ios_base::binary)
    {
        this->of.exceptions(std::ios_base::failbit | std::ios_base::badbit);
    }
    virtual void log(EntryMetadata const& meta, std::string_view msg) override
    {
        std::string const str = this->formatEntry(meta, msg);
        std::lock_guard g{ this->m };
        this->of.write(str.c_str(), str.length());
    }
private:
    std::mutex m;
    std::ofstream of;
};
inline
std::unique_ptr<FormattedStringSink> makeFileSink(std::filesystem::path filename)
{
    return std::make_unique<FileSink>(filename);
}

class Logger final
{
public:
    Logger() = default;
    ~Logger() = default;

    void addSink(std::string name, std::unique_ptr<Sink> sink)
    {
        this->sinks.emplace(name, std::move(sink));
    }
    Sink& getSink(std::string name) const
    {
        auto it = this->sinks.find(name);
        if (it != this->sinks.end())
            return *it->second;
        throw std::runtime_error(std::format("Failed to find sink {0}", name));
    }
    void removeSink(std::string name)
    {
        this->sinks.erase(name);
    }

private:
    void dolog(LogLevel level, std::string_view domain, std::optional<std::string_view> instance, std::source_location src_location, std::string_view fmt, std::format_args args) const
    {
        EntryMetadata const meta = {
            .level = level,
            .domain = domain,
            .instance = instance,
            .source_location = src_location,
            .timestamp = std::chrono::time_point_cast<LogEntryTimestampDuration>(std::chrono::system_clock::now()),
        };
        bool const passed = [&] {
            for (auto&& sink : this->sinks | std::views::values) {
                if (sink->checkFilter(meta))
                    return true;
            }
            return false;
        }();
        if (passed) {
            std::string const msg = std::vformat(fmt, args);
            for (auto&& sink : this->sinks | std::views::values) {
                if (sink->checkFilter(meta))
                    sink->log(meta, msg);
            }
        }
    }

public:
    template <class... Args>
    void log(LogLevel level, std::string_view domain, std::source_location src_location, std::format_string<Args...> fmt, Args&&... args) const
    {
        this->dolog(level, domain, std::nullopt, src_location, fmt.get(), std::make_format_args(args...));
    }

    template <class... Args>
    void log(LogLevel level, std::string_view domain, std::string_view instance, std::source_location src_location, std::format_string<Args...> fmt, Args&&... args) const
    {
        this->dolog(level, domain, instance, src_location, fmt.get(), std::make_format_args(args...));
    }

    template <class ObjectType, class... Args>
        requires std::is_class_v<ObjectType>
    void log(LogLevel level, ObjectType const* obj, std::source_location src_location, std::format_string<Args...> fmt, Args&&... args) const
    {
        auto const domain = [&] {
            if constexpr (HasInstanceGetDomain<ObjectType>) {
                return obj->getDomain();
            }
            else if constexpr (HasClassGetDomain<ObjectType>) {
                return ObjectType::getDomain();
            }
            else {
                return typeid(ObjectType).name();
            }
        }();
        auto const instance = [&] {
            if constexpr (HasGetName<ObjectType>) {
                return obj->getName();
            }
            else {
                return std::format("{}", (void*)obj);
            }
        }();
        this->dolog(level, domain, instance, src_location, fmt.get(), std::make_format_args(args...));
    }
private:
    std::unordered_map<std::string, std::unique_ptr<Sink>> sinks;
};

#ifdef YALF_IMPLEMENTATION
std::unique_ptr<Logger> global_logger = nullptr;
#else
extern std::unique_ptr<Logger> global_logger;
#endif

inline
void setGlobalLogger(std::unique_ptr<Logger> logger)
{
    global_logger = std::move(logger);
}

inline
bool hasGlobalLogger()
{
    return global_logger.get() != nullptr;
}

inline
Logger& getGlobalLogger()
{
    assert(global_logger.get());
    return *global_logger;
}

}

#define LOG_FATAL(domain_or_obj, ...)       ::YALF::getGlobalLogger().log(::YALF::LogLevel::Fatal,    domain_or_obj,    std::source_location::current(), __VA_ARGS__)
#define LOG_FATAL_I(domain, instance, ...)  ::YALF::getGlobalLogger().log(::YALF::LogLevel::Fatal,    domain, instance, std::source_location::current(), __VA_ARGS__)
#define LOG_CRIT(domain_or_obj, ...)        ::YALF::getGlobalLogger().log(::YALF::LogLevel::Critical, domain_or_obj,    std::source_location::current(), __VA_ARGS__)
#define LOG_CRIT_I(domain, instance, ...)   ::YALF::getGlobalLogger().log(::YALF::LogLevel::Critical, domain, instance, std::source_location::current(), __VA_ARGS__)
#define LOG_ERROR(domain_or_obj, ...)       ::YALF::getGlobalLogger().log(::YALF::LogLevel::Error,    domain_or_obj,    std::source_location::current(), __VA_ARGS__)
#define LOG_ERROR_I(domain, instance, ...)  ::YALF::getGlobalLogger().log(::YALF::LogLevel::Error,    domain, instance, std::source_location::current(), __VA_ARGS__)
#define LOG_WARN(domain_or_obj, ...)        ::YALF::getGlobalLogger().log(::YALF::LogLevel::Warning,  domain_or_obj,    std::source_location::current(), __VA_ARGS__)
#define LOG_WARN_I(domain, instance, ...)   ::YALF::getGlobalLogger().log(::YALF::LogLevel::Warning,  domain, instance, std::source_location::current(), __VA_ARGS__)
#define LOG_INFO(domain_or_obj, ...)        ::YALF::getGlobalLogger().log(::YALF::LogLevel::Info,     domain_or_obj,    std::source_location::current(), __VA_ARGS__)
#define LOG_INFO_I(domain, instance, ...)   ::YALF::getGlobalLogger().log(::YALF::LogLevel::Info,     domain, instance, std::source_location::current(), __VA_ARGS__)
#define LOG_DEBUG(domain_or_obj, ...)       ::YALF::getGlobalLogger().log(::YALF::LogLevel::Debug,    domain_or_obj,    std::source_location::current(), __VA_ARGS__)
#define LOG_DEBUG_I(domain, instance, ...)  ::YALF::getGlobalLogger().log(::YALF::LogLevel::Debug,    domain, instance, std::source_location::current(), __VA_ARGS__)
#define LOG_NOISE(domain_or_obj, ...)       ::YALF::getGlobalLogger().log(::YALF::LogLevel::Noise,    domain_or_obj,    std::source_location::current(), __VA_ARGS__)
#define LOG_NOISE_I(domain, instance, ...)  ::YALF::getGlobalLogger().log(::YALF::LogLevel::Noise,    domain, instance, std::source_location::current(), __VA_ARGS__)
