#include "../include/InferenceEngine.h"
#include <iostream>

constexpr int PERSON_CLASS_ID = 0;
constexpr float CONFIDENCE_THRESHOLD = 0.55f;

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

    size_t num_devices;
    edgetpu_device* devices = edgetpu_list_devices(&num_devices);

    if (num_devices == 0) {
        std::cerr << "[FATAL ERROR] No Edge TPU devices found. Is the USB connected and passed through to Docker?\n";
        std::abort();
    }

    std::cout << "[INFO] Found " << num_devices << " Edge TPU device(s). Initializing: "
              << devices[0].path << "\n";

    // Create the delegate using the first available TPU device
    edgetpu_delegate = edgetpu_create_delegate(devices[0].type, devices[0].path, nullptr, 0);

    // Free the device list (prevents memory leak)
    edgetpu_free_devices(devices);

    if (!edgetpu_delegate) {
        std::cerr << "[FATAL ERROR] Failed to create Edge TPU delegate.\n";
        std::abort();
    }

    // Apply the delegate to the interpreter graph
    if (interpreter->ModifyGraphWithDelegate(edgetpu_delegate) != kTfLiteOk) {
        std::cerr << "[FATAL ERROR] Failed to apply Edge TPU delegate to the TFLite graph.\n";
        std::abort();
    }
    // -------------------------------------------------------------------------

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        std::cerr << "[FATAL ERROR] Failed to allocate tensors.\n";
        std::abort();
    }
}

InferenceEngine::~InferenceEngine() {
    stop();

    interpreter.reset();

    if (edgetpu_delegate) {
        edgetpu_free_delegate(edgetpu_delegate);
        edgetpu_delegate = nullptr;
        std::cout << "[SHUTDOWN] Edge TPU delegate released successfully.\n";
    }
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
    const int input_tensor_idx = interpreter->inputs()[0];
    const TfLiteTensor* input_tensor = interpreter->tensor(input_tensor_idx);

    while (keep_running) {
        FrameData frame;

        // Wait safely until a camera pushes a frame
        ai_queue.wait_and_pop(frame);

        if (keep_running) {
            // Copy the 300x300 RGB pixels directly into the neural network's memory
            std::memcpy(input_tensor->data.uint8, frame.pixels.data(), frame.pixels.size());

            if (interpreter->Invoke() == kTfLiteOk) {
                // Parse the results
                const float* scores  = interpreter->typed_output_tensor<float>(0);
                const float* count   = interpreter->typed_output_tensor<float>(2);
                const float* classes = interpreter->typed_output_tensor<float>(3);

                if (classes && scores && count) {
                    const int num_detections = static_cast<int>(*count);
                    bool human_found = false;

                    for (int i = 0; i < num_detections && !human_found; ++i) {
                        const int class_id = static_cast<int>(classes[i]);
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