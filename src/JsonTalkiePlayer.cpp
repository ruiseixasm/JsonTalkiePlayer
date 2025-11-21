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
#include "JsonTalkiePlayer.hpp"


// TalkiePin methods definition
void TalkiePin::pluckTooth() {
    if (talkie_device != nullptr)
        talkie_device->sendMessage(talkie_message);
}


// TalkieDevice methods definition
bool TalkieDevice::initializeSocket() {
    if (socket_initialized)
        return true;

#ifdef _WIN32
    // Windows-specific initialization
    WSADATA wsaData;
    int wsa = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (wsa != 0) {
        std::cerr << "WSAStartup failed: " << wsa << "\n";
        return false;
    }
#endif

    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (sockfd == INVALID_SOCKET) {
        std::cerr << "Failed to create socket. Error: " << WSAGetLastError() << "\n";
#else
    if (sockfd < 0) {
        std::cerr << "Failed to create socket. Error: " << strerror(errno) << "\n";
#endif
        return false;
    }

    // Enable broadcast support
    int bc = 1;  // Use int instead of BOOL for cross-platform
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (char*)&bc, sizeof(bc)) < 0) {
        std::cerr << "Failed to enable broadcast: ";
#ifdef _WIN32
        std::cerr << WSAGetLastError() << "\n";
        closesocket(sockfd);
#else
        std::cerr << strerror(errno) << "\n";
        close(sockfd);
#endif
        return false;
    }

    // Bind to any local port so broadcast is allowed
    sockaddr_in localAddr {};
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(0);
    localAddr.sin_addr.s_addr = INADDR_ANY;

