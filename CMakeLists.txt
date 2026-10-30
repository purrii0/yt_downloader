cmake_minimum_required(VERSION 3.16)
project(ytpl CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CURSES_NEED_WIDE TRUE)  # ncursesw: needed for UTF-8 titles
find_package(Curses REQUIRED)
find_package(Threads REQUIRED)

add_executable(ytpl
  src/main.cpp src/app.cpp src/cli.cpp src/tui.cpp src/tracker.cpp
  src/ytdlp.cpp src/select.cpp src/util.cpp src/downloader.cpp src/config.cpp)
target_include_directories(ytpl PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(ytpl PRIVATE ${CURSES_LIBRARIES} Threads::Threads)
target_compile_options(ytpl PRIVATE -Wall -Wextra)

add_executable(select_test tests/select_test.cpp src/select.cpp)
target_include_directories(select_test PRIVATE src)
enable_testing()
add_test(NAME select COMMAND select_test)
