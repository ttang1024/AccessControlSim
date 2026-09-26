add_library(acs_core
  core/cardholder.cpp
  core/deny_reason.cpp
  core/schedule.cpp
  core/system_clock.cpp
)
target_include_directories(acs_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(acs_core PRIVATE acs_warnings)

add_library(acs_engine
  engine/access_decision_engine.cpp
)
target_link_libraries(acs_engine PUBLIC acs_core PRIVATE acs_warnings)

add_library(acs_events
  events/audit_log_writer.cpp
  events/event_queue.cpp
  events/reader_monitor.cpp
)
target_link_libraries(acs_events PUBLIC acs_engine PRIVATE spdlog::spdlog acs_warnings)

add_library(acs_storage
  storage/database.cpp
  storage/schema.cpp
  storage/seed_loader.cpp
  storage/sqlite_access_data_repository.cpp
  storage/sqlite_event_repository.cpp
  storage/statement.cpp
  storage/transaction.cpp
)
target_link_libraries(acs_storage
  PUBLIC acs_events acs_engine ${ACS_SQLITE_TARGET}
  PRIVATE nlohmann_json::nlohmann_json acs_warnings)

add_library(acs_net
  net/access_request_handler.cpp
  net/frame_codec.cpp
  net/message_codec.cpp
  net/reader_watchdog.cpp
  net/server.cpp
  net/session.cpp
)
target_link_libraries(acs_net
  PUBLIC acs_engine acs_events asio
  PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog acs_warnings)

add_library(acs_sim
  sim/virtual_reader.cpp
)
target_link_libraries(acs_sim PUBLIC acs_net asio PRIVATE acs_warnings)

# Everything in server/ except main(), so it can be unit tested.
add_library(acs_server_lib
  server/logging.cpp
  server/server_config.cpp
)
target_include_directories(acs_server_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(acs_server_lib
  PUBLIC spdlog::spdlog
  PRIVATE nlohmann_json::nlohmann_json acs_warnings)

add_executable(acs_server
  server/main.cpp
)
target_link_libraries(acs_server PRIVATE acs_server_lib acs_net acs_storage acs_warnings)

add_executable(acs_reader_sim
  sim/main.cpp
)
target_link_libraries(acs_reader_sim PRIVATE acs_sim acs_warnings)
