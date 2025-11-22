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
	uint8_t data_bytes[1024] = {0};
	size_t data_bytes_i = 0;
	
	// ASCII byte values:
	// 	'c' = 99
	// 	':' = 58
	// 	'"' = 34
	// 	'0' = 48
	// 	'9' = 57

	// Has to be pre processed (linearly)
	bool at_c0 = false;
	for (size_t i = 0; i < len; ++i) {
		if (!at_c0 && i > 3 && bytes[i - 3] == 'c' && bytes[i - 1] == ':' && bytes[i - 4] == '"' && bytes[i - 2] == '"') {
			at_c0 = true;
			data_bytes[data_bytes_i++] = '0';
			continue;
		} else if (at_c0) {
			if (bytes[i] < '0' || bytes[i] > '9') {
				at_c0 = false;
			} else {
				continue;
			}
		}
		data_bytes[data_bytes_i] = bytes[i];
		data_bytes_i++;
	}
	len = data_bytes_i;
    // std::cout << "Final message: " << data_bytes << std::endl;

	uint16_t chunk = 0;

    for (size_t i = 0; i < len; i += 2) {
		// Combine two data_bytes into 16-bit value
		chunk = data_bytes[i] << 8;
		if (i + 1 < len) {
			chunk |= data_bytes[i + 1];
		}
		checksum ^= chunk;
    }
    
    return checksum & 0xFFFF;
}




bool TalkieSocket::initialize() {
    if (socket_initialized) {
        return true;
    }

#ifdef _WIN32
    // WINDOWS: Must initialize Winsock first!
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return false;
    }
#endif

    // Create socket with error checking
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    if (sockfd == INVALID_SOCKET) {
        std::cerr << "Failed to create socket. Error: " << WSAGetLastError() << std::endl;
        WSACleanup();  // Clean up Winsock
        return false;
    }
#else
    if (sockfd < 0) {
        std::cerr << "Failed to create socket. Error: " << strerror(errno) << std::endl;
        return false;
    }
#endif

    // Enable broadcast with error checking
    int broadcast = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof(broadcast)) < 0) {
        std::cerr << "Failed to enable broadcast: ";
#ifdef _WIN32
        std::cerr << WSAGetLastError() << std::endl;
        closesocket(sockfd);
        WSACleanup();
#else
        std::cerr << strerror(errno) << std::endl;
        close(sockfd);
#endif
        sockfd = -1;
        return false;
    }

    // Bind with error checking
    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(5005);  // Default port
    
    if (bind(sockfd, (sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        std::cerr << "Bind failed: ";
#ifdef _WIN32
        std::cerr << WSAGetLastError() << std::endl;
        closesocket(sockfd);
        WSACleanup();
#else
        std::cerr << strerror(errno) << std::endl;
        close(sockfd);
#endif
        sockfd = -1;
        return false;
    }

    socket_initialized = true;
    if (verbose) std::cout << "Socket initialized successfully" << std::endl;
    return true;
}


bool TalkieSocket::sendToDevice(const std::string& ip, int port, const std::string& message) {
    if (!socket_initialized) {
        return false;
    }

    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &target.sin_addr);
    
    sendto(sockfd, message.c_str(), message.length(), 0,
            (sockaddr*)&target, sizeof(target));
    
    // if (verbose) std::cout << "Message: " << message << " sent to " << ip << ":" << port << std::endl;

    return true;
}


bool TalkieSocket::sendBroadcast(int port, const std::string& message) {
    if (!socket_initialized) {
        return false;
    }

    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(port);
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;
    
    sendto(sockfd, message.c_str(), message.length(), 0,
            (sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
    
    // if (verbose) std::cout << "Message: " << message << " broadcasted to port " << port << std::endl;

    return true;
}


bool TalkieSocket::hasMessages() {
    if (!socket_initialized || sockfd == -1) {
        std::cout << "DEBUG: Socket not initialized or invalid" << std::endl;
        return false;
    }

    // sockaddr_in bound_addr{};
    // socklen_t len = sizeof(bound_addr);
    // getsockname(sockfd, (sockaddr*)&bound_addr, &len);
    // std::cout << "DEBUG: Socket actually bound to port: " << ntohs(bound_addr.sin_port) << std::endl;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

#ifdef _WIN32
    int result = select(0, &readfds, nullptr, nullptr, &timeout);
#else
    int result = select(sockfd + 1, &readfds, nullptr, nullptr, &timeout);
#endif

    // std::cout << "DEBUG: select() returned: " << result << std::endl;
    
    if (result > 0 && FD_ISSET(sockfd, &readfds)) {
        // std::cout << "DEBUG: Data available on socket!" << std::endl;
        return true;
    }
    
    // std::cout << "DEBUG: No data available" << std::endl;
    return false;
}


std::vector<std::pair<std::string, std::string>> TalkieSocket::receiveMessages() {

    received_messages.clear();
    
    if (!socket_initialized || sockfd == -1) {
        return received_messages;
    }

    char buffer[1024];
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    memset(buffer, 0, sizeof(buffer));
    
    int received = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                            (sockaddr*)&client_addr, &client_len);

    
    do {
        memset(buffer, 0, sizeof(buffer));
        
        int received = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                               (sockaddr*)&client_addr, &client_len);

        if (received > 0) {
            buffer[received] = '\0';
            
            // Get IP from socket (always available)
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            
            // Store IP and raw message separately
            received_messages.push_back({client_ip, buffer});
            
            // if (verbose) {
            //     std::cout << "Received from " << client_ip << " - " << buffer << std::endl;
            // }
        } else {
            break;
        }
    } while (hasMessages());


    return received_messages;
}


