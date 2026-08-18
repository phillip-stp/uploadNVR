#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <filesystem>
#include <csignal>
#include <string>
#include <cstdlib>
#include <map>
#include <memory>
#include <sodium.h>
#include <yaml-cpp/yaml.h>

#include "CloudSync.h"
#include "FrameData.h"
#include "../include/ThreadQueue.h"
#include "../include/CameraNode.h"
#include "../include/InferenceEngine.h"

extern "C" {
#include <libavformat/avformat.h>
}

std::atomic<bool> keep_running{true};

void signal_handler(const int signal) {
    if (signal == SIGINT) {
        std::cout << "\n\n[SYSTEM] Commencing shutdown...\n";
        keep_running = false;
    }
}

extern constexpr int PRE_ROLL_SECONDS = 5;
extern constexpr int POST_ROLL_SECONDS = 5;

// Global buffer for the 32-byte encryption key
unsigned char GLOBAL_TEST_KEY[crypto_secretstream_xchacha20poly1305_KEYBYTES];

int main() {
    std::signal(SIGINT, signal_handler);

    std::cout << "--- Starting Complete NVR Pipeline ---\n";

    if (sodium_init() < 0) {
        std::cerr << "[FATAL ERROR] Libsodium failed to initialise.\n";
        return 1;
    }

    if (!std::filesystem::exists("storage")) std::filesystem::create_directories("storage");
    if (!std::filesystem::exists("models"))  std::filesystem::create_directories("models");

    avformat_network_init();

    // -------------------------------------------------------------------------
    // Load & Parse YAML Configuration
    // -------------------------------------------------------------------------
    YAML::Node config;
    try {
        config = YAML::LoadFile("config.yaml");
        std::cout << "[INFO] Successfully loaded config.yaml\n";
    } catch (const YAML::Exception& e) {
        std::cerr << "[FATAL ERROR] Failed to load config.yaml: " << e.what() << "\n";
        return 1;
    }

    // -------------------------------------------------------------------------
    // Parse Encryption Key
    // -------------------------------------------------------------------------
    if (!config["encryption_key"]) {
        std::cerr << "[FATAL ERROR] 'encryption_key' is missing in config.yaml\n";
        return 1;
    }

    std::string hex_key = config["encryption_key"].as<std::string>();

    // Decode the hex string into the raw byte array
    // sodium_hex2bin will fail if the string length doesn't map to exactly 32 bytes
    if (sodium_hex2bin(GLOBAL_TEST_KEY, sizeof(GLOBAL_TEST_KEY),
                       hex_key.c_str(), hex_key.length(),
                       NULL, NULL, NULL) != 0) {
        std::cerr << "[FATAL ERROR] Invalid 'encryption_key' in config.yaml. Must be a valid 64-character hexadecimal string.\n";
        return 1;
    }

    std::cout << "[INFO] Encryption key loaded successfully.\n";

    // -------------------------------------------------------------------------
    // Parse Cameras
    // -------------------------------------------------------------------------
    if (!config["cameras"] || !config["cameras"].IsSequence()) {
        std::cerr << "[FATAL ERROR] 'cameras' list is missing or invalid in config.yaml\n";
        return 1;
    }

    size_t num_cameras = config["cameras"].size();
    if (num_cameras == 0) {
        std::cerr << "[FATAL ERROR] No cameras defined in config.yaml\n";
        return 1;
    }

    std::cout << "[INFO] Detected " << num_cameras << " cameras in configuration.\n";

    // -------------------------------------------------------------------------
    // Shared Queues
    // -------------------------------------------------------------------------
    ThreadQueue<FrameData> ai_queue(num_cameras * 2);
    ThreadQueue<std::string> upload_queue;

    // -------------------------------------------------------------------------
    // Start CloudSync
    // -------------------------------------------------------------------------
    std::cout << "[INIT] Booting up CloudSync...\n";
    CloudSync cloud_sync(upload_queue);

    if (!cloud_sync.start()) {
        std::cerr << "[ERROR] CloudSync failed to start.\n";
        return 1;
    }

    // -------------------------------------------------------------------------
    // Instantiate & Start Camera Nodes Dynamically
    // -------------------------------------------------------------------------
    std::map<uint8_t, std::unique_ptr<CameraNode>> cameras;

    for (const auto& cam_node : config["cameras"]) {
        uint8_t cam_id         = cam_node["id"].as<uint8_t>();
        std::string main_rtsp  = cam_node["main_rtsp"].as<std::string>();
        std::string sub_rtsp   = cam_node["sub_rtsp"].as<std::string>();

        auto cam = std::make_unique<CameraNode>(cam_id, main_rtsp, sub_rtsp, ai_queue, upload_queue);

        if (!cam->start()) {
            std::cerr << "[ERROR] Camera " << (int)cam_id << " failed to start. Aborting.\n";
            for (auto& [started_id, started_cam] : cameras) {
                started_cam->stop();
            }
            cloud_sync.stop();
            return 1;
        }

        cameras[cam_id] = std::move(cam);
    }

    // -------------------------------------------------------------------------
    // Parse AI Config & Start Inference Engine
    // -------------------------------------------------------------------------
    std::cout << "[INIT] Booting up AI Inference Engine...\n";

    std::string model_path = "models/tf2_ssd_mobilenet_v2_coco17_ptq.tflite";
    if (config["model_path"]) {
        model_path = config["model_path"].as<std::string>();
    }

    InferenceEngine ai_engine(model_path, ai_queue, [&](const uint8_t cam_id) {
        auto it = cameras.find(cam_id);
        if (it != cameras.end()) {
            it->second->trigger_recording();
        } else {
            std::cerr << "[WARN] AI triggered recording for unknown camera ID: " << (int)cam_id << "\n";
        }
    });

    if (!ai_engine.start()) {
        std::cerr << "[ERROR] Inference Engine failed to start.\n";
        for (auto& [id, cam] : cameras) {
            cam->stop();
        }
        cloud_sync.stop();
        return 1;
    }

    // -------------------------------------------------------------------------
    // Main Event Loop (Runs until Ctrl+C)
    // -------------------------------------------------------------------------
    std::cout << "\n======================================================\n";
    std::cout << "                     UploadNVR \n";
    std::cout << "           Press Ctrl+C to safely shut down. \n";
    std::cout << "======================================================\n\n";

    while (keep_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // -------------------------------------------------------------------------
    // Graceful Teardown (Triggered by Ctrl+C)
    // -------------------------------------------------------------------------
    std::cout << "[SHUTDOWN] Stopping Cameras... (flushing active recordings)\n";
    for (auto& [id, cam] : cameras) {
        cam->stop();
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "[SHUTDOWN] Stopping AI Engine...\n";
    ai_engine.stop();

    std::cout << "[SHUTDOWN] Stopping CloudSync...\n";
    cloud_sync.stop();

    std::cout << "--- System Safely Halted ---\n";
    return 0;
}