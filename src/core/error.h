#pragma once

#include <stdexcept>

namespace tva::core {

// Every expected failure (bad input, corrupt object, missing ref, ...) throws
// this. The message has no "fatal:" prefix; the CLI adds it.
class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

}  // namespace tva::core
