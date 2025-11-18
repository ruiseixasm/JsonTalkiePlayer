/*
JsonTalkiePlayer - Json Talkie Player is intended to be used
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
*/
// Compile as: g++ -shared -fPIC -o JsonTalkiePlayer.so src/JsonTalkiePlayer.cpp -I/single_include/nlohmann -I/include -I/src
#ifndef JSON_TALKIE_PLAYER_CTYPES_HPP
#define JSON_TALKIE_PLAYER_CTYPES_HPP

#include "JsonTalkiePlayer.hpp"

#ifdef _WIN32
    #define DLL_EXPORT __declspec(dllexport)
#else
    #define DLL_EXPORT
#endif

extern "C" {    // Needed for Python ctypes
    DLL_EXPORT int PlayList_ctypes(const char* json_str, int verbose);
    DLL_EXPORT int add_ctypes(int a, int b);
}

#endif // JSON_TALKIE_PLAYER_CTYPES_HPP