#include "../include/CloudSync.h"

#include <iostream>


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

void CloudSync::run() {
    while (keep_running) {
        if (!queue.is_empty()) {
            std::string file;
            queue.wait_and_pop(file);
            std::cout << "[INFO] CloudSync: Preparing to upload file: " << file << "\n";
            // upload file
            // delete file if successful from disk
            // keep running
            // also do maintenance tasks like delete old files



        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}