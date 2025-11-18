#[===[
JsonMidiPlayer - Json Midi Player is intended to be used
in conjugation with the Json Midi Creator to Play its composed Elements
Original Copyright (c) 2024 Rui Seixas Monteiro. All right reserved.
This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.
This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.
https://github.com/ruiseixasm/JsonMidiCreator
https://github.com/ruiseixasm/JsonMidiPlayer
]===]

# List all available generators command: cmake --help
# Run cmake with a specific generator selected: cmake -g "MinGW Makefiles" ..
cmake_minimum_required(VERSION 3.15)
project(JsonMidiPlayer
        VERSION 6.0.0
        DESCRIPTION "Very simple MIDI Player of JSON files"
        HOMEPAGE_URL "https://github.com/ruiseixasm/JsonMidiPlayer"
        LANGUAGES CXX)

# Generates a compile_commands.json file containing the exact compiler calls for all translation units of the project in machine-readable form.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include directories
include_directories(include single_include)

# Add main.cpp explicitly
set(STATIC_SOURCES src/JsonMidiPlayer.cpp src/RtMidi.cpp)

# Create the shared library
add_library(JsonMidiPlayer_library STATIC ${STATIC_SOURCES})
# Add the -fPIC option for position-independent code
set_target_properties(JsonMidiPlayer_library PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Specify output directories
set_target_properties(JsonMidiPlayer_library PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# Add main.cpp explicitly
set(CTYPES_SOURCES src/JsonMidiPlayer_ctypes.cpp)

# Create the shared library
add_library(JsonMidiPlayer_ctypes SHARED ${CTYPES_SOURCES})
# Link to MyLibrary
target_link_libraries(JsonMidiPlayer_ctypes PRIVATE JsonMidiPlayer_library)
# Add the -fPIC option for position-independent code
set_target_properties(JsonMidiPlayer_ctypes PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Specify output directories
set_target_properties(JsonMidiPlayer_ctypes PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# Add main.cpp explicitly
set(EXECUTABLE_SOURCES main.cpp src/JsonMidiPlayer.cpp)

# Set properties for Windows DLL export
if(WIN32)
    set(EXECUTABLE_NAME "JsonMidiPlayer")
else()
    set(EXECUTABLE_NAME "JsonMidiPlayer.out")
endif()

# Add the executable target
add_executable(${EXECUTABLE_NAME} ${EXECUTABLE_SOURCES})
# Link to MyLibrary
target_link_libraries(${EXECUTABLE_NAME} PRIVATE JsonMidiPlayer_library)

# Check if we are on Windows
if (WIN32)  # Try to load ASIO SDK
    add_compile_definitions(__WINDOWS_MM__)
    target_link_libraries(JsonMidiPlayer_library PRIVATE
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
        target_link_libraries(JsonMidiPlayer_library ${ALSA_LIBRARIES})
        add_definitions(-D__LINUX_ALSA__)
    endif()
endif()
