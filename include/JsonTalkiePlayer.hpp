/*
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
#include "RtMidi.h"             // Includes the necessary MIDI library


// #define DEBUGGING true
#define FILE_TYPE "Json Midi Player"
#define FILE_URL  "https://github.com/ruiseixasm/JsonTalkiePlayer"
#define VERSION   "1.0.0"
#define DRAG_DURATION_MS (1000.0/((120/60)*24))


// Taken from: https://users.cs.cf.ac.uk/Dave.Marshall/Multimedia/node158.html

const unsigned char action_note_off         = 0x80; // Note off
const unsigned char action_note_on          = 0x90; // Note on
const unsigned char action_key_pressure     = 0xA0; // Polyphonic Key Pressure
const unsigned char action_control_change   = 0xB0; // Control Change
const unsigned char action_program_change   = 0xC0; // Program Change
const unsigned char action_channel_pressure = 0xD0; // Channel Pressure
const unsigned char action_pitch_bend       = 0xE0; // Pitch Bend
const unsigned char action_system           = 0xF0; // Device related Messages, System

const unsigned char system_sysex_start      = 0xF0; // Sysex Start
const unsigned char system_time_mtc         = 0xF1; // MIDI Time Code Quarter Frame
const unsigned char system_song_pointer     = 0xF2; // Song Position Pointer
const unsigned char system_song_select      = 0xF3; // Song Select
const unsigned char system_tune_request     = 0xF6; // Tune Request
const unsigned char system_sysex_end        = 0xF7; // Sysex End
const unsigned char system_timing_clock     = 0xF8; // Timing Clock
const unsigned char system_clock_start      = 0xFA; // Start
const unsigned char system_clock_continue   = 0xFB; // Continue
const unsigned char system_clock_stop       = 0xFC; // Stop
const unsigned char system_active_sensing   = 0xFE; // Active Sensing
const unsigned char system_system_reset     = 0xFF; // System Reset



class MidiDevice;


class MidiPin {

private:
    const double time_ms;
    const unsigned char priority;
    MidiDevice * const midi_device = nullptr;
    std::vector<unsigned char> midi_message;  // Replaces midi_message[3]
    // Auxiliary variable for the final playing loop!!
    double delay_time_ms = -1;

public:
    // Pin DEFAULT constructor, no arguments,
    // needed for emplace and insert of the std::unordered_map inside MidiDevice class !!
    MidiPin()
        : time_ms(0),                   // Default to 0
        priority(0),                    // Default to 0
        midi_device(nullptr),           // Default to nullptr
        midi_message(),                 // Default to an empty vector
        delay_time_ms(-1),              // Default to -1
        level(1)                        // Default to 1
    { }

    // Pin constructor
    MidiPin(double time_milliseconds, MidiDevice* midi_device,
        const std::vector<unsigned char>& json_midi_message, const unsigned char priority = 0xFF)
            : time_ms(time_milliseconds),
            midi_device(midi_device),
            midi_message(json_midi_message),    // Directly initialize midi_message
            priority(priority)
        { }

    // Pin copy constructor
    MidiPin(const MidiPin& other)
        : time_ms(other.time_ms),                     // Copy the time_ms
          midi_device(other.midi_device),             // Copy the pointer to the MidiDevice
          midi_message(other.midi_message),           // Copy the midi_message vector
          priority(other.priority),                   // Copy the priority
          delay_time_ms(other.delay_time_ms),         // Copy the delay_time_ms
          level(other.level)                          // Copy the level
    { }

    double getTime() const {
        return time_ms;
    }

    MidiDevice *getMidiDevice() const {
        return midi_device;
    }

    void pluckTooth();

    void setDelayTime(double delay_time_ms) {
        this->delay_time_ms = delay_time_ms;
    }

    double getDelayTime() const {
        return this->delay_time_ms;
    }

    std::vector<unsigned char> getMessage() const {
        return this->midi_message; // Returns a copy
    }

    void setStatusByte(unsigned char status_byte) {
        this->midi_message[0] = status_byte;
    }

    unsigned char getStatusByte() const {
        return this->midi_message[0];
    }

    void setDataByte(int nth_byte, unsigned char data_byte) {
        this->midi_message[nth_byte] = data_byte;
    }

    unsigned char getDataByte(int nth_byte = 1) const {
        return this->midi_message[nth_byte];
    }

    unsigned char getChannel() const {
        return this->midi_message[0] & 0x0F;
    }

    unsigned char getAction() const {
        return this->midi_message[0] & 0xF0;
    }

    unsigned char getPriority() const {
        return this->priority;
    }

    MidiDevice * const getDevice() const {
        return this->midi_device;
    }


public:

    // If this is a Note On pin, then, by definition, is already at level 1
    size_t level = 1;   // VERY IMPORTANT TO AVOID EARLIER NOTE OFF

    // Intended for Note On only
    bool operator == (const MidiPin &midi_pin) {
        // mapped by Channel, so, with the same Channel for sure
        return this->getDataByte(1) == midi_pin.getDataByte(1); // Key number
    }

    // Intended for Automation messages only
    bool operator != (const MidiPin &midi_pin) {
        // mapped by status byte, so, with the same action type for sure
        switch (this->getAction()) {
            case action_control_change:
            case action_key_pressure:
                return this->getDataByte(2) != midi_pin.getDataByte(2);
            case action_pitch_bend:
                return this->getDataByte(1) != midi_pin.getDataByte(1) ||
                        this->getDataByte(2) != midi_pin.getDataByte(2);
            case action_channel_pressure:
                return this->getDataByte(1) != midi_pin.getDataByte(1);
        }
        return true;
    }

    // Prefix increment
    MidiPin& operator++() {
        ++level;
        return *this;
    }
    // Postfix increment
    MidiPin operator++(int) {
        MidiPin temp = *this;
        ++level;
        return temp;
    }
    // Prefix decrement
    MidiPin& operator--() {
        --level;
        return *this;
    }
    // Postfix decrement
    MidiPin operator--(int) {
        MidiPin temp = *this;
        --level;
        return temp;
    }

};


class MidiDevice {
    private:
        RtMidiOut midiOut;
        const std::string name;
        const unsigned int port;
        const bool verbose;
        bool opened_port = false;
        bool unavailable_device = false;
    
    public:
    
        // Keeps MidiPin pointers
        std::unordered_map<unsigned char, std::list<MidiPin*>>
                                                    last_pin_note_on;   // For Note On tracking
        
        // Keeps MidiPin dummy copies
        std::unordered_map<unsigned char, MidiPin>  last_pin_byte_8;    // For Pitch Bend and Aftertouch alike
        std::unordered_map<uint16_t, MidiPin>       last_pin_byte_16;   // For Control Change and Key Pressure

        // Keeps MidiPin pointers
        MidiPin *last_pin_clock = nullptr;          // Midi clock messages 0xF0
        MidiPin *last_pin_song_pointer = nullptr;   // Midi clock messages 0xF2
    
    
    public:
        MidiDevice(std::string device_name, unsigned int device_port, bool verbose = false)
                    : name(device_name), port(device_port), verbose(verbose) { }
        ~MidiDevice() { closePort(); }
    
        // Move constructor
        MidiDevice(MidiDevice &&other) noexcept : midiOut(std::move(other.midiOut)),
                name(std::move(other.name)), port(other.port), verbose(other.verbose),
                opened_port(other.opened_port) { }
    
        // Delete the copy constructor and copy assignment operator
        MidiDevice(const MidiDevice &) = delete;
        MidiDevice &operator=(const MidiDevice &) = delete;
    
        // Move assignment operator
        MidiDevice &operator=(MidiDevice &&other) noexcept {
            if (this != &other) {
                // Since name and port are const, they cannot be assigned.
                opened_port = other.opened_port;
                // midiOut can't be assigned using the = assignment operator because has none.
                // midiOut = std::move(other.midiOut);
            }
            std::cout << "Move assigned: " << name << std::endl;
            return *this;
        }
    
        bool openPort();
        void closePort();
        bool hasPortOpen() const;
        const std::string& getName() const;
        unsigned int getDevicePort() const;
        void sendMessage(const std::vector<unsigned char> *midi_message);
    };
    






    
class TalkieDevice;


class TalkiePin {

private:
    const double time_ms;
    TalkieDevice * const talkie_device = nullptr;
    std::vector<unsigned char> talkie_message;  // Replaces talkie_message[3]
    // Auxiliary variable for the final playing loop!!
    double delay_time_ms = -1;

public:
    // Pin DEFAULT constructor, no arguments,
    // needed for emplace and insert of the std::unordered_map inside TalkieDevice class !!
    TalkiePin()
        : time_ms(0),                   // Default to 0
        talkie_device(nullptr),           // Default to nullptr
        talkie_message(),                 // Default to an empty vector
        delay_time_ms(-1),              // Default to -1
        level(1)                        // Default to 1
    { }

    // Pin constructor
    TalkiePin(double time_milliseconds, TalkieDevice* talkie_device,
        const std::vector<unsigned char>& json_talkie_message)
            : time_ms(time_milliseconds),
            talkie_device(talkie_device),
            talkie_message(json_talkie_message)
        { }

    // Pin copy constructor
    TalkiePin(const TalkiePin& other)
        : time_ms(other.time_ms),                     // Copy the time_ms
          talkie_device(other.talkie_device),             // Copy the pointer to the TalkieDevice
          talkie_message(other.talkie_message),           // Copy the talkie_message vector
          delay_time_ms(other.delay_time_ms),         // Copy the delay_time_ms
          level(other.level)                          // Copy the level
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

    std::vector<unsigned char> getMessage() const {
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


public:

    // If this is a Note On pin, then, by definition, is already at level 1
    size_t level = 1;   // VERY IMPORTANT TO AVOID EARLIER NOTE OFF

    // Intended for Note On only
    bool operator == (const TalkiePin &talkie_pin) {
        // mapped by Channel, so, with the same Channel for sure
        return this->getDataByte(1) == talkie_pin.getDataByte(1); // Key number
    }

    // Intended for Automation messages only
    bool operator != (const TalkiePin &talkie_pin) {
        // mapped by status byte, so, with the same action type for sure
        switch (this->getAction()) {
            case action_control_change:
            case action_key_pressure:
                return this->getDataByte(2) != talkie_pin.getDataByte(2);
            case action_pitch_bend:
                return this->getDataByte(1) != talkie_pin.getDataByte(1) ||
                        this->getDataByte(2) != talkie_pin.getDataByte(2);
            case action_channel_pressure:
                return this->getDataByte(1) != talkie_pin.getDataByte(1);
        }
        return true;
    }

    // Prefix increment
    TalkiePin& operator++() {
        ++level;
        return *this;
    }
    // Postfix increment
    TalkiePin operator++(int) {
        TalkiePin temp = *this;
        ++level;
        return temp;
    }
    // Prefix decrement
    TalkiePin& operator--() {
        --level;
        return *this;
    }
    // Postfix decrement
    TalkiePin operator--(int) {
        TalkiePin temp = *this;
        --level;
        return temp;
    }

};


class TalkieDevice {
    private:
        RtMidiOut midiOut;
        const std::string name;
        const unsigned int port;
        const bool verbose;
        bool opened_port = false;
        bool unavailable_device = false;
        // Socket variables
        int sockfd;
        struct sockaddr_in server_addr;
    
    public:
    
        // Keeps TalkiePin pointers
        std::unordered_map<unsigned char, std::list<TalkiePin*>>
                                                    last_pin_note_on;   // For Note On tracking
        
        // Keeps TalkiePin dummy copies
        std::unordered_map<unsigned char, TalkiePin>  last_pin_byte_8;    // For Pitch Bend and Aftertouch alike
        std::unordered_map<uint16_t, TalkiePin>       last_pin_byte_16;   // For Control Change and Key Pressure

        // Keeps TalkiePin pointers
        TalkiePin *last_pin_clock = nullptr;          // Midi clock messages 0xF0
        TalkiePin *last_pin_song_pointer = nullptr;   // Midi clock messages 0xF2
    
    
    public:
        TalkieDevice(std::string device_name, unsigned int device_port, bool verbose = false)
                    : name(device_name), port(device_port), verbose(verbose) { }
        ~TalkieDevice() { closePort(); }
    
        // Move constructor
        TalkieDevice(TalkieDevice &&other) noexcept : midiOut(std::move(other.midiOut)),
                name(std::move(other.name)), port(other.port), verbose(other.verbose),
                opened_port(other.opened_port) { }
    
        // Delete the copy constructor and copy assignment operator
        TalkieDevice(const TalkieDevice &) = delete;
        TalkieDevice &operator=(const TalkieDevice &) = delete;
    
        // Move assignment operator
        TalkieDevice &operator=(TalkieDevice &&other) noexcept {
            if (this != &other) {
                // Since name and port are const, they cannot be assigned.
                opened_port = other.opened_port;
                // midiOut can't be assigned using the = assignment operator because has none.
                // midiOut = std::move(other.midiOut);
            }
            std::cout << "Move assigned: " << name << std::endl;
            return *this;
        }
    
        bool openPort();
        void closePort();
        bool hasPortOpen() const;
        const std::string& getName() const;
        unsigned int getDevicePort() const;
        void sendMessage(const std::vector<unsigned char> *talkie_message);
    };
    

    



    

// Declare the function in the header file
void disableBackgroundThrottling();

void setRealTimeScheduling();
void highResolutionSleep(long long microseconds);
int PlayList(const char* json_str, bool verbose = false);


#endif // JSON_TALKIE_PLAYER_HPP
