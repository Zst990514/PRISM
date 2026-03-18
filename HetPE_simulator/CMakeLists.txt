CMAKE_MINIMUM_REQUIRED(VERSION 3.10)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
PROJECT(triangle_counting)
set(CMAKE_BUILD_TYPE "Debug")

add_executable(triangle_counting src/main.cpp src/mem.cpp src/helper.cpp)
add_executable(preprocess ./src/preprocess.cpp)

# For Graph Analyzer
add_executable(graph_analyzer
    src/graph_analyzer.cpp
    src/mem.cpp
    src/helper.cpp)
