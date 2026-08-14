#include "../include/InferenceEngine.h"
#include <iostream>

constexpr int PERSON_CLASS_ID = 0;
constexpr float CONFIDENCE_THRESHOLD = 0.65f;

InferenceEngine::InferenceEngine(const std::string &model_path,
                                 ThreadQueue<FrameData> &queue,
                                 std::function<void(uint8_t)> trigger_callback)
    : ai_queue(queue), on_human_detected(std::move(trigger_callback)), keep_running(false) {
    
    model = tflite::FlatBufferModel::BuildFromFile(model_path.c_str());
    if (!model) {
        std::cerr << "[FATAL ERROR] Failed to load TFLite model from: " << model_path << "\n";
        std::abort();
    }

    tflite::ops::builtin::BuiltinOpResolver resolver;
    tflite::InterpreterBuilder builder(*model, resolver);
    
    builder(&interpreter);
    if (!interpreter) {
        std::cerr << "[FATAL ERROR] Failed to construct TFLite interpreter.\n";
        std::abort();
    }

    // [EDGE TPU DELEGATE GOES HERE IN THE FUTURE] TODO
    
    if (interpreter->AllocateTensors() != kTfLiteOk) {
        std::cerr << "[FATAL ERROR] Failed to allocate tensors.\n";
        std::abort();
    }
}

InferenceEngine::~InferenceEngine() {
    stop();
}

bool InferenceEngine::start() {
    keep_running = true;
    try {
        worker_thread = std::thread(&InferenceEngine::run_inference_loop, this);
    } catch (const std::system_error& e) {
        std::cerr << "[CRITICAL ERROR] Failed to start Inference Engine thread: " << e.what() << "\n";
        keep_running = false;
        return false;
    }
    std::cout << "[INFO] Inference Engine started successfully.\n";
    return true;
}

void InferenceEngine::stop() {
    if (!keep_running) return;
    keep_running = false;
    
    // Push an empty frame to wake up the thread so it can exit cleanly
    ai_queue.push(FrameData{}); 
    
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
}

void InferenceEngine::run_inference_loop() {
    // Locate the memory address where the model expects the image pixels
    int input_tensor_idx = interpreter->inputs()[0];
    TfLiteTensor* input_tensor = interpreter->tensor(input_tensor_idx);

    while (keep_running) {
        FrameData frame;
        
        // Wait safely until a camera pushes a frame
        ai_queue.wait_and_pop(frame);

        if (keep_running) {
            // Copy the 300x300 RGB pixels directly into the neural network's memory
            std::memcpy(input_tensor->data.uint8, frame.pixels.data(), frame.pixels.size());

            if (interpreter->Invoke() == kTfLiteOk) {
                // Parse the results
                const float* classes = interpreter->typed_output_tensor<float>(1);
                const float* scores = interpreter->typed_output_tensor<float>(2);
                const float* count_ptr = interpreter->typed_output_tensor<float>(3);

                if (classes && scores && count_ptr) {
                    int num_detections = static_cast<int>(*count_ptr);
                    bool human_found = false;

                    for (int i = 0; i < num_detections && !human_found; ++i) {
                        int class_id = static_cast<int>(classes[i]);
                        float score = scores[i];

                        if (class_id == PERSON_CLASS_ID && score >= CONFIDENCE_THRESHOLD) {
                            human_found = true;
                        }
                    }

                    if (human_found) {
                        on_human_detected(frame.camera_id);
                    }
                }
                else {
                    std::cerr << "[WARNING] Malformed output tensors from model.\n";
                }
            }
            else{
                std::cerr << "[WARNING] Inference failed on frame from camera " << static_cast<int>(frame.camera_id) << "\n";
            }
        }
    }
}
