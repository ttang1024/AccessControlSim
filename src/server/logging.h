#pragma once

#include <spdlog/common.h>

namespace acs::server {

// Sets up the process-wide spdlog logger. Output goes to stdout as one line
// per event, in logfmt style:
//   2026-09-24T10:15:02.123+0000 level=warn thread=4242 event=reader_offline reader=R-101
// A timestamp plus key=value pairs is easy for people to read and for tools
// such as grep, Loki or Splunk to parse (ADR-009).
void configureLogging(spdlog::level::level_enum level);

}  // namespace acs::server
