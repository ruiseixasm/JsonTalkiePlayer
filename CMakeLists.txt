#[===[
JsonTalkiePlayer - Json Midi Player is intended to be used
in conjugation with the Json Midi Creator to Play its composed Elements
Original Copyright (c) 2025 Rui Seixas Monteiro. All right reserved.
This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.
This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.
https://github.com/ruiseixasm/JsonMidiCreator
https://github.com/ruiseixasm/JsonTalkiePlayer
]===]

# List all available generators command: cmake --help
# Run cmake with a specific generator selected: cmake -g "MinGW Makefiles" ..
cmake_minimum_required(VERSION 3.15)
project(JsonTalkiePlayer
        VERSION 1.0.0
        DESCRIPTION "Very simple Talkie Player of JSON files trough the JsonTalkie implementation"
        HOMEPAGE_URL "https://github.com/ruiseixasm/JsonTalkiePlayer"
        LANGUAGES CXX)

# Generates a compile_commands.json file containing the exact compiler calls for all translation units of the project in machine-readable form.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include directories
include_directories(include single_include)

# Add main.cpp explicitly
set(STATIC_SOURCES src/JsonTalkiePlayer.cpp src/RtMidi.cpp)

# Create the shared library
add_library(JsonTalkiePlayer_library STATIC ${STATIC_SOURCES})
# Add the -fPIC option for position-independent code
set_target_properties(JsonTalkiePlayer_library PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Specify output directories
set_target_properties(JsonTalkiePlayer_library PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# Add main.cpp explicitly
set(CTYPES_SOURCES src/JsonTalkiePlayer_ctypes.cpp)

# Create the shared library
add_library(JsonTalkiePlayer_ctypes SHARED ${CTYPES_SOURCES})
# Link to MyLibrary
target_link_libraries(JsonTalkiePlayer_ctypes PRIVATE JsonTalkiePlayer_library)
# Add the -fPIC option for position-independent code
set_target_properties(JsonTalkiePlayer_ctypes PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Specify output directories
set_target_properties(JsonTalkiePlayer_ctypes PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# Add main.cpp explicitly
set(EXECUTABLE_SOURCES main.cpp src/JsonTalkiePlayer.cpp)

# Set properties for Windows DLL export
if(WIN32)
    set(EXECUTABLE_NAME "JsonTalkiePlayer")
else()
    set(EXECUTABLE_NAME "JsonTalkiePlayer.out")
endif()

# Add the executable target
add_executable(${EXECUTABLE_NAME} ${EXECUTABLE_SOURCES})
# Link to MyLibrary
target_link_libraries(${EXECUTABLE_NAME} PRIVATE JsonTalkiePlayer_library)

# Check if we are on Windows
if (WIN32)  # Try to load ASIO SDK
    add_compile_definitions(__WINDOWS_MM__)
    target_link_libraries(JsonTalkiePlayer_library PRIVATE
            winmm.lib
            ws2_32.lib
            wininet.lib
            version.lib
            Shlwapi.dll
        )
else()
    # Find and link ALSA library
    find_package(ALSA REQUIRED)
    if (ALSA_FOUND)
        include_directories(${ALSA_INCLUDE_DIRS})
        target_link_libraries(JsonTalkiePlayer_library ${ALSA_LIBRARIES})
        add_definitions(-D__LINUX_ALSA__)
    endif()
endif()
