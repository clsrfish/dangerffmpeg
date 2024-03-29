
#include <spdlog/spdlog.h>

#include <iostream>
#include <string>

#include "./config.h"
#include "./src/tutorial01.hpp"
#include "./src/tutorial02.hpp"
#include "./src/tutorial03.hpp"
#include "./src/tutorial04.hpp"
#include "./src/tutorial05.hpp"
#include "./src/tutorial06.hpp"
#include "./src/tutorial07.hpp"

int main(int argc, char const *argv[]) {
  spdlog::set_level(spdlog::level::debug);  // Set global log level to debug
  spdlog::info("Welcome to DangerFFmpefg {}:{}:{}!", PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR,
               PROJECT_VERSION_PATCH);

  return tutorial07::main(argc, argv);
}