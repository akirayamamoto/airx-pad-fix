cmake_minimum_required(VERSION 3.22)
project(airxpadfix LANGUAGES CXX)

add_executable(airxpadfixd src/main.cpp src/xbox_proxy.cpp)
target_compile_features(airxpadfixd PRIVATE cxx_std_17)
target_compile_options(airxpadfixd PRIVATE -Wall -Wextra -Wpedantic -Werror)
