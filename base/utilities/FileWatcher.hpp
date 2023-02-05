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
#include "Pipeline.hpp"

struct FileWatchInfo {
    std::filesystem::file_time_type filetime;
    std::vector<void*> owners{};
};

class FileWatcher {
private:
    std::thread thread;
    std::unordered_map<std::string, FileWatchInfo> files{};
	std::chrono::duration<int, std::milli> interval{ 1000 };
	bool active = true;

    void watch() {
        while (active) {
            std::this_thread::sleep_for(interval);

            // Check if files have been modified
            for (auto& file : files) {
                auto current_file_last_write_time = std::filesystem::last_write_time(file.first);
                if (file.second.filetime != current_file_last_write_time) {
                    files[file.first].filetime = current_file_last_write_time;
                    onFileChanged(file.first, file.second.owners);
                }
            }
        }
    }

public:
    std::function<void(const std::string, const std::vector<void*> owners)> onFileChanged;

	void addFile(const std::string filename, void* owner) {
        if (files.find(filename) == files.end()) {
            files[filename] = FileWatchInfo{
                .filetime = std::filesystem::last_write_time(filename),
                .owners = { owner }
            };
        } else {
            // If the file is already present, only attach userData to the list, so the owning object gets properly notified
            files[filename].owners.push_back(owner);
        }
	}

    void addPipeline(Pipeline* pipeline) {
        for (auto& filename : pipeline->initialCreateInfo->shaders) {
            addFile(filename, pipeline);
        }
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