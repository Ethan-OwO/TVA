#pragma once

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "core/bytes.h"
#include "core/object_id.h"
#include "core/repository.h"

// Small helpers shared by the cmd_*.cpp files.
namespace tva::cli {

inline core::Repository open_repo() {
  return core::Repository::discover(std::filesystem::current_path());
}

inline std::string short_id(const core::ObjectId& id) { return id.hex().substr(0, 7); }

// Like git's repeated -m: each paragraph becomes its own line(s), separated
// by a blank line.
inline std::string join_message_paragraphs(const std::vector<std::string>& paragraphs) {
  std::string msg;
  for (const auto& p : paragraphs) {
    if (!msg.empty()) msg += '\n';
    msg += p;
    if (!msg.empty() && msg.back() != '\n') msg += '\n';
  }
  return msg;
}

inline void write_stdout(core::ByteView data) {
  std::cout.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
}

}  // namespace tva::cli
