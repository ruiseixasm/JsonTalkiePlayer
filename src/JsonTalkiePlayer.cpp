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
#include "JsonTalkiePlayer.hpp"

// MidiPin methods definition
void MidiPin::pluckTooth() {
    if (midi_device != nullptr)
        midi_device->sendMessage(&midi_message);
}


// MidiDevice methods definition
bool MidiDevice::openPort() {
    if (!opened_port && !unavailable_device) {
        try {
            midiOut.openPort(port);
            opened_port = true;
            if (verbose) std::cout << "   " << name;
        } catch (RtMidiError &error) {
            unavailable_device = true;
            error.printMessage();
        }
    }
    return opened_port;
}

void MidiDevice::closePort() {
    if (opened_port) {
        midiOut.closePort();
        opened_port = false;
        if (verbose) std::cout << "   " << name;
    }
}

bool MidiDevice::hasPortOpen() const {
    return opened_port;
}

const std::string& MidiDevice::getName() const {
    return name;
}

unsigned int MidiDevice::getDevicePort() const {
    return port;
}

void MidiDevice::sendMessage(const std::vector<unsigned char> *midi_message) {
    midiOut.sendMessage(midi_message);
}



// Function to set real-time scheduling
void setRealTimeScheduling() {
#ifdef _WIN32
    // Set the thread priority to highest for real-time scheduling on Windows
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#else
    // Set real-time scheduling on Linux
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
#endif
}


double get_time_ms(int minutes_numerator, int minutes_denominator) {

    double milliseconds = minutes_numerator * 60000.0 / minutes_denominator;
    // Round to three decimal places
    return std::round(milliseconds * 1000.0) / 1000.0;
}


