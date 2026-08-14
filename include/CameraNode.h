#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <deque>
#include <stdexcept>
#include "ThreadQueue.h"
#include "FrameData.h"

struct AVPacket;
struct AVFormatContext;

class CameraNode {
public:
    CameraNode(uint8_t id,
               std::string main_url,
               std::string sub_url,
               ThreadQueue<FrameData>& ai_queue,
               ThreadQueue<std::string>& upload_queue);

    ~CameraNode();

    CameraNode(const CameraNode&) = delete;
    CameraNode& operator=(const CameraNode&) = delete;

    [[nodiscard]] bool start();

    void stop();

    void trigger_recording();

    uint8_t get_id() const { return camera_id; }
    bool get_is_healthy() const { return is_healthy; }

private:
    void run_main_stream();
    void run_sub_stream();

    uint8_t camera_id;
    std::string main_rtsp_url;
    std::string sub_rtsp_url;

    ThreadQueue<FrameData>& central_ai_queue;
    ThreadQueue<std::string>& central_upload_queue;

    std::thread main_thread;
    std::thread sub_thread;

    std::atomic<bool> keep_running;
    std::atomic<uint64_t> last_detection_time{0};
    std::atomic<bool> is_healthy{true};

    std::deque<AVPacket*> ring_buffer;

    const int PRE_ROLL_SECONDS = 5;
    const int POST_ROLL_SECONDS = 5;
};