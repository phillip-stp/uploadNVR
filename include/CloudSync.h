#pragma once
#include <thread>
#include <atomic>
#include "ThreadQueue.h"

class CloudSync {
public:
    CloudSync(ThreadQueue<std::string> &upload_queue);

    ~CloudSync();

    bool start();
    void stop();

private:
    void run();
    static bool upload_and_delete_local(const std::string& file, const std::string& remote_name);
    bool perform_maintenance(const std::string& remote_name);


    ThreadQueue<std::string> &queue;
    std::atomic<bool> keep_running;
    std::thread worker_thread;
    std::vector<std::string> failed_uploads;



};
