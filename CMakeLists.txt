cmake_minimum_required(VERSION 3.24)
project(MinRTS LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)

FetchContent_Declare(EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.14.0
    GIT_SHALLOW TRUE)

# Lua 공식 배포본에는 CMakeLists.txt가 없어서 소스만 받고 아래에서 직접 묶는다.
FetchContent_Declare(lua
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# 샌드박스 GeoJSON(QGIS 내보내기) 파싱용
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(raylib EnTT lua json)

file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM LUA_SOURCES ${lua_SOURCE_DIR}/src/lua.c ${lua_SOURCE_DIR}/src/luac.c)
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR}/src)

# 시뮬레이션 코어: raylib에 의존하지 않는다.
add_library(minrts_core STATIC
    src/core/lua_read.cpp
    src/core/lua_loader.cpp
    src/core/templates.cpp
    src/core/profile.cpp
    src/core/phrases.cpp
    src/sim/hex.cpp
    src/sim/map.cpp
    src/sim/spline.cpp
    src/sim/map_features.cpp
    src/sim/geojson_map.cpp
    src/sim/rules.cpp
    src/sim/simulation.cpp
    src/sim/scenario.cpp
    src/sim/world_systems.cpp
    src/sim/combat.cpp
    src/sim/economy.cpp
    src/sim/profiler.cpp
    src/sim/profile_report.cpp
    src/ai/blackboard.cpp
    src/ai/htn.cpp
    src/ai/utility.cpp
    src/ai/ai_system.cpp)
target_include_directories(minrts_core PUBLIC src)
target_link_libraries(minrts_core PUBLIC EnTT::EnTT lua_static PRIVATE nlohmann_json::nlohmann_json)

add_executable(minrts
    src/main.cpp
    src/app/ui.cpp
    src/app/i18n.cpp
    src/app/render.cpp
    src/app/menu_screen.cpp
    src/app/template_screen.cpp
    src/app/sandbox_screen.cpp
    src/app/game_screen.cpp
    src/app/economy_panel.cpp
    src/app/profiler_overlay.cpp
    src/app/map_view.cpp)
target_link_libraries(minrts PRIVATE minrts_core raylib)

add_executable(minrts_tests tests/sim_tests.cpp)
target_link_libraries(minrts_tests PRIVATE minrts_core)

# 창 없는 샌드박스 벤치마크 / 프로파일 리포트
add_executable(minrts_profile src/tools/profile_main.cpp)
target_link_libraries(minrts_profile PRIVATE minrts_core)

foreach(target minrts_core minrts minrts_tests minrts_profile)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W4 /permissive-)
    endif()
endforeach()

foreach(target minrts minrts_tests minrts_profile)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:${target}>/data)
endforeach()
