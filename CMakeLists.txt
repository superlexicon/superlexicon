cmake_minimum_required(VERSION 3.16)

if(EXISTS "/usr/lib/gcc/x86_64-pc-linux-gnu/16/cc1")
    set(CMAKE_C_FLAGS_INIT "-B/usr/lib/gcc/x86_64-pc-linux-gnu/16/")
    set(CMAKE_CXX_FLAGS_INIT "-B/usr/lib/gcc/x86_64-pc-linux-gnu/16/")
endif()

project(SuperLexicon C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

set(COMMON_WARN_FLAGS
    -Wno-multichar
    -Wno-deprecated-declarations
    -Wno-unused-result
    -Wno-unused-variable
    -Wno-format
    -Wno-return-type
    -Wno-sign-compare
    -Wno-unused-label
)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
endif()
if(NOT SDL2_FOUND)
    find_package(SDL2 REQUIRED)
    set(SDL2_LIBRARIES SDL2::SDL2)
endif()

file(GLOB_RECURSE LVGL_SOURCES "${CMAKE_SOURCE_DIR}/shell/vendor/lvgl/src/*.c")
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl SYSTEM PUBLIC
    "${CMAKE_SOURCE_DIR}/shell/vendor"
    "${CMAKE_SOURCE_DIR}/shell/vendor/lvgl"
)
target_compile_options(lvgl PRIVATE ${COMMON_WARN_FLAGS} -O2)

add_library(cjson STATIC "${CMAKE_SOURCE_DIR}/shell/vendor/cjson/cJSON.c")
target_include_directories(cjson SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/shell/vendor/cjson")

set(DASHBOARD_CSRCS
    "${CMAKE_SOURCE_DIR}/shell/vendor/dashboard/ui/dashboard.c"
    "${CMAKE_SOURCE_DIR}/shell/vendor/dashboard/ui/superapp_views.c"
    "${CMAKE_SOURCE_DIR}/shell/vendor/dashboard/ui/logo_horizontal.c"
    "${CMAKE_SOURCE_DIR}/shell/vendor/dashboard/ui/app_icon.c"
    "${CMAKE_SOURCE_DIR}/shell/vendor/dashboard/ws.c"
    "${CMAKE_SOURCE_DIR}/shell/vendor/dashboard/fonts/outfit_28.c"
    "${CMAKE_SOURCE_DIR}/shell/vendor/dashboard/fonts/outfit_16.c"
    "${CMAKE_SOURCE_DIR}/shell/vendor/dashboard/fonts/jbm_13.c"
    "${CMAKE_SOURCE_DIR}/shell/vendor/dashboard/fonts/jbm_15.c"
)

add_executable(superlexicon
    "${CMAKE_SOURCE_DIR}/shell/main.cpp"
    "${CMAKE_SOURCE_DIR}/shell/supervisor.cpp"
    ${DASHBOARD_CSRCS}
)

target_include_directories(superlexicon PRIVATE
    "${CMAKE_SOURCE_DIR}/shell"
    "${CMAKE_SOURCE_DIR}/shell/vendor"
    "${CMAKE_SOURCE_DIR}/shell/vendor/lvgl"
    "${CMAKE_SOURCE_DIR}/shell/vendor/dashboard"
    "${CMAKE_SOURCE_DIR}/shell/vendor/cjson"
    ${SDL2_INCLUDE_DIRS}
)

target_compile_options(superlexicon PRIVATE ${COMMON_WARN_FLAGS} -O2)
target_link_directories(superlexicon PRIVATE ${SDL2_LIBRARY_DIRS})
target_link_libraries(superlexicon PRIVATE
    lvgl
    cjson
    ${SDL2_LIBRARIES}
    pthread
    m
)

add_executable(lexicon-radio-bridge
    "${CMAKE_SOURCE_DIR}/radio-bridge/lexicon_radio_bridge.cpp"
)
target_compile_options(lexicon-radio-bridge PRIVATE ${COMMON_WARN_FLAGS} -O2)
target_link_libraries(lexicon-radio-bridge PRIVATE pthread)
