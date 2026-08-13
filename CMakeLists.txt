cmake_minimum_required(VERSION 3.16)
project(sensor_pipeline C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
find_path(MYSQL_INCLUDE_DIR mysql.h PATH_SUFFIXES mysql mariadb)
find_library(MYSQL_CLIENT_LIBRARY NAMES mysqlclient mariadb)
if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_CLIENT_LIBRARY)
    message(FATAL_ERROR "MySQL/MariaDB client development library not found")
endif()

add_library(system_monitor
    src/collector/cpu_monitor.c
    src/collector/memory_monitor.c
    src/storage/mysql_writer.c
)
target_include_directories(system_monitor PUBLIC include ${MYSQL_INCLUDE_DIR})

add_executable(system_monitor_app apps/system_monitor_main.c)
target_link_libraries(system_monitor_app PRIVATE system_monitor Threads::Threads ${MYSQL_CLIENT_LIBRARY})
