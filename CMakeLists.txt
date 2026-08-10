cmake_minimum_required(VERSION 3.16)
project(sensor_pipeline C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)

add_library(system_monitor
    src/collector/cpu_monitor.c
    src/collector/memory_monitor.c
)
target_include_directories(system_monitor PUBLIC include)

add_executable(system_monitor_app apps/system_monitor_main.c)
target_link_libraries(system_monitor_app PRIVATE system_monitor Threads::Threads)