bool TalkieSocket::updateAddresses() {
    bool updated_addresses = false;
    try {
        if (socket_initialized && this->hasMessages()) {
            this->receiveMessages();
            for (const auto& full_message : received_messages) {
                std::string device_address = full_message.first;
                std::string json_string = full_message.second;
                
                std::cout << "1. Unchecked message: " << json_string << std::endl;
                
                // Stupid json parser changes content order !!!
                nlohmann::json json_message = nlohmann::json::parse(json_string);

                // IT'S FASTER THIS WAY
                std::string device_name = json_message["f"];
                // std::cout << "2. Checked message: " << json_string << " of " << device_name << std::endl;
                auto device_it = devices_by_name.find(device_name);
                if (device_it != devices_by_name.end()) {

                    auto talkie_device = &device_it->second;
                    // Checks if it has an ip already (avoids extra heavy string manipulation and searching)
                    if (talkie_device->getTargetIP().empty()) {

                        uint16_t checksum = json_message["c"];
                        std::cout << "   Expected checksum: " << checksum << std::endl;
                        
                        uint16_t calculated = calculate_checksum(json_string);
                        std::cout << "   Calculated checksum: " << calculated << std::endl;
                        
                        if (checksum == calculated) {
                                // std::cout << "3. Accepted message: " << json_string << std::endl;
                                talkie_device->setTargetIP(device_address);
                                std::cout << "New Address " << device_address << " for " << device_name << std::endl;
                                total_updates++;
                                updated_addresses = true;
                        } else {
                            std::cout << "CHECKSUM FAILED! Expected: " << checksum 
                                    << ", Got: " << calculated << std::endl;
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error while updating Addresses: " << e.what() << std::endl;
        return false;
    }
    return updated_addresses;
}



void TalkieSocket::closeSocket() {
    if (sockfd != -1) {
#ifdef _WIN32
        closesocket(sockfd);
        WSACleanup();  // Clean up Winsock when done
#else
        close(sockfd);
#endif
        sockfd = -1;
    }
}

TalkieSocket * const TalkieDevice::getSocket() {
    return talkie_socket;
}

bool TalkieDevice::sendMessage(const std::string& talkie_message) {
    if (talkie_message.empty()) {
        std::cerr << "Error: Empty message\n";
        return false;
    }

    // No IP defined
    if (target_ip.empty()) {
        // Use broadcast as default
        return talkie_socket->sendBroadcast(target_port, talkie_message);
    } else {
        // Use specific IP
        return talkie_socket->sendToDevice(target_ip, target_port, talkie_message);
    }

    return false;
}


bool TalkieDevice::sendTempo(const nlohmann::json& json_talkie_message, const int bpm_n, const int bpm_d) {

    try {

        nlohmann::json json_talkie_tempo = json_talkie_message; // Does a copy
        json_talkie_tempo["m"] = MessageCode::set;
        json_talkie_tempo["i"] = 0;

        json_talkie_tempo["n"] = "bpm_n";
        json_talkie_tempo["v"] = bpm_n;
        json_talkie_tempo["c"] = 0;
        json_talkie_tempo["c"] = calculate_checksum(encode(json_talkie_tempo));
        this->sendMessage(encode(json_talkie_tempo));
        json_talkie_tempo["n"] = "bpm_d";
        json_talkie_tempo["v"] = bpm_d;
        json_talkie_tempo["c"] = 0;
        json_talkie_tempo["c"] = calculate_checksum(encode(json_talkie_tempo));
        this->sendMessage(encode(json_talkie_tempo));

    } catch (const std::exception& e) {

        std::cerr << "Fatal error while sending Tempo: " << e.what() << std::endl;
        return false;
    }

    return true;
}





// TalkiePin methods definition
void TalkiePin::pluckTooth() {
    if (talkie_device != nullptr)
        talkie_device->sendMessage(talkie_message);
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



int PlayList(const char* json_str, const int delay_ms, bool verbose) {
    
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

    if (verbose) {
        std::cout << "JsonTalkiePlayer version: " << VERSION << std::endl;
        std::cout << "Delay set to: " << delay_ms << " ms" << std::endl;
    }
    
    TalkieSocket talkie_socket(verbose);
    
    // Where the playing happens
    if (talkie_socket.initialize()) {

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


                std::unordered_map<uint8_t, TalkieDevice> devices_by_channel;
                TalkieDevice *talkie_device = nullptr;

                int bpm_n = 0;
                int bpm_d = 0;

                for (auto jsonElement : jsonFileContent)
                {
                    // Talkie message is just message
                    if (jsonElement.contains("port") && jsonElement.contains("time_ms") && jsonElement.contains("message")) {

                        double time_milliseconds = jsonElement["time_ms"] + static_cast<double>(delay_ms);
                        int target_port = jsonElement["port"];
                        nlohmann::json json_talkie_message = jsonElement["message"];
                        json_talkie_message["i"] = message_id(time_milliseconds);
                        json_talkie_message["c"] = 0;
                        json_talkie_message["c"] = calculate_checksum(encode(json_talkie_message));
                        
                        play_reporting.total_incorrect++;

                        if (json_talkie_message["t"].is_string()) {
                            std::string name = json_talkie_message["t"].get<std::string>();

                            auto device_it = talkie_socket.devices_by_name.find(name);  // Use iterator, not device
                            if (device_it != talkie_socket.devices_by_name.end()) {
                                talkie_device = &device_it->second;  // Use iterator directly
                            } else {
                                auto device = talkie_socket.devices_by_name.emplace(name, TalkieDevice(&talkie_socket, target_port, verbose));
                                talkie_device = &device.first->second; // Get pointer to stored object
                                // New device found, needs to set its tempo right away
                                if (bpm_d != 0) {
                                    talkie_device->sendTempo(jsonElement["message"], bpm_n, bpm_d);
                                }
                            }
                        } else if (json_talkie_message["t"].is_number()) {
                            uint8_t channel = json_talkie_message["t"].get<uint8_t>();

                            auto device_it = devices_by_channel.find(channel);  // Use iterator, not device
                            if (device_it != devices_by_channel.end()) {
                                talkie_device = &device_it->second;  // Use iterator directly
                            } else {
                                auto device = devices_by_channel.emplace(channel, TalkieDevice(&talkie_socket, target_port, verbose));
                                talkie_device = &device.first->second; // Get pointer to stored object
                                // New channel found, needs to set its tempo right away
                                if (bpm_d != 0) {
                                    talkie_device->sendTempo(jsonElement["message"], bpm_n, bpm_d);
                                }
                            }
                        } else {
                            continue;
                        }

                        const std::string talkie_message = encode(json_talkie_message);
                        talkieToProcess.push_back( TalkiePin(time_milliseconds, talkie_device, talkie_message) );
                        play_reporting.total_incorrect--;    // Cancels out the initial ++ increase at the beginning of the loop
                        play_reporting.total_validated++;

                    } else if (bpm_d == 0 && jsonElement.contains("tempo")) {

                        nlohmann::json json_talkie_clock = jsonElement["tempo"];
                        bpm_n = json_talkie_clock["bpm_numerator"];
                        bpm_d = json_talkie_clock["bpm_denominator"];

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

                highResolutionSleep(sleep_time_us, &talkie_socket);  // Sleep for x microseconds

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
void highResolutionSleep(long long microseconds, TalkieSocket * const talkie_socket) {
#ifdef _WIN32
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);

    double elapsedMicroseconds = 0;
    do {
        QueryPerformanceCounter(&end);
        elapsedMicroseconds = static_cast<double>(end.QuadPart - start.QuadPart) * 1e6 / frequency.QuadPart;
        
        // Update received Addresses
        if (talkie_socket && talkie_socket->totalUpdates() < talkie_socket->devices_by_name.size()) {
            talkie_socket->updateAddresses();
        }


        // // Check and process messages
        // if (talkie_socket && talkie_socket->hasMessages()) {
        //     auto messages = talkie_socket->receiveMessages();
        //     for (const auto& msg : messages) {
        //         // Process the message here
        //         std::cout << "Received during sleep: " << msg.second << std::endl;
        //     }
        // }
        
        // Small sleep to prevent 100% CPU usage
        if (elapsedMicroseconds < microseconds - 1000) {  // If we have more than 1ms left
            std::this_thread::sleep_for(std::chrono::microseconds(100));  // Sleep 100us
        }
        
    } while (elapsedMicroseconds < microseconds);
    
#else
    // Linux: High-resolution timing using clock_gettime
    struct timespec start, current;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    long long elapsedNanoseconds = 0;
    long long targetNanoseconds = microseconds * 1000;  // Convert microseconds to nanoseconds
    
    do {
        clock_gettime(CLOCK_MONOTONIC, &current);
        elapsedNanoseconds = (current.tv_sec - start.tv_sec) * 1000000000LL + 
                           (current.tv_nsec - start.tv_nsec);
        
        // Update received Addresses
        if (talkie_socket) {
            talkie_socket->updateAddresses();
        }


        // // Check and process messages
        // if (talkie_socket && talkie_socket->hasMessages()) {
        //     auto messages = talkie_socket->receiveMessages();
        //     for (const auto& msg : messages) {
        //         // Process the message here
        //         std::cout << "Received during sleep: " << msg << std::endl;
        //     }
        // }
        
        // Small sleep to prevent 100% CPU usage
        if (elapsedNanoseconds < targetNanoseconds - 1000000) {  // If we have more than 1ms left
            std::this_thread::sleep_for(std::chrono::microseconds(100));  // Sleep 100us
        }
        
    } while (elapsedNanoseconds < targetNanoseconds);
#endif
}


