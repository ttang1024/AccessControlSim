FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Clang 21 flags a char8_t conversion inside GoogleTest's own printers. Silence
# it for the third-party target only; our targets keep their full warning set.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wno-character-conversion ACS_HAS_WNO_CHARACTER_CONVERSION)
if(ACS_HAS_WNO_CHARACTER_CONVERSION)
  target_compile_options(gtest PRIVATE -Wno-character-conversion)
endif()

# Shared main(): silences logging so test output stays readable.
add_library(acs_test_main STATIC test_main.cpp)
target_link_libraries(acs_test_main PUBLIC GTest::gtest PRIVATE spdlog::spdlog)

add_executable(acs_unit_tests
  unit/core/cardholder_test.cpp
  unit/core/deny_reason_test.cpp
  unit/core/schedule_test.cpp
  unit/core/system_clock_test.cpp
  unit/engine/access_decision_engine_test.cpp
  unit/events/audit_log_writer_test.cpp
  unit/events/bounded_queue_test.cpp
  unit/events/event_queue_test.cpp
  unit/events/reader_monitor_test.cpp
  unit/net/access_request_handler_test.cpp
  unit/net/frame_codec_test.cpp
  unit/net/message_codec_test.cpp
  unit/net/reader_watchdog_test.cpp
  unit/net/session_test.cpp
  unit/server/server_config_test.cpp
  unit/storage/database_test.cpp
  unit/storage/seed_loader_test.cpp
  unit/storage/sqlite_access_data_repository_test.cpp
  unit/storage/sqlite_event_repository_test.cpp
)
target_include_directories(acs_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(acs_unit_tests PRIVATE acs_net acs_storage acs_server_lib acs_warnings acs_test_main)
# Lets tests find config/seed.json, to check the shipped seed file is valid.
target_compile_definitions(acs_unit_tests PRIVATE ACS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(GoogleTest)
# Both executables use real sockets (SessionTest and the integration tests).
# A blocking test client would hang forever if the code under test misbehaved,
# so fail fast instead. Under TSan, apply the Asio suppression (see tsan.supp).
set(ACS_TEST_ENV "")
if(ACS_ENABLE_TSAN)
  set(ACS_TEST_ENV "TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan.supp")
endif()
gtest_discover_tests(acs_unit_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "${ACS_TEST_ENV}")

add_executable(acs_integration_tests
  integration/server_integration_test.cpp
)
target_include_directories(acs_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(acs_integration_tests PRIVATE acs_sim acs_storage acs_warnings acs_test_main)
gtest_discover_tests(acs_integration_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "${ACS_TEST_ENV}")
