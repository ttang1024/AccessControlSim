cmake_minimum_required(VERSION 3.24)
project(AccessControlSim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(ACS_BUILD_TESTS "Build unit and integration tests" ON)
option(ACS_ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(ACS_ENABLE_TSAN "Build with ThreadSanitizer" OFF)

if(ACS_ENABLE_SANITIZERS AND ACS_ENABLE_TSAN)
  message(FATAL_ERROR "ASan and TSan cannot be combined; use separate build directories")
endif()

# Applied globally (including to third-party code) so every object in the
# binary is instrumented consistently.
if(ACS_ENABLE_SANITIZERS)
  if(MSVC)
    add_compile_options(/fsanitize=address)
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

if(ACS_ENABLE_TSAN)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()

# --- Third-party dependencies ------------------------------------------------
include(FetchContent)
find_package(Threads REQUIRED)

# Standalone Asio is header-only and ships without CMake support, so we wrap it
# in an interface target. SYSTEM stops its headers tripping our -Werror.
FetchContent_Declare(
  asio
  URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-34-2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(asio)
add_library(asio INTERFACE)
target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(asio INTERFACE Threads::Threads)

FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
  spdlog
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(spdlog)
# Treat spdlog's headers as system headers, so they can't trip our -Werror.
get_target_property(ACS_SPDLOG_INCLUDES spdlog INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(spdlog PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${ACS_SPDLOG_INCLUDES}")

# SQLite ships with macOS and every Linux distro (libsqlite3-dev), so use the system copy.
find_package(SQLite3 REQUIRED)
# CMake 4.3 renamed the imported target; older versions only have the old name.
if(TARGET SQLite3::SQLite3)
  set(ACS_SQLITE_TARGET SQLite3::SQLite3)
else()
  set(ACS_SQLITE_TARGET SQLite::SQLite3)
endif()

# Strict warnings apply to our own targets only, via this interface library.
add_library(acs_warnings INTERFACE)
if(MSVC)
  target_compile_options(acs_warnings INTERFACE /W4 /WX)
else()
  target_compile_options(acs_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

add_subdirectory(src)

if(ACS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

find_program(CLANG_FORMAT_EXE clang-format)
if(CLANG_FORMAT_EXE)
  file(GLOB_RECURSE ACS_FORMAT_SOURCES CONFIGURE_DEPENDS
       ${CMAKE_SOURCE_DIR}/src/*.h ${CMAKE_SOURCE_DIR}/src/*.cpp
       ${CMAKE_SOURCE_DIR}/tests/*.h ${CMAKE_SOURCE_DIR}/tests/*.cpp)
  add_custom_target(format COMMAND ${CLANG_FORMAT_EXE} -i ${ACS_FORMAT_SOURCES}
                    COMMENT "Running clang-format")
endif()
