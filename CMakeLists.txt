cmake_minimum_required(VERSION 3.21)

project(UdpMinesweeper
    VERSION 1.0.0
    DESCRIPTION "UDP networked two-player minesweeper battle (Qt UI)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ------------------------------------------------------------------ Qt
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia)

# ------------------------------------------------------------------ core（纯 C++，无 Qt 依赖，可单测）
set(MB_CORE_SRC
    src/core/Board.cpp
    src/core/GameState.cpp
    src/core/RuleEngine.cpp)
set(MB_CORE_HDR
    src/core/Types.h
    src/core/Board.h
    src/core/GameState.h
    src/core/RuleEngine.h)

add_library(mbcore STATIC ${MB_CORE_SRC} ${MB_CORE_HDR})
target_include_directories(mbcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(mbcore PRIVATE /W4)
else()
    target_compile_options(mbcore PRIVATE -Wall -Wextra)
endif()

# ------------------------------------------------------------------ net（UDP，仅标准库 + socket）
set(MB_NET_SRC
    src/net/UdpSocket.cpp
    src/net/HostSession.cpp
    src/net/ClientSession.cpp)
set(MB_NET_HDR
    src/net/Protocol.h
    src/net/UdpSocket.h
    src/net/HostSession.h
    src/net/ClientSession.h)

add_library(mbnet STATIC ${MB_NET_SRC} ${MB_NET_HDR})
target_link_libraries(mbnet PUBLIC mbcore)
target_include_directories(mbnet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(WIN32)
    target_link_libraries(mbnet PUBLIC ws2_32)
endif()

# ------------------------------------------------------------------ ai（三档，仅标准库）
set(MB_AI_SRC
    src/ai/RandomAI.cpp
    src/ai/ProbabilityAI.cpp
    src/ai/LlmAI.cpp)
set(MB_AI_HDR
    src/ai/IAI.h
    src/ai/RandomAI.h
    src/ai/ProbabilityAI.h
    src/ai/LlmAI.h)

add_library(mbai STATIC ${MB_AI_SRC} ${MB_AI_HDR})
target_link_libraries(mbai PUBLIC mbcore)
target_include_directories(mbai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ------------------------------------------------------------------ audio
# Synth 与 MusicLibrary 是纯 C++（无 Qt 依赖），MusicPlayer 用 Qt Multimedia 播放
set(MB_AUDIO_SRC
    src/audio/Synth.cpp
    src/audio/MusicLibrary.cpp
    src/audio/MusicPlayer.cpp)
set(MB_AUDIO_HDR
    src/audio/Synth.h
    src/audio/MusicLibrary.h
    src/audio/MusicPlayer.h)

add_library(mbaudio STATIC ${MB_AUDIO_SRC} ${MB_AUDIO_HDR})
target_link_libraries(mbaudio PUBLIC mbcore Qt6::Core Qt6::Multimedia)
target_include_directories(mbaudio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ------------------------------------------------------------------ 主程序（Qt Widgets）
set(APP_SRC
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/BoardWidget.cpp
    src/ui/BoardWidget.h
    src/ui/MenuScene.cpp
    src/ui/MenuScene.h
    src/ui/Settings.h
    src/ui/SettingsDialog.cpp
    src/ui/SettingsDialog.h)

add_executable(MinesweeperUDP ${APP_SRC})
target_link_libraries(MinesweeperUDP PRIVATE
    mbcore mbnet mbai mbaudio
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia)

# ------------------------------------------------------------------ 测试（控制台，无 Qt GUI）
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE mbcore)

add_executable(net_tests tests/net_tests.cpp)
target_link_libraries(net_tests PRIVATE mbnet)

add_executable(ai_tests tests/ai_tests.cpp)
target_link_libraries(ai_tests PRIVATE mbai)

add_executable(audio_tests tests/audio_tests.cpp)
target_link_libraries(audio_tests PRIVATE mbaudio Qt6::Multimedia)

# ------------------------------------------------------------------ 输出与部署
foreach(t MinesweeperUDP core_tests net_tests ai_tests audio_tests)
    set_target_properties(${t} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endforeach()

# 构建后复制 deploy.bat 到产物目录
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deploy.bat")
    add_custom_command(TARGET MinesweeperUDP POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/deploy.bat"
                "${CMAKE_BINARY_DIR}/bin/deploy.bat")
endif()

message(STATUS "Qt      : ${Qt6_VERSION}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build   : ${CMAKE_BUILD_TYPE}")
