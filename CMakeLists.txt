cmake_minimum_required(VERSION 3.20)
project(MQTT_broker C)

set(CMAKE_C_STANDARD 99)
find_package( Threads )
add_executable(MQTT_broker main.c lib/set.c lib/set.h)
target_link_libraries( MQTT_broker ${CMAKE_THREAD_LIBS_INIT} )