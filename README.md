# Yet Another Loggig Framework

A C++20-first Logging Framework (because what's one more on top of a couple thousand options?)

## Table of Contents
- [Getting Started](#getting-started)
- [Logging Messages](#logging-messages)
- [Logger Configuration](#logger-configuration)
    - [Sinks](#sinks)
    - [Filtering](#filtering)
    - [FormattedStringSink](#formattedstringsink)
    - [ConsoleSink](#consolesink)
    - [FileSink](#filesink)
    - [Other Possible Sinks](#other-possible-sinks)
- [Format String Reference](#format-string-reference)

## Getting Started
YALF is a header-only library, and as such it can simply be copied to your project's source tree.

Exactly one file must define `YALF_IMPLEMENTATION` before this header is included.
This is to give storage for the global logger object, which is used by the various `LOG_*` macros.

## Logging Messages
To log a message, use one of the `LOG_*` macros:
- `LOG_FATAL()` Errors that need to halt the program immediately
- `LOG_CRIT()` Errors that MUST be corrected, but do not terminate the program
- `LOG_NOTICE()` Normal, but significant conditions
- `LOG_ERROR()` Errors
- `LOG_WARN()` Warnings
- `LOG_INFO()` Informational
- `LOG_DEBUG()` Debug
- `LOG_NOISE()` Debugging Messages that are usually ignored

The LOG macros use a ``std::format()`` style API (influenced by `printf()`) - you provide a "format" string as well as arguments that get substituted into the format string.
For example: `LOG_INFO("Domain", "This is the log message.  42=0x{:x}", 42);` will log a message with the string `"This is the log message.  42=0x2a"`

To use custom objects in format strings, simply provide `std::formatter<>` specializations, exactly as you would when using `std::format()` (because YALF uses `std::format()` internally!)

Each log entry has an associated "domain" and an (optional) "instance" field.
This is to facilitate logging within objects that may have more than one instance - "domain" is essentially the class, while "instance" is some kind of identifier of the instance.
There are a number of ways for domain/instance to be added to the entry:
- `LOG_INFO("This is the Domain String", ...)`  Domain is "This is the Domain String", the instance field is `std::nullopt`.
- `LOG_INFO_I("Domain", "Instance", ...)`  The domain is "Domain", the instance is "Instance".
- `LOG_INFO(some_object, ...)`  Automatically pull domain and instance fields from the object.  This is the simplest to use but requires some participation from the objects.

For the last example, YALF will use a number of methods to determine the domain and instance strings:
- If the class does nothing special, YALF will use `typeinfo` to get a string for the domain and will use the address of the object for the instance.
- If the class provides a `getName()` (possibly const) member function that returns a `std::string_view`, then YALF will call that to get the instance string.
- If the class provides a `getDomain()` (possiby static or const) member function that returns a `std::string_view`, then YALF will call that to get the domain string.
  `getDomain()` may be static, non-static, or even virtual.

For both `getName()` and `getDomain()` the return value does not need to be exactly `std::string_view` it only needs to be `convertible_to` one.
See the concepts `HasGetName`, `HasInstanceGetDomain`, and `HasClassGetDomain` in YALF.h

## Logger Configuration
Logging is funneled though the `Logger` class but it is actually the various `Sinks` that *do* things with the message, such as printing to the console or storing in a log file.

### Sinks
All Sinks derive from `Sink` which has a single virtual member function to perform logging for a message:
```cpp
virtual void log(EntryMetadata const& meta, LogEntryTimestamp const& timestamp, std::string_view msg) = 0;
```

`EntryMetadata` is a struct containing metadata about the message:
```cpp
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
```
The timestamp granularity is std::micro (microseconds) and uses std::chrono::system_clock by default.
These can be changed by defining `YALF_TIMESTAMP_RESOLUTION` and/or `YALF_TIMESTAMP_CLOCK` before the header is included.

By default, the timestamp will be in whatever timezone the `YALF_TIMESAMP_CLOCK` (default: `std::chrono::system_clock`) is in, which is likely UTC (Unix Time).
To configure YALF to use localtime, define `YALF_USE_LOCALTIME` before including the header.

### Filtering
`Sink` derives from `Filter` which performs filtering on log entries.

Filtering is configured via the three member functions:
```cpp
void setDefaultLogLevel(LogLevel level);
void setDomainLogLevel(std::string_view domain, LogLevel level);
void clearDomainLogLevel(std::string_view domain);
```

- `setDefaultLogLevel()` sets the minimum log level that an entry must achieve in order for the Sink to be interested in it.
eg. if the default log level is set to `Warn` then only `Fatal`, `Critical`, `Error`, and `Warning` level messages will be considered by that sink, all other levels will be ignored.

- `setDomainLogLevel()` set a per-domain log level (and the default log level is ignored).

- `clearDomainLogLevel()` removes the per-domain log level (returning that domain to the default log level).

- `checkFilter()` is used by `Logger` to determine if the Sink is interested in a given log entry.

All of this functionality is defined `virtual` so `Sink` subclasses may override and completely change that behavior if they wish.

### FormattedStringSink
`FormattedStringSink` is a subclass of `Sink` that provides logging-focused string formatting of the entry metadata into a single string.
It is used by the YALF-provided sinks `ConsoleSink` and `FileSink` to format the final log string that is written to the console or log file.

To configure `FormattedStringSink` subclasses, use these methods:
```cpp
    void setFormat(std::string_view fmt);
    void setFormat(LogLevel level, std::string_view fmt);
    void clearFormat(LogLevel level);
```

- `setFormat(std::string_view fmt)` sets the default format for all log levels.
See [Format String Reference](#format-string-reference) for the special identifiers used by the `fmt`.
If a default format is never set, then `"%H:%M:%S %F:%l %D[%I] %L:  %x%R%n"` is used.

- `setFormat(LogLevel level, std::string_view fmt)` sets a per-log-level format that overrides the default.
This is mainly used by `ConsoleSink` to provide colored output for different log levels.

- `clearFormat(LogLevel level)` clears the per-log-level format and returns that log level to the default.

Custom subclasses should use `std::string formatEntry(EntryMetadata const& meta, std::string_view msg)` to turn a log entry into a singular string.

### ConsoleSink
`ConsoleSink` requires very little configuration (other than what is provided by the base `Filter` and `FormattedStringSink` base classes).

It can be instantiated with `YALF::makeConsoleSink()`.

### FileSink
`FileSink` requires very little configuration (other than what is provided by the base `Filter` and `FormattedStringSink` base classes).

It can be instantiated with `YALF::makeFileSink(std::filesystem::path filename)`.
The file will be created if it doesn't exist and opened for append if it does exist.
The file is opened once when the FileSink is created, eg. log rotation is not supported.

### PbFileSink
Requires the header `YALF_PbFileSink.h` to be included.
Requires `Logger.proto` to be used with protoc to generate `Logger.pb.cc` and `Logger.pb.h`.
Typical way to do this is with `protoc --cpp_out=. ./Logger.proto`.

`PbFileSink` requires very little configuration (other than what is provided by the base `Filter` and `FormattedStringSink` base classes).

It can be instantiated with `YALF::makePbFileSink(std::filesystem::path filename)`.

### Other Possible Sinks
Here's a list of other sinks that the author envisions but are not yet implemented:

- `SyslogSink` puts entries into `syslog` on unix-like systems.
- `WinEventSink` puts entries into Windows Event Log.
- `JournaldSink` puts entires into journald on systemd-based Linux systems.
    This could utilize the metadata mechanisms that Journald provides to keep the metadata structured in the final log database.
- `TcpServerSink` opens a listening TCP socket and sends messages to clients.
- `TcpClientSink` opens a TCP socket to a defined remote endpoint and sends messages.
- `ProtobufFileSink`, `ProtobufTcpServerSink`, and `ProtobufTcpClientSink` are similar to `FileSink`, `TcpServerSink`, and `TcpClientSink`, respectively, but instead of writing textual messages writes protobuf-encoded messages

## Example Setup
```cpp
auto logger = std::make_unique<YALF::Logger>();
{
    auto console_sink = YALF::makeConsoleSink();
    console_sink->setDefaultLogLevel(YALF::LogLevel::Info); // Show messages of Info or higher level
    console_sink->setFormat("%H:%M:%S %D[%I] %L:  %x%n"); // Give all levels a useful format
    console_sink->setFormat(YALF::LogLevel::Fatal, "%Qr%H:%M:%S %D[%I] %L:  %x%R%n"); // Make Fatal-level messages have a red background
    console_sink->setDomainLogLevel("SomeNoisyDomain", YALF::LogLevel::Error); // Only show Error and higher level messages from "SomeNoisyDomain"
    logger->addSink("ConsoleSink", std::move(console_sink));
}
{
    auto const log_filename = std::format("{:%Y.%m.%d_%H.%M.%S}.txt", std::chrono::system_clock::now());
    auto file_sink = YALF::makeFileSink(log_filename);
    file_sink->setDefaultLogLevel(YALF::LogLevel::Debug); // Log all messages except Noise
    file_sink->setFormat("%y/%m/%d %H:%M:%S %F:%l %D[%I] %L:  %x%n"); // Log all metadata from messages
    logger->addSink("FileSink", std::move(file_sink));
}
YALF::setGlobalLogger(std::move(logger));
```

## Format String Reference
Timestamps use the local timezone for convenience.

| Placeholder | Description |
| :---------: | ----------- |
| `%%` | A literal "%" |
| `%n` | A newline (\r\n when compiling for MSVC, \n otherwise) |
| `%y` | Timestamp: Writes the last two decimal digits of the year. If the result is a single digit it is prefixed by 0. |
| `%Y` | Timestamp: Writes the year as a decimal number. If the result is less than four digits it is left-padded with 0 to four digits. |
| `%b` | Timestamp: Writes the locale's abbreviated month name. |
| `%B` | Timestamp: Writes the locale's full month name. |
| `%m` | Timestamp: Writes the month as a decimal number (January is 01). If the result is a single digit, it is prefixed with 0. |
| `%d` | Timestamp: Writes the day of month as a decimal number. If the result is a single decimal digit, it is prefixed with 0. |
| `%e` | Timestamp: Writes the day of month as a decimal number. If the result is a single decimal digit, it is prefixed with a space. |
| `%a` | Timestamp: Writes the locale's abbreviated weekday name. |
| `%A` | Timestamp: Writes the locale's full weekday name. |
| `%H` | Timestamp: Writes the hour (24-hour clock) as a decimal number. If the result is a single digit, it is prefixed with 0. |
| `%M` | Timestamp: Writes the minute as a decimal number. If the result is a single digit, it is prefixed with 0. |
| `%S` | Timestamp: Writes the second as a decimal number. If the number of seconds is less than 10, the result is prefixed with 0. |
| `%F` | Source: Filename with path stripped out. |
| `%f` | Source: Filename with path. |
| `%l` | Source: Line number. |
| `%c` | Source: Column number. |
| `%D` | Domain identifier. |
| `%I` | Instance identifier. |
| `%L` | Log level string, left padded with spaces. |
| `%x` | Log message string. |
| `%R` | Reset foreground and background colors to default. |
| `%Cx` | Set Foreground Color: Black |
| `%Cr` | Set Foreground Color: Red |
| `%Cg` | Set Foreground Color: Green |
| `%Cy` | Set Foreground Color: Yellow  |
| `%Cb` | Set Foreground Color: Blue |
| `%Cm` | Set Foreground Color: Magenta |
| `%Cc` | Set Foreground Color: Cyan |
| `%Cw` | Set Foreground Color: White (Light Gray) |
| `%CX` | Set Foreground Color: Bright Black (Dark Gray) |
| `%CR` | Set Foreground Color: Bright Red |
| `%CG` | Set Foreground Color: Bright Green |
| `%CY` | Set Foreground Color: Bright Yellow |
| `%CB` | Set Foreground Color: Bright Blue |
| `%CM` | Set Foreground Color: Bright Magenta |
| `%CC` | Set Foreground Color: Bright Cyan |
| `%CW` | Set Foreground Color: Bright White |
| `%Qx` | Set Background Color: Black |
| `%Qr` | Set Background Color: Red |
| `%Qg` | Set Background Color: Green |
| `%Qy` | Set Background Color: Yellow  |
| `%Qb` | Set Background Color: Blue |
| `%Qm` | Set Background Color: Magenta |
| `%Qc` | Set Background Color: Cyan |
| `%Qw` | Set Background Color: White (Light Gray) |
| `%QX` | Set Background Color: Bright Black (Dark Gray) |
| `%QR` | Set Background Color: Bright Red |
| `%QG` | Set Background Color: Bright Green |
| `%QY` | Set Background Color: Bright Yellow |
| `%QB` | Set Background Color: Bright Blue |
| `%QM` | Set Background Color: Bright Magenta |
| `%QC` | Set Background Color: Bright Cyan |
| `%QW` | Set Background Color: Bright White |