int PlayList(const char* json_str, bool verbose) {
    
    disableBackgroundThrottling();

    // Set real-time scheduling
    setRealTimeScheduling();
    
    #ifdef DEBUGGING
    auto debugging_start = std::chrono::high_resolution_clock::now();
    auto debugging_now = debugging_start;
    auto debugging_last = debugging_now;
    long long completion_time_us = 0;
    #endif

    struct PlayReporting {
        size_t json_processing  = 0;    // milliseconds
        size_t total_generated  = 0;
        size_t total_validated  = 0;
        size_t total_incorrect  = 0;
        size_t total_redundant  = 0;
        double total_drag       = 0.0;
        double total_delay      = 0.0;
        double maximum_delay    = 0.0;
        double minimum_delay    = 0.0;
        double average_delay    = 0.0;
        double sd_delay         = 0.0;
    };
    PlayReporting play_reporting;

    
    if (verbose) std::cout << "JsonTalkiePlayer version: " << VERSION << std::endl;

    // Where the playing happens
    {
        // Under its own scope in order to disconnect all devices before the stats reporting !
        std::vector<MidiDevice> available_midi_devices;
        std::list<MidiPin> midiToProcess;
        std::list<MidiPin> midiProcessed;

        //
        // Where each Available Device is collected BUT NOT connected
        //

        try {
            RtMidiOut midiOut;  // Temporary MidiOut manipulator
            unsigned int nPorts = midiOut.getPortCount();
            if (nPorts == 0) {
                if (verbose) std::cout << "No output Midi devices available.\n";
                return 1;
            }
            if (verbose) std::cout << "Available output Midi devices:\n";
            for (unsigned int i = 0; i < nPorts; i++) {
                std::string portName = midiOut.getPortName(i);
                if (verbose) std::cout << "\tMidi device #" << i << ": " << portName << std::endl;
                available_midi_devices.push_back(MidiDevice(portName, i, verbose));   // The object is copied
            }
            if (available_midi_devices.size() == 0) {
                if (verbose) std::cout << "\tNo output Midi devices available.\n";
                return 1;
            }
        } catch (RtMidiError &error) {
            error.printMessage();
            return EXIT_FAILURE;
        }

        #ifdef DEBUGGING
        debugging_now = std::chrono::high_resolution_clock::now();
        auto completion_time = std::chrono::duration_cast<std::chrono::microseconds>(debugging_now - debugging_last);
        completion_time_us = completion_time.count();
        std::cout << "MIDI DEVICES FULLY PROCESSED IN: " << completion_time_us << " microseconds" << std::endl;
        debugging_last = std::chrono::high_resolution_clock::now();
        #endif

        //
        // Where the JSON content is processed and added up the Pluck midi messages
        //

        if (verbose) std::cout << "Devices connected:    ";

        auto data_processing_start = std::chrono::high_resolution_clock::now();

        try {

            nlohmann::json json_files_data = nlohmann::json::parse(json_str);

            for (nlohmann::json jsonData : json_files_data) {

                nlohmann::json jsonFileType;
                nlohmann::json jsonFileUrl;
                nlohmann::json jsonFileContent;

                try
                {
                    jsonFileType = jsonData["filetype"];
                    jsonFileUrl = jsonData["url"];
                    jsonFileContent = jsonData["content"];
                }
                catch (nlohmann::json::parse_error& ex)
                {
                    if (verbose) std::cerr << "Unable to extract json data: " << ex.byte << std::endl;
                    continue;
                }
                
                if (jsonFileType != FILE_TYPE || jsonFileUrl != FILE_URL) {
                    if (verbose) std::cerr << "Wrong type of file!" << std::endl;
                    continue;
                }

                // Dictionary where the key is a JSON list
                std::unordered_map<std::string, MidiDevice*> connected_devices_by_name;
                std::unordered_set<std::string> unavailable_devices;
                
                // Check if jsonFileContent is a non-empty array
                if (jsonFileContent.is_array() && !jsonFileContent.empty()) {

                    // Access the first element
                    const auto& firstElement = jsonFileContent.front();

                    // Check if the first element is an object and contains the key "clock"
                    if (firstElement.is_object() && firstElement.contains("clock")) {

                        try
                        {
                            // Access the value associated with the key "clock"
                            auto clockValue = firstElement.at("clock");
                            // The devices JSON list key
                            const unsigned int total_clock_pulses = clockValue["total_clock_pulses"];
                            const unsigned int pulse_duration_min_numerator = clockValue["pulse_duration_min_numerator"];
                            const unsigned int pulse_duration_min_denominator = clockValue["pulse_duration_min_denominator"];
							auto last_position_ms = get_time_ms(total_clock_pulses * pulse_duration_min_numerator, pulse_duration_min_denominator);
                            const nlohmann::json clocked_device_names = clockValue["clocked_devices"];
                            const nlohmann::json controlled_device_names = clockValue["controlled_devices"];

                            if (total_clock_pulses > 0 && pulse_duration_min_numerator > 0 && pulse_duration_min_denominator > 0) {

                                std::unordered_set<MidiDevice*> clocked_devices;

                                // First time any Device is tried to be connected, so, none is connected at this moment
                                // It's a list of Devices that is given as Device
                                for (std::string device_name : clocked_device_names) {

                                    for (auto &available_device : available_midi_devices) {
                                        if (available_device.getName().find(device_name) != std::string::npos) {
                                            //
                                            // Where the Device Port is connected/opened (Main reason for errors)
                                            //
                                            if (available_device.openPort()) {

                                                if (clocked_devices.find(&available_device) != clocked_devices.end())
                                                    continue;   // Already clocked!

                                                connected_devices_by_name[device_name] = &available_device;
                                                clocked_devices.insert(&available_device);
                                                    
                                                midiToProcess.push_back( MidiPin(0.0, &available_device, { system_clock_start }, 0x30) );
                                                play_reporting.total_generated++;

                                                for (unsigned int pulse_i = 1; pulse_i < total_clock_pulses; ++pulse_i) {

                                                    midiToProcess.push_back(MidiPin(
                                                        get_time_ms(pulse_i * pulse_duration_min_numerator, pulse_duration_min_denominator),
                                                        &available_device,
                                                        { system_timing_clock },
                                                        0x30
                                                    ));
                                                    play_reporting.total_generated++;
                                                }

                                                midiToProcess.push_back(MidiPin(last_position_ms, &available_device, { system_clock_stop }, 0xB0));
                                                play_reporting.total_generated++;

                                                midiToProcess.push_back(MidiPin(last_position_ms, &available_device, { system_song_pointer, 0, 0 }, 0xB0));
                                                play_reporting.total_generated++;

                                            } else {
                                                connected_devices_by_name[device_name] = nullptr;
                                            }
                                        } else {
                                            // Just adds it as a processed device
                                            unavailable_devices.insert(device_name);
                                        }
                                    }
                                }

                                std::unordered_set<MidiDevice*> controlled_devices;

                                // First time any Device is tried to be connected, so, none is connected at this moment
                                // It's a list of Devices that is given as Device
                                for (std::string device_name : controlled_device_names) {

                                    for (auto &available_device : available_midi_devices) {
                                        if (available_device.getName().find(device_name) != std::string::npos) {
                                            //
                                            // Where the Device Port is connected/opened (Main reason for errors)
                                            //
                                            if (available_device.openPort()) {

                                                if (controlled_devices.find(&available_device) != controlled_devices.end())
                                                    continue;   // Already controlled!

                                                connected_devices_by_name[device_name] = &available_device;
                                                controlled_devices.insert(&available_device);
                                                
                                                // Action			MMC	SysEx
                                                // Stop				F0 7F 7F 06 01 F7
                                                // Play				F0 7F 7F 06 02 F7
                                                // Deferred Play	F0 7F 7F 06 03 F7
                                                // Fast Forward		F0 7F 7F 06 04 F7
                                                // Rewind			F0 7F 7F 06 05 F7
                                                // Record Strobe	F0 7F 7F 06 06 F7
                                                // Record Exit		F0 7F 7F 06 07 F7
                                                // Pause			F0 7F 7F 06 09 F7
                                                // Locate			F0 7F 7F 06 44 … F7

                                                // MMC - Play
                                                midiToProcess.push_back(MidiPin(
                                                    0.0,
                                                    &available_device,
                                                    { system_sysex_start, 0x7F, 0x7F, 0x06, 0x02, system_sysex_end },
                                                    0x00    // Highest priority 0
                                                ));
                                                play_reporting.total_generated++;

                                                // MMC - Stop
                                                midiToProcess.push_back(MidiPin(
                                                    last_position_ms,
                                                    &available_device,
                                                    { system_sysex_start, 0x7F, 0x7F, 0x06, 0x01, system_sysex_end },
                                                    0xF0    // Lowest priority 16
                                                ));
                                                play_reporting.total_generated++;
                                                
                                                // MMC - Rewind
                                                midiToProcess.push_back(MidiPin(
                                                    last_position_ms,
                                                    &available_device,
                                                    { system_sysex_start, 0x7F, 0x7F, 0x06, 0x05, system_sysex_end },
                                                    0xF0    // Lowest priority 16
                                                ));
                                                play_reporting.total_generated++;

                                            } else {
                                                connected_devices_by_name[device_name] = nullptr;
                                            }
                                        } else {
                                            // Just adds it as a processed device
                                            unavailable_devices.insert(device_name);
                                        }
                                    }
                                }
                            }
                        } catch (const std::exception& e) {
                            if (verbose) std::cerr << "Error: " << e.what() << std::endl;
                        }

                    } else {
                        if (verbose) std::cerr << "No Clock given!" << std::endl;
                    }
                } else {
                    if (verbose) std::cerr << "JSON file is empty." << std::endl;
                }

                // Keeps the last called device in the JsonTalkiePlayer file
                MidiDevice *last_called_midi_device = nullptr;

                for (auto jsonElement : jsonFileContent)
                {
                    // Most of the time it's a midi_message being processed, so it makes sense to be the first to check
                    if (jsonElement.contains("midi_message")) {

                        if (last_called_midi_device != nullptr) {

                            play_reporting.total_incorrect++;
                            double time_milliseconds = jsonElement["time_ms"];

                            // Create an API with the default API
                            try
                            {
                                if (time_milliseconds < 0) {

                                    continue;
                                    
                                } else {

                                    unsigned char status_byte = jsonElement["midi_message"]["status_byte"];
                                    std::vector<unsigned char> json_midi_message = { status_byte }; // Starts the json_midi_message to a new Status Byte
                                    unsigned char priority = 0xFF;  // Lowest priority 16 by default
                                    
                                    unsigned char message_action = status_byte & 0xF0;
                                    switch (message_action) {
                                        case action_system:
                                            switch (status_byte) {
                                                case system_timing_clock:
                                                case system_clock_start:
                                                case system_clock_stop:
                                                case system_clock_continue:
                                                    // Any clock message falls here
                                                    priority = 0x30 | status_byte & 0x0F;       // High priority 3
                                                    break;
                                                case system_song_pointer:
                                                {
                                                    // This is already a try catch situation
                                                    unsigned char data_byte_1 = jsonElement["midi_message"]["data_byte_1"];
                                                    unsigned char data_byte_2 = jsonElement["midi_message"]["data_byte_2"];
                                                    if (data_byte_1 & 128 | data_byte_2 & 128)  // Makes sure it's inside the processing window
                                                        continue;

                                                    json_midi_message.push_back(data_byte_1);
                                                    json_midi_message.push_back(data_byte_2);
                                                    priority = 0xB0 | status_byte & 0x0F;       // Low priority 12
                                                    break;
                                                }
                                                case system_sysex_start:
                                                {
                                                    // sysex_data_bytes = jsonElement["midi_message"]["data_bytes"].get<std::vector<unsigned char>>();
                                                    
                                                    nlohmann::json data_bytes = jsonElement["midi_message"]["data_bytes"];
                                                    for (unsigned char sysex_data_byte : data_bytes) {
                                                        // Makes sure it's SysEx valid data
                                                        if (sysex_data_byte != 0xF0 && sysex_data_byte != 0xF7) {
                                                            json_midi_message.push_back(sysex_data_byte);
                                                        } else {
                                                            continue;
                                                        }
                                                    }
                                                    if (json_midi_message.size() < 2)
                                                        continue;
                                                    
                                                    json_midi_message.push_back(0xF7);  // End SysEx Data Byte
                                                    priority = 0xF0 | status_byte & 0x0F;       // Lowest priority 16
                                                    break;
                                                }
                                                default:
                                                    // All other messages get a low priority
                                                    priority = 0xD0 | status_byte & 0x0F;       // Low priority 14
                                                    break;
                                            }
                                            break;
                                        case action_note_off:
                                        case action_note_on:
                                        case action_control_change:
                                        case action_pitch_bend:
                                        case action_key_pressure:
                                        {
                                            // This is already a try catch situation
                                            unsigned char data_byte_1 = jsonElement["midi_message"]["data_byte_1"];
                                            unsigned char data_byte_2 = jsonElement["midi_message"]["data_byte_2"];
                                            if (data_byte_1 & 128 | data_byte_2 & 128)
                                                continue;

                                            json_midi_message.push_back(data_byte_1);
                                            json_midi_message.push_back(data_byte_2);

                                            // Set the respective priorities
                                            switch (message_action) {

                                                case action_note_off:
                                                case action_note_on:
                                                    priority = 0x50 | status_byte & 0x0F;       // Normal priority 5 for On and Off
                                                    break;
                                                case action_control_change:
                                                    if (data_byte_1 == 1) {             // Modulation
                                                        priority = 0x60 | status_byte & 0x0F;       // Low priority 6
                                                    } else if (data_byte_1 == 0 || data_byte_1 == 32) {
                                                        // 0 -  Bank Select (MSB)
                                                        // 32 - Bank Select (LSB)
                                                        priority = 0x00 | status_byte & 0x0F;       // Top priority 0
                                                    } else if (data_byte_1 == 123) {
                                                        // 123 - All notes off (0x7B)
                                                        // shall come after Notes On and Off
                                                        priority = 0x90 | status_byte & 0x0F;       // Low priority 9
                                                    } else {
                                                        priority = 0x20 | status_byte & 0x0F;       // High priority 2
                                                    }
                                                    break;
                                                case action_pitch_bend:
                                                    priority = 0x70 | status_byte & 0x0F;       // Low priority 7
                                                    break;
                                                case action_key_pressure:
                                                    priority = 0x80 | status_byte & 0x0F;       // Low priority 8
                                                    break;
                                            }
                                            break;
                                        }
                                        case action_program_change:
                                        case action_channel_pressure:
                                        {
                                            unsigned char data_byte = jsonElement["midi_message"]["data_byte"];
                                            if (data_byte & 128)
                                                continue;
                                            
                                            json_midi_message.push_back(data_byte);
                                            // Set the respective priorities
                                            switch (message_action) {

                                                case action_program_change:
                                                    priority = 0x10 | status_byte & 0x0F;       // High priority 1
                                                    break;
                                                case action_channel_pressure:
                                                    priority = 0x80 | status_byte & 0x0F;       // Low priority 8
                                                    break;
                                            }
                                            break;
                                        }

                                        default:
                                            continue;
                                    }

                                    midiToProcess.push_back( MidiPin(time_milliseconds, last_called_midi_device, json_midi_message, priority) );
                                    play_reporting.total_incorrect--;    // Cancels out the initial ++ increase at the beginning of the loop
                                    play_reporting.total_validated++;
                                }
                            }
                            catch (const nlohmann::json::exception& e) {
                                if (verbose) std::cerr << "JSON error: " << e.what() << std::endl;
                                continue;
                            } catch (const std::exception& e) {
                                if (verbose) std::cerr << "Error: " << e.what() << std::endl;
                                continue;
                            } catch (...) {
                                if (verbose) std::cerr << "Unknown error occurred." << std::endl;
                                continue;
                            }
                        }

                    // Where the last device is set based on the json "device" input
                    } else if (jsonElement.contains("devices")) {

                        // The devices JSON list key
                        nlohmann::json json_device_names = jsonElement["devices"];

                        last_called_midi_device = nullptr; // No available device found at start
                        // It's a list of Devices that is given as Device
                        for (std::string device_name : json_device_names) {
                            
                            if (connected_devices_by_name.find(device_name) != connected_devices_by_name.end()) {
                                last_called_midi_device = connected_devices_by_name[device_name];
                                goto skip_to_1;
                            }
                    
                            if (unavailable_devices.find(device_name) != unavailable_devices.end()) {
                                continue;
                            }
                    
                            for (auto &available_device : available_midi_devices) {
                                if (available_device.getName().find(device_name) != std::string::npos) {
                                    //
                                    // Where the Device Port is connected/opened (Main reason for errors)
                                    //
                                    if (available_device.openPort()) {
                                        connected_devices_by_name[device_name] = &available_device; 
                                        last_called_midi_device = &available_device;
                                    } else {
                                        connected_devices_by_name[device_name] = nullptr; 
                                    }
                                } else {
                                    unavailable_devices.insert(device_name);
                                }
                            }
                        }
                    }
                skip_to_1: continue;
                }
            }
        } catch (const nlohmann::json::parse_error& e) {
            if (verbose) std::cerr << "JSON parse error: " << e.what() << std::endl;
        }

        if (verbose) std::cout << std::endl;

        #ifdef DEBUGGING
        debugging_now = std::chrono::high_resolution_clock::now();
        completion_time = std::chrono::duration_cast<std::chrono::microseconds>(debugging_now - debugging_last);
        completion_time_us = completion_time.count();
        std::cout << "JSON DATA FULLY PROCESSED IN: " << completion_time_us << " microseconds" << std::endl;
        debugging_last = std::chrono::high_resolution_clock::now();
        #endif

        if (midiToProcess.size() == 0) {

            auto data_processing_finish = std::chrono::high_resolution_clock::now();

            auto pre_processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(data_processing_finish - data_processing_start);
            play_reporting.json_processing = pre_processing_time.count();

            // Where the reporting is finally done
            if (verbose) std::cout << "Data stats reporting:" << std::endl;
            if (verbose) std::cout << "\tMidi Messages processing time (ms):       " << std::setw(10) << play_reporting.json_processing << std::endl;
            if (verbose) std::cout << "\tTotal generated Midi Messages (included): " << std::setw(10) << play_reporting.total_generated << std::endl;
            if (verbose) std::cout << "\tTotal validated Midi Messages (accepted): " << std::setw(10) << play_reporting.total_validated << std::endl;
            if (verbose) std::cout << "\tTotal incorrect Midi Messages (excluded): " << std::setw(10) << play_reporting.total_incorrect << std::endl;
            if (verbose) std::cout << "\tTotal redundant Midi Messages (excluded): " << std::setw(10) << play_reporting.total_redundant << std::endl;
            if (verbose) std::cout << "\tTotal resultant Midi Messages (included): " << std::setw(10) << midiToProcess.size() << std::endl;

        } else {

            //
            // Where the existing Midi messages are sorted by time and other parameters
            //

            // Two levels sorting criteria
            midiToProcess.sort([]( const MidiPin &a, const MidiPin &b ) {
                
                // Time is the primary sorting criteria
                if (a.getTime() != b.getTime())  
                    return a.getTime() < b.getTime();           // Primary: Sort by time (ascending)
            
                // Then sort by Priority (Ascendent)
                // Must be "<" instead of "<=" due to the mysterious "strict weak ordering"
                // Explanation here: https://youtu.be/fi0CQ7laiXE?si=fysJC-UdG2lJytjU&t=1542
                return a.getPriority() < b.getPriority();      // Secondary: Sort by priority (ascending)
                
            });

            #ifdef DEBUGGING
            debugging_now = std::chrono::high_resolution_clock::now();
            completion_time = std::chrono::duration_cast<std::chrono::microseconds>(debugging_now - debugging_last);
            completion_time_us = completion_time.count();
            std::cout << "SORTING FULLY PROCESSED IN: " << completion_time_us << " microseconds" << std::endl;
            debugging_last = std::chrono::high_resolution_clock::now();
            #endif

            //
            // Where the redundant Midi messages lists are Cleaned up and processed
            //

            // Loop through the list and remove elements
            for (auto pin_it = midiToProcess.begin(); pin_it != midiToProcess.end(); ) {

                // Auxiliary variables
                MidiPin &pluck_pin = *pin_it;
                MidiDevice &pluck_device = *pluck_pin.getDevice();

                switch (pluck_pin.getAction()) {
                    case action_system:
                        switch (pluck_pin.getStatusByte()) {
                            case system_timing_clock:
                                if (pluck_device.last_pin_clock != nullptr) {
                                    if (pluck_device.last_pin_clock->getTime() == pluck_pin.getTime()) {
                                        if (pluck_device.last_pin_clock->getStatusByte() == system_clock_stop) {      // Clock Stop
                                            pluck_device.last_pin_clock->setStatusByte(system_timing_clock);
                                        }
                                        ++(play_reporting.total_redundant);
                                        pin_it = midiToProcess.erase(pin_it);
                                        goto skip_to_2;
                                    } else if (pluck_device.last_pin_clock->getStatusByte() == system_clock_stop) {   // Clock Stop
                                        pluck_pin.setStatusByte(system_clock_continue);
                                    }
                                } else {
                                    pluck_pin.setStatusByte(system_clock_start);
                                }
                                pluck_device.last_pin_clock = &pluck_pin;
                                ++pin_it; // Only increment if no removal
                            break;
                            case system_clock_start:
                                if (pluck_device.last_pin_clock != nullptr) {
                                    if (pluck_device.last_pin_clock->getTime() == pluck_pin.getTime()) {
                                        if (pluck_device.last_pin_clock->getStatusByte() == system_clock_stop) {      // Clock Stop
                                            pluck_device.last_pin_clock->setStatusByte(system_timing_clock);
                                        }
                                        ++(play_reporting.total_redundant);
                                        pin_it = midiToProcess.erase(pin_it);
                                        goto skip_to_2;
                                    } else if (pluck_device.last_pin_clock->getStatusByte() == system_clock_stop) {   // Clock Stop
                                        pluck_pin.setStatusByte(system_clock_continue);
                                    } else {
                                        pluck_pin.setStatusByte(system_timing_clock);
                                    }
                                }
                                pluck_device.last_pin_clock = &pluck_pin;
                                ++pin_it; // Only increment if no removal
                            break;
                            case system_clock_stop:
                                if (pluck_device.last_pin_clock != nullptr) {
                                    if (pluck_device.last_pin_clock->getTime() == pluck_pin.getTime()) {
                                        pluck_device.last_pin_clock->setStatusByte(system_clock_stop);
                                        ++(play_reporting.total_redundant);
                                        pin_it = midiToProcess.erase(pin_it);
                                        goto skip_to_2;
                                    } else if (pluck_device.last_pin_clock->getStatusByte() == system_clock_stop) {   // Clock Stop
                                        ++(play_reporting.total_redundant);
                                        pin_it = midiToProcess.erase(pin_it);
                                        goto skip_to_2;
                                    }
                                }
                                pluck_device.last_pin_clock = &pluck_pin;
                                ++pin_it; // Only increment if no removal
                            break;
                            case system_clock_continue:
                                if (pluck_device.last_pin_clock != nullptr) {
                                    if (pluck_device.last_pin_clock->getTime() == pluck_pin.getTime()) {
                                        pluck_device.last_pin_clock->setStatusByte(system_timing_clock);
                                        ++(play_reporting.total_redundant);
                                        pin_it = midiToProcess.erase(pin_it);
                                        goto skip_to_2;
                                    } else if (pluck_device.last_pin_clock->getStatusByte() == system_clock_start) {   // Clock Start
                                        pluck_pin.setStatusByte(system_timing_clock);
                                    } else if (pluck_device.last_pin_clock->getStatusByte() == system_clock_continue) {   // Clock Continue
                                        pluck_pin.setStatusByte(system_timing_clock);
                                    } else {                                                    // NOT Clock Start or Continue
                                        pluck_device.last_pin_clock->setStatusByte(system_clock_stop);
                                    }
                                } else {
                                    pluck_pin.setStatusByte(system_clock_start);
                                }
                                pluck_device.last_pin_clock = &pluck_pin;
                                ++pin_it; // Only increment if no removal
                            break;
                            case system_song_pointer:
                                if (pluck_device.last_pin_song_pointer != nullptr) {
                                    if (pluck_device.last_pin_song_pointer->getTime() == pluck_pin.getTime()
                                            && pluck_device.last_pin_song_pointer->getStatusByte() == system_song_pointer
                                            && pluck_device.last_pin_song_pointer->getDataByte(1) == pluck_pin.getDataByte(1)
                                            && pluck_device.last_pin_song_pointer->getDataByte(2) == pluck_pin.getDataByte(2)) {
                                        ++(play_reporting.total_redundant);
                                        pin_it = midiToProcess.erase(pin_it);
                                        goto skip_to_2;
                                    }
                                }
                                pluck_device.last_pin_song_pointer = &pluck_pin;
                                ++pin_it; // Only increment if no removal
                            break;
                            default:
                                ++pin_it; // Only increment if no removal
                            break;
                        }
                    break;
                    case action_note_off:
                    {
                        unsigned char channel_key = pluck_pin.getChannel();
                        auto& dict_last = pluck_device.last_pin_note_on;
                        auto note_on_it = dict_last.find(channel_key);

                        if (note_on_it != dict_last.end() && !note_on_it->second.empty()) { // Note On list found
                            auto& note_on_list = note_on_it->second;

                            // Loop through the list of a particular Channel
                            // and remove elements (because midi pins may be removed)
                            for (auto note_on = note_on_list.begin(); note_on != note_on_list.end(); ++note_on) {

                                MidiPin *last_pin_note_on = *note_on;

                                if (*last_pin_note_on == pluck_pin) {

                                    if (last_pin_note_on->level == 1) {

                                        note_on = note_on_list.erase(note_on);
                                        ++pin_it; // Only increment if no removal
                                    } else {
                                        --(*last_pin_note_on);  // Decrements level
                                        ++(play_reporting.total_redundant);
                                        pin_it = midiToProcess.erase(pin_it);
                                    }
                                    goto skip_to_2;
                                }
                            }
                        }
                        ++(play_reporting.total_redundant);  // Note Off as no Note On pair
                        pin_it = midiToProcess.erase(pin_it);
                    }
                    break;
                    case action_note_on:
                    {
                        unsigned char channel_key = pluck_pin.getChannel();
                        auto& dict_last = pluck_device.last_pin_note_on;
                        auto note_on_it = dict_last.find(channel_key);

                        if (note_on_it != dict_last.end() && !note_on_it->second.empty()) { // Note On list found
                            auto& note_on_list = note_on_it->second;

                            // Loops the list of a particular Channel
                            for (MidiPin *last_pin_note_on : note_on_list) {

                                if (*last_pin_note_on == pluck_pin) {

                                    ++(*last_pin_note_on);  // Increments level

                                    // New note off message
                                    std::vector<unsigned char> midi_message = {
                                        static_cast<unsigned char>(pluck_pin.getChannel() | action_note_off),
                                        pluck_pin.getDataByte(1),
                                        0
                                    };
                                    pin_it = midiToProcess.insert(pin_it,   // Makes a copy to the place given by pin_it
                                        MidiPin(
                                                pluck_pin.getTime(),
                                                pluck_pin.getMidiDevice(),
                                                midi_message
                                            )
                                        );
                                    // THIS IS RIGHT, NEW PIN ADDED, IT'S INTENDED TO BE TWO CONSECUTIVE SKIPS !!
                                    // Skips the previously inserted Note Off MidiPin
                                    ++pin_it;  // Move the iterator to the next element
                                    // Skips the default Note On MidiPin
                                    ++pin_it;  // Move the iterator to the next element
                                    goto skip_to_2;
                                }
                            }

                        }
                        // First timer Note On
                        // It's safe to use a direct reference given that the Note On midi_pin note parameters are never changed
                        dict_last[channel_key].push_back( &pluck_pin );
                        ++pin_it; // Only increment if no removal
                    }
                    break;
                    case action_control_change:
                    case action_key_pressure:
                    {
                        uint16_t dict_key = pluck_pin.getStatusByte() << 8 | pluck_pin.getDataByte(1);
                        auto& dict_last = pluck_device.last_pin_byte_16;

                        if (dict_last.find(dict_key) != dict_last.end()) {  // Key found
                            auto &last_pin_16 = dict_last[dict_key];
                            if (last_pin_16 != pluck_pin) {

                                last_pin_16.setDataByte(2, pluck_pin.getDataByte(2));
                                ++pin_it; // Only increment if no removal
                            } else {
                                ++(play_reporting.total_redundant);
                                pin_it = midiToProcess.erase(pin_it);
                            }
                        } else {
                            // Needs to use a pin dummy copy given that their midi parameters may be changed
                            dict_last.emplace(dict_key, MidiPin(pluck_pin));    // Just a dummy copy
                            ++pin_it; // Only increment if no removal
                        }
                    }
                    break;
                    case action_pitch_bend:
                    {
                        unsigned char dict_key = pluck_pin.getStatusByte();
                        auto& dict_last = pluck_device.last_pin_byte_8;

                        if (dict_last.find(dict_key) != dict_last.end()) {  // Key found
                            auto &last_pin_8 = dict_last[dict_key];
                            if (last_pin_8 != pluck_pin) {

                                last_pin_8.setDataByte(1, pluck_pin.getDataByte(1));
                                last_pin_8.setDataByte(2, pluck_pin.getDataByte(2));
                                ++pin_it; // Only increment if no removal
                            } else {
                                ++(play_reporting.total_redundant);
                                pin_it = midiToProcess.erase(pin_it);
                            }
                        } else {
                            // Needs to use a pin dummy copy given that their midi parameters may be changed
                            dict_last.emplace(dict_key, MidiPin(pluck_pin));    // Just a dummy copy
                            ++pin_it; // Only increment if no removal
                        }
                    }
                    break;
                    case action_channel_pressure:
                    {
                        unsigned char dict_key = pluck_pin.getStatusByte();
                        auto& dict_last = pluck_device.last_pin_byte_8;

                        if (dict_last.find(dict_key) != dict_last.end()) {  // Key found
                            auto &last_pin_8 = dict_last[dict_key];
                            if (last_pin_8 != pluck_pin) {

                                last_pin_8.setDataByte(1, pluck_pin.getDataByte(1));
                                ++pin_it; // Only increment if no removal
                            } else {
                                ++(play_reporting.total_redundant);
                                pin_it = midiToProcess.erase(pin_it);
                            }
                        } else {
                            // Needs to use a pin dummy copy given that their midi parameters may be changed
                            dict_last.emplace(dict_key, MidiPin(pluck_pin));    // Just a dummy copy
                            ++pin_it; // Only increment if no removal
                        }
                    }
                    break;

                    default:    // Includes Program Change 0xC0 (Never considered redundant!)
                        ++pin_it; // Only increment if no removal
                    break;
                }

            skip_to_2: continue;
            }

            // Get time_ms of last message
            auto last_message_time_ms = midiToProcess.back().getTime();
            
            
            for (auto &device : available_midi_devices) {
                
                if (device.hasPortOpen()) {
                    
                    // MIDI NOTES SHALL NOT BE LEFT PRESSED !!
                    // Add the needed note off for all those still on at the end!
                    // Iterate over all keys and values
                    for (const auto& pair : device.last_pin_note_on) {
                        unsigned char channel_key = pair.first;
                        auto& note_on_list = pair.second;

                        for (MidiPin *last_pin_note_on : note_on_list) {
                            // Transform midi on in midi off
                            std::vector<unsigned char> midi_message = {
                                static_cast<unsigned char>(last_pin_note_on->getChannel() | action_note_off),    // note_off_status_byte
                                last_pin_note_on->getDataByte(1),
                                last_pin_note_on->getDataByte(2)
                            };
                            // Adds a new MidiPin as a copy to the list of pins to be processed
                            midiToProcess.push_back( MidiPin(last_message_time_ms, &device, midi_message) );
                        }
                    }

                    // LAST MIDI CLOCK MESSAGE SHALL BE STOP
                    if (device.last_pin_clock != nullptr && device.last_pin_clock->getStatusByte() == system_timing_clock)
                        device.last_pin_clock->setStatusByte(system_clock_stop);    // Clock Stop
                }
            }

            #ifdef DEBUGGING
            debugging_now = std::chrono::high_resolution_clock::now();
            completion_time = std::chrono::duration_cast<std::chrono::microseconds>(debugging_now - debugging_last);
            completion_time_us = completion_time.count();
            std::cout << "MIDI MESSAGES CLEANING UP FULLY PROCESSED IN: " << completion_time_us << " microseconds" << std::endl;
            debugging_last = std::chrono::high_resolution_clock::now();
            #endif

            auto data_processing_finish = std::chrono::high_resolution_clock::now();
            auto data_processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(data_processing_finish - data_processing_start);

            play_reporting.json_processing = data_processing_time.count();

            // Where the reporting is finally done
            if (verbose) std::cout << "Data stats reporting:" << std::endl;
            if (verbose) std::cout << "\tMidi Messages processing time (ms):       " << std::setw(10) << play_reporting.json_processing << std::endl;
            if (verbose) std::cout << "\tTotal generated Midi Messages (included): " << std::setw(10) << play_reporting.total_generated << std::endl;
            if (verbose) std::cout << "\tTotal validated Midi Messages (accepted): " << std::setw(10) << play_reporting.total_validated << std::endl;
            if (verbose) std::cout << "\tTotal incorrect Midi Messages (excluded): " << std::setw(10) << play_reporting.total_incorrect << std::endl;
            if (verbose) std::cout << "\tTotal redundant Midi Messages (excluded): " << std::setw(10) << play_reporting.total_redundant << std::endl;
            if (verbose) std::cout << "\tTotal resultant Midi Messages (included): " << std::setw(10) << midiToProcess.size() << std::endl;

            MidiPin *last_pin = &midiToProcess.back();
            size_t duration_time_sec = std::round(last_pin->getTime() / 1000);
            if (verbose) std::cout << "The data will now be played during "
                << duration_time_sec / 60 << " minutes and " << duration_time_sec % 60 << " seconds..." << std::endl;

            //
            // Where the Midi messages are sent to each Device
            //

            auto playing_start = std::chrono::high_resolution_clock::now();

            while (midiToProcess.size() > 0) {
                
                MidiPin &midi_pin = midiToProcess.front();  // Pin MIDI message

                long long next_pin_time_us = std::round((midi_pin.getTime() + play_reporting.total_drag) * 1000);
                auto playing_now = std::chrono::high_resolution_clock::now();
                auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(playing_now - playing_start);
                long long elapsed_time_us = elapsed_time.count();
                long long sleep_time_us = next_pin_time_us > elapsed_time_us ? next_pin_time_us - elapsed_time_us : 0;

                highResolutionSleep(sleep_time_us);  // Sleep for x microseconds

                auto pluck_time = std::chrono::high_resolution_clock::now() - playing_start;
                midi_pin.pluckTooth();  // as soon as possible! <----- Midi Send

                auto pluck_time_us = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::microseconds>(pluck_time).count()
                );
                double delay_time_ms = (pluck_time_us - next_pin_time_us) / 1000;
                midi_pin.setDelayTime(delay_time_ms);
                midiProcessed.push_back(std::move(midiToProcess.front()));  // Move the object
                midiToProcess.pop_front();  // Remove the first element

                // Process drag if existent
                if (delay_time_ms > DRAG_DURATION_MS)
                    play_reporting.total_drag += delay_time_ms - DRAG_DURATION_MS;  // Drag isn't Delay
            }

            #ifdef DEBUGGING
            debugging_now = std::chrono::high_resolution_clock::now();
            completion_time = std::chrono::duration_cast<std::chrono::microseconds>(debugging_now - debugging_last);
            completion_time_us = completion_time.count();
            std::cout << "PLAYING FULLY PROCESSED IN: " << completion_time_us << " microseconds" << std::endl;
            debugging_last = std::chrono::high_resolution_clock::now();
            #endif

            //
            // Where the final Statistics are calculated
            //

            if (midiProcessed.size() > 0) {

                for (auto &midi_pin : midiProcessed) {
                    auto delay_time_ms = midi_pin.getDelayTime();
                    play_reporting.total_delay += delay_time_ms;
                    play_reporting.maximum_delay = std::max(play_reporting.maximum_delay, delay_time_ms);
                }

                play_reporting.minimum_delay = play_reporting.maximum_delay;
                play_reporting.average_delay = play_reporting.total_delay / midiProcessed.size();

                for (auto &midi_pin : midiProcessed) {
                    auto delay_time_ms = midi_pin.getDelayTime();
                    play_reporting.minimum_delay = std::min(play_reporting.minimum_delay, delay_time_ms);
                    play_reporting.sd_delay += std::pow(delay_time_ms - play_reporting.average_delay, 2);
                }

                play_reporting.sd_delay /= midiProcessed.size();
                play_reporting.sd_delay = std::sqrt(play_reporting.sd_delay);
            }
        }
        
    if (verbose) std::cout << "Devices disconnected: ";
    // Exiting devices scope automatically disconnects them
    }

    // Where the reporting is finally done
    if (verbose) std::cout << std::endl << "Midi stats reporting:" << std::endl;
    // Set fixed floating-point notation and precision
    if (verbose) std::cout << std::fixed << std::setprecision(3);
    if (verbose) std::cout << "\tTotal drag (ms):      " << std::setw(34) << play_reporting.total_drag << " \\" << std::endl;
    if (verbose) std::cout << "\tCumulative delay (ms):" << std::setw(34) << play_reporting.total_delay << " /" << std::endl;
    if (verbose) std::cout << "\tMaximum delay (ms): " << std::setw(36) << play_reporting.maximum_delay << " \\" << std::endl;
    if (verbose) std::cout << "\tMinimum delay (ms): " << std::setw(36) << play_reporting.minimum_delay << " /" << std::endl;
    if (verbose) std::cout << "\tAverage delay (ms): " << std::setw(36) << play_reporting.average_delay << " \\" << std::endl;
    if (verbose) std::cout << "\tStandard deviation of delays (ms):" << std::setw(36 - 14) << play_reporting.sd_delay << " /"  << std::endl;


    return 0;
}



