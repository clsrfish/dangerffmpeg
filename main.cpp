
#include <spdlog/spdlog.h>

#include <iostream>

#include "./config.h"
#include "./src/tutorial01.hpp"

int main(int argc, char const *argv[]) {
  spdlog::set_level(spdlog::level::debug);  // Set global log level to debug
  spdlog::info("Welcome to DangerFFmpefg {}:{}:{}!", PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR,
               PROJECT_VERSION_PATCH);

  return tutorial01::main(argc, argv);
}