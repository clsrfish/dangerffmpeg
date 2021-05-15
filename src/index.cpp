
#include <spdlog/spdlog.h>

#include <iostream>

int hello(int argc, char const* argv[]) {
  spdlog::info("Hello");
  return 0;
}