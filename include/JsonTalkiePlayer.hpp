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
#ifndef JSON_TALKIE_PLAYER_HPP
#define JSON_TALKIE_PLAYER_HPP

#include <iostream>
#include <string>
#include <array>
#include <vector>
#include <list>
#include <algorithm>
#include <cmath>                // For std::round
#include <cstdlib>
#include <thread>               // Include for std::this_thread::sleep_for
#include <chrono>               // Include for std::chrono::seconds
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <iomanip>              // For std::fixed and std::setprecision
// Needed for UDP sockets
#include <cstring>

#ifdef _WIN32
    #define NOMINMAX    // disables the definition of min and max macros.
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <Windows.h>
    #include <processthreadsapi.h> // For SetProcessInformation
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket   // Compatible with Linux naming
#else
    #include <pthread.h>
    #include <time.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

// External libraries
#include <nlohmann/json.hpp>    // Include the JSON library


// #define DEBUGGING true
#define FILE_TYPE "Json Midi Player"
#define FILE_URL  "https://github.com/ruiseixasm/JsonMidiPlayer"
#define VERSION   "1.0.0"
#define DRAG_DURATION_MS (1000.0/((120/60)*24))


enum MessageCode {
    talk,
    list,
    run,
    set,
    get,
    sys,
    echo,
    error,
    channel
};



class TalkieDevice;


class TalkieSocket {
public:
    // Intended to have their IPs updated based on the response (echo)
    std::unordered_map<std::string, TalkieDevice> devices_by_name;

private:
    const bool verbose;
    int sockfd = -1;  // ONE socket for everything
    bool socket_initialized = false;
    static struct sockaddr_in server_addr;
    
public:
    TalkieSocket(bool verbose = false) : verbose(verbose) { }
    ~TalkieSocket() { closeSocket(); }
    
    // Explicitly delete copy assignment
    TalkieSocket& operator=(const TalkieSocket&) = delete;
    // Use this class as non-copyable but movable
    TalkieSocket(TalkieSocket&&) = default;             // Move constructor OK
    TalkieSocket& operator=(TalkieSocket&&) = default;  // Move assignment OK


    bool initialize();
    bool sendToDevice(const std::string& ip, int port, const std::string& message);
    bool sendBroadcast(int port, const std::string& message);
    void closeSocket();
};



class TalkieDevice {
                
    private:
        TalkieSocket * const talkie_socket = nullptr;
        const bool verbose;
        // Socket variables
        std::string target_ip;  // Default constructor makes it empty
        int target_port;
    
        
    public:
        TalkieDevice(TalkieSocket * const socket, int port = 5005, bool verbose = false)
                    : talkie_socket(socket), target_port(port), verbose(verbose) { }

        // Explicitly delete copy assignment
        TalkieDevice& operator=(const TalkieDevice&) = delete;
        // Use this class as non-copyable but movable
        TalkieDevice(TalkieDevice&&) = default;             // Move constructor OK
        TalkieDevice& operator=(TalkieDevice&&) = default;  // Move assignment OK

        void setTargetIP(const std::string& ip);
        std::string getTargetIp() const { return target_ip; }
        int getTargetPort() const { return target_port; }
        bool sendMessage(const std::string& talkie_message);
        bool sendTempo(const nlohmann::json& json_talkie_message, const int bpm_n, const int bpm_d);
        
};



class TalkiePin {
private:
    const double time_ms;
    TalkieDevice * const talkie_device = nullptr;
    std::string talkie_message;
    // Auxiliary variable for the final playing loop!!
    double delay_time_ms = -1;

public:
    // Pin DEFAULT constructor, no arguments,
    // needed for emplace and insert of the std::unordered_map inside TalkieDevice class !!
    TalkiePin()
        : time_ms(0),                       // Default to 0
        talkie_device(nullptr),             // Default to nullptr
        talkie_message(),                   // Default to an empty vector
        delay_time_ms(-1)
    { }

    // Pin constructor
    TalkiePin(double time_milliseconds, TalkieDevice* talkie_device,
        const std::string& json_talkie_message)
            : time_ms(time_milliseconds),
            talkie_device(talkie_device),
            talkie_message(json_talkie_message)
        { }

    // Pin copy constructor
    TalkiePin(const TalkiePin& other)
        : time_ms(other.time_ms),                   // Copy the time_ms
          talkie_device(other.talkie_device),       // Copy the pointer to the TalkieDevice
          talkie_message(other.talkie_message),     // Copy the talkie_message vector
          delay_time_ms(other.delay_time_ms)
    { }

    double getTime() const {
        return time_ms;
    }

    TalkieDevice *getTalkieDevice() const {
        return talkie_device;
    }

    void pluckTooth();

    void setDelayTime(double delay_time_ms) {
        this->delay_time_ms = delay_time_ms;
    }

    double getDelayTime() const {
        return this->delay_time_ms;
    }

    std::string getMessage() const {
        return this->talkie_message; // Returns a copy
    }

    void setStatusByte(unsigned char status_byte) {
        this->talkie_message[0] = status_byte;
    }

    unsigned char getStatusByte() const {
        return this->talkie_message[0];
    }

    void setDataByte(int nth_byte, unsigned char data_byte) {
        this->talkie_message[nth_byte] = data_byte;
    }

    unsigned char getDataByte(int nth_byte = 1) const {
        return this->talkie_message[nth_byte];
    }

    unsigned char getChannel() const {
        return this->talkie_message[0] & 0x0F;
    }

    unsigned char getAction() const {
        return this->talkie_message[0] & 0xF0;
    }

    TalkieDevice * const getDevice() const {
        return this->talkie_device;
    }

};



    


// Declare the function in the header file
void disableBackgroundThrottling();

void setRealTimeScheduling();
void highResolutionSleep(long long microseconds);
int PlayList(const char* json_str, const int delay_ms, bool verbose = false);


#endif // JSON_TALKIE_PLAYER_HPP