#ifdef _WIN32
    if (bind(sockfd, (sockaddr*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << "\n";
        closesocket(sockfd);
#else
    if (bind(sockfd, (sockaddr*)&localAddr, sizeof(localAddr)) < 0) {
        std::cerr << "Bind failed: " << strerror(errno) << "\n";
        close(sockfd);
#endif
        return false;
    }

    // Set remote broadcast address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(target_port);
    server_addr.sin_addr.s_addr = INADDR_BROADCAST;  // 255.255.255.255

    socket_initialized = true;
    if (verbose) std::cout << "Broadcast socket initialized\n";

    return true;
}


void TalkieDevice::setTargetIP(const std::string& ip) {
    target_ip = ip;
    
    // If socket exists, we need to update server_addr
    if (socket_initialized) {
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(target_port);
        inet_pton(AF_INET, target_ip.c_str(), &server_addr.sin_addr);
        
        if (verbose) std::cout << "Target updated to " << target_ip << ":" << target_port << "\n";
    }
}


void TalkieDevice::closeSocket() {
    if (socket_initialized) {
        // Close Socket
        close(sockfd);
        sockfd = -1;
        socket_initialized = false;
    }
}


bool TalkieDevice::sendMessage(const std::string& talkie_message) {
    if (talkie_message.empty()) {
        std::cerr << "Error: Empty message\n";
        return false;
    }

    if (!socket_initialized || sockfd < 0) {
        std::cerr << "Error: Socket not initialized\n";
        return false;
    }

    size_t sent = sendto(sockfd, talkie_message.c_str(), talkie_message.size(), 0,
                         (sockaddr*)&server_addr, sizeof(server_addr));

#ifdef _WIN32
    bool error = (sent == SOCKET_ERROR);
#else
    bool error = (sent < 0);
#endif

    if (error) {
        std::cerr << "Failed to send message: ";
#ifdef _WIN32
        std::cerr << "WSA error " << WSAGetLastError();
#else
        std::cerr << strerror(errno);
#endif
        std::cerr << "\n";
        return false;
    }

    return true;
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


static uint32_t message_id(const double time_milliseconds) {
    return static_cast<uint32_t>(time_milliseconds);
}


static std::string encode(const nlohmann::json& message) {
    return message.dump(); // Convert JSON to string
}


static uint16_t calculate_checksum(const std::string& data) {
    uint16_t checksum = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.c_str());
    size_t len = data.length();
    
    for (size_t i = 0; i < len; i += 2) {
        // Combine two bytes into 16-bit value
        uint16_t chunk = bytes[i] << 8;
        if (i + 1 < len) {
            chunk |= bytes[i + 1];
        }
        checksum ^= chunk;
    }
    
    return checksum & 0xFFFF;
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
        size_t total_validated  = 0;
        size_t total_incorrect  = 0;
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
        std::unordered_map<std::string, TalkieDevice> devices_by_name;
        std::unordered_map<uint8_t, TalkieDevice> devices_by_channel;
                
        std::list<TalkiePin> talkieToProcess;
        std::list<TalkiePin> talkieProcessed;


        #ifdef DEBUGGING
        debugging_now = std::chrono::high_resolution_clock::now();
        auto completion_time = std::chrono::duration_cast<std::chrono::microseconds>(debugging_now - debugging_last);
        completion_time_us = completion_time.count();
        std::cout << "TALKIE DEVICES FULLY PROCESSED IN: " << completion_time_us << " microseconds" << std::endl;
        debugging_last = std::chrono::high_resolution_clock::now();
        #endif

        //
        // Where the JSON content is processed and added up the Pluck Talkie messages
        //

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

                // Check if jsonFileContent is a non-empty array
                if (!jsonFileContent.is_array() || jsonFileContent.empty()) {
                    if (verbose) std::cerr << "JSON file is empty." << std::endl;
                }


                TalkieDevice *talkie_device = nullptr;

                for (auto jsonElement : jsonFileContent)
                {

                    // Talkie message is just message
                    if (jsonElement.contains("port") && jsonElement.contains("time_ms") && jsonElement.contains("message")) {

                        double time_milliseconds = jsonElement["time_ms"];
                        int target_port = jsonElement["port"];
                        nlohmann::json json_talkie_message = jsonElement["message"];
                        json_talkie_message["i"] = message_id(time_milliseconds);
                        json_talkie_message["c"] = 0;
                        json_talkie_message["c"] = calculate_checksum(encode(json_talkie_message));
                        

                        play_reporting.total_incorrect++;

                        if (json_talkie_message["t"].is_string()) {
                            std::string name = json_talkie_message["t"].get<std::string>();

                            auto device_it = devices_by_name.find(name);  // Use iterator, not device
                            if (device_it != devices_by_name.end()) {
                                talkie_device = &device_it->second;  // Use iterator directly
                            } else {
                                auto device = devices_by_name.emplace(name, TalkieDevice(target_port, verbose));
                                talkie_device = &device.first->second; // Get pointer to stored object
                            }
                        } else if (json_talkie_message["t"].is_number()) {
                            uint8_t channel = json_talkie_message["t"].get<uint8_t>();

                            auto device_it = devices_by_channel.find(channel);  // Use iterator, not device
                            if (device_it != devices_by_channel.end()) {
                                talkie_device = &device_it->second;  // Use iterator directly
                            } else {
                                auto device = devices_by_channel.emplace(channel, TalkieDevice(target_port, verbose));
                                talkie_device = &device.first->second; // Get pointer to stored object
                            }
                        } else {
                            continue;
                        }

                        if (talkie_device->initializeSocket()) {
                            const std::string talkie_message = encode(json_talkie_message);
                            talkieToProcess.push_back( TalkiePin(time_milliseconds, talkie_device, talkie_message) );
                            play_reporting.total_incorrect--;    // Cancels out the initial ++ increase at the beginning of the loop
                            play_reporting.total_validated++;
                        }

                    }
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




        if (talkieToProcess.size() == 0) {

            auto data_processing_finish = std::chrono::high_resolution_clock::now();

            auto pre_processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(data_processing_finish - data_processing_start);
            play_reporting.json_processing = pre_processing_time.count();

            // Where the reporting is finally done
            if (verbose) std::cout << "Data stats reporting:" << std::endl;
            if (verbose) std::cout << "\tTalkie Messages processing time (ms):       " << std::setw(10) << play_reporting.json_processing << std::endl;
            if (verbose) std::cout << "\tTotal validated Talkie Messages (accepted): " << std::setw(10) << play_reporting.total_validated << std::endl;
            if (verbose) std::cout << "\tTotal incorrect Talkie Messages (excluded): " << std::setw(10) << play_reporting.total_incorrect << std::endl;
            if (verbose) std::cout << "\tTotal resultant Talkie Messages (included): " << std::setw(10) << talkieToProcess.size() << std::endl;

            
        } else {

            //
            // Where the existing Talkie messages are sorted by time and other parameters
            //

            // Two levels sorting criteria
            talkieToProcess.sort([]( const TalkiePin &a, const TalkiePin &b ) {
            
                 return a.getTime() < b.getTime();
            });

            #ifdef DEBUGGING
            debugging_now = std::chrono::high_resolution_clock::now();
            completion_time = std::chrono::duration_cast<std::chrono::microseconds>(debugging_now - debugging_last);
            completion_time_us = completion_time.count();
            std::cout << "SORTING FULLY PROCESSED IN: " << completion_time_us << " microseconds" << std::endl;
            debugging_last = std::chrono::high_resolution_clock::now();
            #endif

            #ifdef DEBUGGING
            debugging_now = std::chrono::high_resolution_clock::now();
            completion_time = std::chrono::duration_cast<std::chrono::microseconds>(debugging_now - debugging_last);
            completion_time_us = completion_time.count();
            std::cout << "TALKIE MESSAGES CLEANING UP FULLY PROCESSED IN: " << completion_time_us << " microseconds" << std::endl;
            debugging_last = std::chrono::high_resolution_clock::now();
            #endif

            auto data_processing_finish = std::chrono::high_resolution_clock::now();
            auto data_processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(data_processing_finish - data_processing_start);

            play_reporting.json_processing = data_processing_time.count();

            // Where the reporting is finally done
            if (verbose) std::cout << "Data stats reporting:" << std::endl;
            if (verbose) std::cout << "\tTalkie Messages processing time (ms):       " << std::setw(10) << play_reporting.json_processing << std::endl;
            if (verbose) std::cout << "\tTotal validated Talkie Messages (accepted): " << std::setw(10) << play_reporting.total_validated << std::endl;
            if (verbose) std::cout << "\tTotal incorrect Talkie Messages (excluded): " << std::setw(10) << play_reporting.total_incorrect << std::endl;
            if (verbose) std::cout << "\tTotal resultant Talkie Messages (included): " << std::setw(10) << talkieToProcess.size() << std::endl;

            TalkiePin *last_pin = &talkieToProcess.back();
            size_t duration_time_sec = std::round(last_pin->getTime() / 1000);
            if (verbose) std::cout << "The data will now be played during "
                << duration_time_sec / 60 << " minutes and " << duration_time_sec % 60 << " seconds..." << std::endl;


            //
            // Where the Talkie messages are sent to each Device
            //

            auto playing_start = std::chrono::high_resolution_clock::now();

            while (talkieToProcess.size() > 0) {
                
                TalkiePin &talkie_pin = talkieToProcess.front();  // Pin TALKIE message

                long long next_pin_time_us = std::round((talkie_pin.getTime() + play_reporting.total_drag) * 1000);
                auto playing_now = std::chrono::high_resolution_clock::now();
                auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(playing_now - playing_start);
                long long elapsed_time_us = elapsed_time.count();
                long long sleep_time_us = next_pin_time_us > elapsed_time_us ? next_pin_time_us - elapsed_time_us : 0;

                highResolutionSleep(sleep_time_us);  // Sleep for x microseconds

                auto pluck_time = std::chrono::high_resolution_clock::now() - playing_start;
                talkie_pin.pluckTooth();  // as soon as possible! <----- Talkie Send

                auto pluck_time_us = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::microseconds>(pluck_time).count()
                );
                double delay_time_ms = (pluck_time_us - next_pin_time_us) / 1000;
                talkie_pin.setDelayTime(delay_time_ms);
                talkieProcessed.push_back(std::move(talkieToProcess.front()));  // Move the object
                talkieToProcess.pop_front();  // Remove the first element

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

            if (talkieProcessed.size() > 0) {

                for (auto &talkie_pin : talkieProcessed) {
                    auto delay_time_ms = talkie_pin.getDelayTime();
                    play_reporting.total_delay += delay_time_ms;
                    play_reporting.maximum_delay = std::max(play_reporting.maximum_delay, delay_time_ms);
                }

                play_reporting.minimum_delay = play_reporting.maximum_delay;
                play_reporting.average_delay = play_reporting.total_delay / talkieProcessed.size();

                for (auto &talkie_pin : talkieProcessed) {
                    auto delay_time_ms = talkie_pin.getDelayTime();
                    play_reporting.minimum_delay = std::min(play_reporting.minimum_delay, delay_time_ms);
                    play_reporting.sd_delay += std::pow(delay_time_ms - play_reporting.average_delay, 2);
                }

                play_reporting.sd_delay /= talkieProcessed.size();
                play_reporting.sd_delay = std::sqrt(play_reporting.sd_delay);
            }

        }

    }

    // Where the reporting is finally done
    if (verbose) std::cout << std::endl << "Talkie stats reporting:" << std::endl;
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


