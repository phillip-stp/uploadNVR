#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include "FrameData.h"
#include "ThreadQueue.h"

// TensorFlow Lite Headers
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h" // Do not remove
#include "tensorflow/lite/model.h"

class InferenceEngine {
public:
    // Takes the model path, the queue to consume from, and a callback function
    InferenceEngine(const std::string &model_path,
                    ThreadQueue<FrameData> &queue,
                    std::function<void(uint8_t)> trigger_callback);
    
    ~InferenceEngine();

    bool start();
    void stop();

private:
    void run_inference_loop();

    ThreadQueue<FrameData> &ai_queue;
    std::function<void(uint8_t)> on_human_detected;
    
    std::atomic<bool> keep_running;
    std::thread worker_thread;

    std::unique_ptr<tflite::FlatBufferModel> model;
    std::unique_ptr<tflite::Interpreter> interpreter;
};