void disableBackgroundThrottling() {
#ifdef _WIN32
    // Windows-specific code to disable background throttling
    PROCESS_POWER_THROTTLING_STATE PowerThrottling;
    PowerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    PowerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    PowerThrottling.StateMask = 0;

    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &PowerThrottling, sizeof(PowerThrottling));
#else
    // Linux: No equivalent functionality, so do nothing
#endif
}

// High-resolution sleep function
void highResolutionSleep(long long microseconds) {
#ifdef _WIN32
    // Windows: High-resolution sleep using QueryPerformanceCounter
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);

    long long sleepInterval = microseconds > 100*1000 ? microseconds - 100*1000 : 0;  // Sleep 1ms if the wait is longer than 100ms
    if (sleepInterval > 0) {
        // Sleep for most of the time to save CPU, then busy wait for the remaining time
        std::this_thread::sleep_for(std::chrono::microseconds(sleepInterval));
    }

    double elapsedMicroseconds = 0;
    do {
        QueryPerformanceCounter(&end);
        elapsedMicroseconds = static_cast<double>(end.QuadPart - start.QuadPart) * 1e6 / frequency.QuadPart;
    } while (elapsedMicroseconds < microseconds);
    
#else
    // Linux: High-resolution sleep using clock_nanosleep
    struct timespec ts;
    ts.tv_sec = microseconds / 1e6;
    ts.tv_nsec = (microseconds % static_cast<long long>(1e6)) * 1000;
    clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
