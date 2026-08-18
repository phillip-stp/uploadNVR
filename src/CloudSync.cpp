#include "../include/CloudSync.h"

#include <fstream>
#include <iostream>
#include <cstdio>
#include <chrono>

CloudSync::CloudSync(ThreadQueue<std::string> &upload_queue) : queue(upload_queue) {}

CloudSync::~CloudSync() {
    stop();
}

bool CloudSync::start() {
    keep_running = true;
    try {
        worker_thread = std::thread(&CloudSync::run, this);
    } catch (const std::system_error& e) {
        std::cerr << "[CRITICAL ERROR] Failed to start CloudSync thread: " << e.what() << "\n";
        keep_running = false;
        return false;
    }
    std::cout << "[INFO] CloudSync started successfully.\n";
    return true;
}

void CloudSync::stop() {
    if (!keep_running) return;
    keep_running = false;

    if (worker_thread.joinable()) {
        worker_thread.join();
    }
}

bool CloudSync::upload_and_delete_local(const std::string& file, const std::string& remote_name) {
    std::cout << "[INFO] CloudSync: Preparing to upload file: " << file << "\n";

    size_t slash_pos = file.find_last_of("/\\");
    std::string filename = (slash_pos == std::string::npos) ? file : file.substr(slash_pos + 1);

    std::string cam_folder = "unknown_camera";
    if (const size_t cam_pos = filename.find("cam_"); cam_pos != std::string::npos) {
        if (const size_t next_underscore = filename.find('_', cam_pos + 4); next_underscore != std::string::npos) {
            cam_folder = filename.substr(cam_pos, next_underscore - cam_pos);
        }
    }

    const std::string dest_path = remote_name + ":cameras/" + cam_folder + "/";
    const std::string upload_cmd = "rclone copy " + file + " " + dest_path;

    if (const int upload_status = std::system(upload_cmd.c_str()); upload_status == 0) {
        std::cout << "[INFO] CloudSync: Successfully uploaded to " << dest_path << "\n";

        if (std::remove(file.c_str()) == 0) {
            std::cout << "[INFO] CloudSync: Deleted local file: " << file << "\n";
        } else {
            std::cerr << "[WARNING] CloudSync: Failed to delete local file: " << file << "\n";
        }
        return true;
    }
    std::cerr << "[ERROR] CloudSync: Failed to upload file via rclone: " << file << "\n";
    return false;
}

bool CloudSync::perform_maintenance(const std::string& remote_name) {
    bool all_success = true;

    if (!failed_uploads.empty()) {
        std::cout << "[INFO] CloudSync: Retrying " << failed_uploads.size() << " failed uploads...\n";

        std::vector<std::string> still_failed;

        for (const std::string& file : failed_uploads) {
            if (!upload_and_delete_local(file, remote_name)) {
                still_failed.push_back(file);
                all_success = false;
            }
        }

        failed_uploads = still_failed;
    }

    std::cout << "[INFO] CloudSync: Running remote cloud cleanup (deleting files older than 7 days)...\n";
    const std::string maintenance_cmd = "rclone delete " + remote_name + ":cameras/ --min-age 7d";

    if (const int maintenance_status = std::system(maintenance_cmd.c_str()); maintenance_status != 0) {
        std::cerr << "[WARNING] CloudSync: Remote maintenance cleanup encountered an error.\n";
        all_success = false;
    }

    return all_success;
}

void CloudSync::run() {
    auto last_maintenance_time = std::chrono::steady_clock::now() - std::chrono::hours(24);
    const std::string remote_name = "gdrive";

    while (keep_running) {
        if (!queue.is_empty()) {
            std::string file;
            queue.wait_and_pop(file);

            if (!upload_and_delete_local(file, remote_name)) {
                failed_uploads.push_back(file);
                std::cout << "[INFO] CloudSync: Added " << file << " to retry queue.\n";
            }
        } else {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (auto now = std::chrono::steady_clock::now(); std::chrono::duration_cast<std::chrono::hours>(now - last_maintenance_time).count() >= 24) {

            if (perform_maintenance(remote_name)) {
                std::cout << "[INFO] CloudSync: All maintenance tasks completed successfully.\n";
            } else {
                std::cout << "[WARNING] CloudSync: Maintenance completed with errors. Will retry failed tasks later.\n";
            }

            last_maintenance_time = now;
        }
    }
}