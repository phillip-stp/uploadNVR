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

    ThreadQueue<std::string> &queue;
    std::atomic<bool> keep_running;
    std::thread worker_thread;

};
