/*
 * File change watcher class
 *
 * Copyright (C) 2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <filesystem>
#include <iostream>
#include <functional>

 // @todo: add link back to e.g. pipeline

class FileWatcher {
private:
    std::thread thread;
    std::unordered_map<std::string, std::filesystem::file_time_type> files{};
	std::chrono::duration<int, std::milli> interval{ 1000 };
	bool active = true;

    void watch() {
        while (active) {
            std::this_thread::sleep_for(interval);

            // Check if files have been modified
            for (auto& file : files) {
                auto current_file_last_write_time = std::filesystem::last_write_time(file.first);
                if (file.second != current_file_last_write_time) {
                    files[file.first] = current_file_last_write_time;
                    onFileChanged(file.first);
                }
            }
        }
    }

public:
    std::function<void(const std::string)> onFileChanged;

	void addFile(const std::string filename) {
        files[filename] = std::filesystem::last_write_time(filename);
	}

	void start() {
        active = true;
        thread = std::thread(&FileWatcher::watch, this);
	}

    void stop() {
        active = false;
        thread.join();
    }

};