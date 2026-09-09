cmake_minimum_required(VERSION 3.16)
project(vantage_sim LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 14)

find_package(SDL2 REQUIRED)

# Tell the vendored LVGL to use sim/lv_conf.h and build for desktop.
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH
    "Path to the simulator lv_conf.h" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(
  "${CMAKE_CURRENT_SOURCE_DIR}/../components/lvgl"
  "${CMAKE_CURRENT_BINARY_DIR}/lvgl")

# Add SDL2 include path to LVGL target to fix header issues
target_include_directories(lvgl PRIVATE ${SDL2_INCLUDE_DIRS})

add_executable(vantage_sim
  main.c
  wm.c
  host_wifi.c
  sdk_bridge.c
  apps/registry.c
  "${CMAKE_CURRENT_SOURCE_DIR}/../main/wm_anim.c"
)

target_include_directories(vantage_sim PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../main"
  "${CMAKE_CURRENT_SOURCE_DIR}/../app_sdk"
  ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(vantage_sim PRIVATE lvgl ${SDL2_LIBRARIES})

if(WIN32)
  target_link_libraries(vantage_sim PRIVATE wlanapi)
endif()
