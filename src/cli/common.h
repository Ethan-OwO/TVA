#pragma once

#include <filesystem>
#include <iostream>
#include <string>

#include "core/bytes.h"
#include "core/object_id.h"
#include "core/repository.h"

// Small helpers shared by the cmd_*.cpp files.
namespace tva::cli {

inline core::Repository open_repo() {
  return core::Repository::discover(std::filesystem::current_path());
}

inline std::string short_id(const core::ObjectId& id) { return id.hex().substr(0, 7); }

inline void write_stdout(core::ByteView data) {
  std::cout.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
}

}  // namespace tva::cli
