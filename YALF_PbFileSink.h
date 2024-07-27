// Copyright (c) 2024 Matt M Halenza
// SPDX-License-Identifier: MIT
#pragma once
#include "YALF.h"
#include "Logger.pb.h"
#include <google/protobuf/io/zero_copy_stream_impl.h>

namespace YALF {

inline
DTO::LogEntry encodeDto(EntryMetadata const& meta, std::string_view msg)
{
    DTO::LogEntry entry;
    entry.set_level(static_cast<YALF::DTO::LogLevel>(meta.level));
    entry.set_domain(meta.domain.data());
    if (meta.instance)
        entry.set_instance(meta.instance.value().data());
    entry.set_filename(meta.source_location.file_name());
    entry.set_line(meta.source_location.line());
    entry.set_column(meta.source_location.column());
    entry.set_function(meta.source_location.function_name());

    auto const tp_sec = std::chrono::time_point_cast<std::chrono::seconds>(meta.timestamp);
    std::chrono::nanoseconds const ns = meta.timestamp - tp_sec;
    entry.mutable_timestamp()->set_seconds(tp_sec.time_since_epoch().count());
    entry.mutable_timestamp()->set_nanos(ns.count());

    return entry;
}

class ProtobufFileSink : public Sink
{
public:
    ProtobufFileSink(std::filesystem::path filename)
        : Sink()
        , of(filename, std::ios_base::out | std::ios_base::ate | std::ios_base::binary)
        , oos(&this->of)
        , cos(&this->oos, true)
    {
        this->of.exceptions(std::ios_base::failbit | std::ios_base::badbit);
    }
    virtual void log(EntryMetadata const& meta, std::string_view msg) override
    {
        auto const entry = encodeDto(meta, msg);
        auto const msg_byte_count = entry.ByteSizeLong();

        std::lock_guard g{ this->m };
        cos.WriteVarint64(msg_byte_count);
        entry.SerializeToCodedStream(&this->cos);
    }
private:
    std::mutex m;
    std::ofstream of;
    google::protobuf::io::OstreamOutputStream oos;
    google::protobuf::io::CodedOutputStream cos;
};

inline
std::unique_ptr<Sink> makePbFileSink(std::filesystem::path filename)
{
    return std::make_unique<ProtobufFileSink>(filename);
}

}