#endif
}

/*
    Voice Message           Status Byte      Data Byte1          Data Byte2
    -------------           -----------   -----------------   -----------------
    Note off                      8x      Key number          Note Off velocity
    Note on                       9x      Key number          Note on velocity
    Polyphonic Key Pressure       Ax      Key number          Amount of pressure
    Control Change                Bx      Controller number   Controller value
    Program Change                Cx      Program number      None
    Channel Pressure              Dx      Pressure value      None            
    Pitch Bend                    Ex      MSB                 LSB
    Song Position Ptr             F2      0                   0

    System Real-Time Message         Status Byte 
    ------------------------         -----------
    Timing Clock                         F8
    Start Sequence                       FA
    Continue Sequence                    FB
    Stop Sequence                        FC
    Active Sensing                       FE
    System Reset                         FF


    SysEx Message                    Status Byte 
    ------------------------         -----------
    0xF0: SysEx Start
    <Data Bytes>: Manufacturer ID + Command + Data
    0xF7: SysEx End

    self_playlist.append(
        {
            "time_ms": self.get_time_ms(single_pulse_duration_ms * total_clock_pulses),
            "midi_message": {
                "status_byte": 0xF0,    # Start of SysEx
                "data_bytes": [0x7F, 0x7F, 0x06, 0x01],  # Universal Stop command
                "end_byte": 0xF7,       # End of SysEx
                "device": devices
            }
        }
    )


*/