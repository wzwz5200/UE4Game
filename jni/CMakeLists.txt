cmake_minimum_required(VERSION 3.10)

project(UE4Game)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)

# 生成可执行文件 (而不是 .so 库)


add_library(driver STATIC IMPORTED)
set_target_properties(driver PROPERTIES IMPORTED_LOCATION
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/driver.a)


file(GLOB SRC_FILES
    src/*.cpp
    src/**/*.cpp
)

add_executable(UE4Game ${SRC_FILES})



find_library(log-lib log)
target_link_libraries(UE4Game

    driver
    ${log-lib})
