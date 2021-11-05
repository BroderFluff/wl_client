cmake_minimum_required(VERSION 3.15)

project(wayland_client CXX)

add_executable(wl_client src/main.cpp wl/xdg-shell-protocol.o)

target_include_directories(wl_client PRIVATE wl/)
target_link_directories(wl_client PRIVATE wl/)
target_link_libraries(wl_client PRIVATE wayland-client)