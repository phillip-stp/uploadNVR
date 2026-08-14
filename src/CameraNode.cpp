#include "../include/CameraNode.h"
#include <iostream>
#include <utility>
#include <chrono>
#include <optional>
#include "Encryptor.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

// helpers

namespace {
    auto get_time = [](const AVPacket* p) {
        return (p->pts != AV_NOPTS_VALUE) ? p->pts : p->dts;
    };

    std::optional<int> get_video_stream_idx(const AVFormatContext *in_ctx) {
        std::optional<int> video_stream_idx;
        for (unsigned int i = 0; i < in_ctx->nb_streams; i++) {
            if (in_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_stream_idx = i;
                break;
            }
        }
        return video_stream_idx;
    }

    bool openAndFindStreamInfo(AVFormatContext* &in_ctx, const std::string &url, const uint8_t camera_id) {
        if (avformat_open_input(&in_ctx, url.c_str(), nullptr, nullptr) != 0) {
            std::cerr << "[ERROR] Could not open stream for " << static_cast<int>(camera_id) << "\n";
            return false;
        }

        if (avformat_find_stream_info(in_ctx, nullptr) < 0) {
            std::cerr << "[ERROR] Failed to read stream info for camera " << static_cast<int>(camera_id) << "\n";
            avformat_close_input(&in_ctx);
            return false;
        }
        return true;
    }

    std::optional<AVCodecContext*> setupDecoder(AVFormatContext* &in_ctx, const std::optional<int> video_stream_idx, const uint8_t camera_id) {
        AVCodecParameters* codecpar = in_ctx->streams[video_stream_idx.value()]->codecpar;
        const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            std::cerr << "[ERROR] Unsupported codec for camera " << static_cast<int>(camera_id) << "\n";
            avformat_close_input(&in_ctx);
            return std::nullopt;
        }

        AVCodecContext* decoder_ctx = avcodec_alloc_context3(decoder);
        if (!decoder_ctx) {
            std::cerr << "[ERROR] Failed to allocate decoder context.\n";
            avformat_close_input(&in_ctx);
            return std::nullopt;
        }
        avcodec_parameters_to_context(decoder_ctx, codecpar);

        if (avcodec_open2(decoder_ctx, decoder, nullptr) < 0) {
            std::cerr << "[ERROR] Failed to open codec for camera " << static_cast<int>(camera_id) << "\n";
            avcodec_free_context(&decoder_ctx);
            avformat_close_input(&in_ctx);
            return std::nullopt;
        }

        return decoder_ctx;
    }

}

CameraNode::CameraNode(const uint8_t id, std::string main_url, std::string sub_url,
                       ThreadQueue<FrameData>& ai_queue, ThreadQueue<std::string>& upload_queue)
    : camera_id(id), main_rtsp_url(std::move(main_url)), sub_rtsp_url(std::move(sub_url)),
      central_ai_queue(ai_queue), central_upload_queue(upload_queue),
      keep_running(false) {}

CameraNode::~CameraNode() {
    stop();
}

[[nodiscard]] bool CameraNode::start() {
    keep_running = true;

    try {
        main_thread = std::thread(&CameraNode::run_main_stream, this);
        sub_thread = std::thread(&CameraNode::run_sub_stream, this);
    }
    catch (const std::system_error& e) {
        std::cerr << "[CRITICAL ERROR] Failed to spawn threads for camera "
                  << static_cast<int>(camera_id) << ". Reason: " << e.what() << "\n";

        keep_running = false;

        if (main_thread.joinable()) main_thread.join();
        if (sub_thread.joinable()) sub_thread.join();
        return false;
    }

    std::cout << "[INFO] Camera " << static_cast<int>(camera_id) << " started successfully.\n";
    return true;
}

void CameraNode::stop() {
    keep_running = false;

    if (main_thread.joinable()) main_thread.join();
    if (sub_thread.joinable()) sub_thread.join();

    for (AVPacket* pkt : ring_buffer) {
        av_packet_free(&pkt);
    }

    ring_buffer.clear();
}

void CameraNode::trigger_recording() {
    const auto now = std::chrono::system_clock::now();
    last_detection_time = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

void CameraNode::run_main_stream() {
    AVFormatContext* in_ctx = nullptr;

    if (!openAndFindStreamInfo(in_ctx, main_rtsp_url, camera_id)) {
        is_healthy = false;
        return;
    }

    const int64_t time_seconds_pts = PRE_ROLL_SECONDS * 90000;
    bool is_writing_to_file = false;
    std::string current_filepath;


    const std::optional<int> video_stream_idx = get_video_stream_idx(in_ctx);

    if (!video_stream_idx) {
        std::cerr << "[ERROR] No video stream found for camera " << static_cast<int>(camera_id) << "\n";
        avformat_close_input(&in_ctx);
        is_healthy = false;
        return;
    }

    unsigned char dummy_key[crypto_secretstream_xchacha20poly1305_KEYBYTES];
    crypto_secretstream_xchacha20poly1305_keygen(dummy_key);
    Encryptor encryptor(dummy_key);

    while (keep_running) {


        AVPacket* pkt = av_packet_alloc();

        if (av_read_frame(in_ctx, pkt) < 0) {
            std::cerr << "[WARNING] Stream dropped for camera " << static_cast<int>(camera_id) << "\n";
            av_packet_free(&pkt);
            is_healthy = false;
            break;
        }

        if (pkt->stream_index == video_stream_idx.value()) {
            // Calculate how long it has been since the AI last saw a human
            auto now = std::chrono::system_clock::now();
            const uint64_t current_unix_time = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

            // The cooldown is active if the last detection was less than POST_ROLL_SECONDS ago
            const bool ai_sees_human = (current_unix_time - last_detection_time.load()) <= static_cast<uint64_t>(POST_ROLL_SECONDS);

            // Transition from idle to recording
            if (!is_writing_to_file && ai_sees_human) {
                is_writing_to_file = true;

                current_filepath = "storage/cam_" + std::to_string(camera_id) + "_" + std::to_string(current_unix_time) + ".enc";
                if (encryptor.open(current_filepath)) {
                    is_writing_to_file = true;
                    std::cout << "[INFO] Human detected. Starting recording...\n";

                    for (AVPacket* buffered_pkt : ring_buffer) {
                        encryptor.push_packet(buffered_pkt->data, buffered_pkt->size);
                        av_packet_free(&buffered_pkt);
                    }
                    ring_buffer.clear();
                }
                else {
                    std::cerr << "[ERROR] Storage failure. Could not start recording for camera " << static_cast<int>(camera_id) << "\n";
                }
            }

            // Recording
            if (is_writing_to_file) {
                encryptor.push_packet(pkt->data, pkt->size);
                av_packet_free(&pkt);

                // if AI no longer sees human go back to idle
                if (!ai_sees_human) {
                    is_writing_to_file = false;
                    if (encryptor.close()) {
                        central_upload_queue.push(current_filepath);
                    }
                    else {
                        std::remove(current_filepath.c_str());
                        std::cerr << "[ERROR] Event corrupted during save. Deleted locally.\n";
                    }
                    std::cout << "[INFO] Event saved and queued for upload.\n";
                }
            }
            // idle
            else {
                ring_buffer.push_back(pkt);

                int64_t buffer_duration = get_time(ring_buffer.back()) - get_time(ring_buffer.front());

                while (buffer_duration > time_seconds_pts && !ring_buffer.empty()) {
                    AVPacket* old_pkt = ring_buffer.front();
                    ring_buffer.pop_front();
                    av_packet_free(&old_pkt);

                    if (!ring_buffer.empty()) {
                        buffer_duration = get_time(ring_buffer.back()) - get_time(ring_buffer.front());
                    }
                }
            }
        }
        else {
            av_packet_free(&pkt);
        }
    }
    //If the network drops while recording, make sure we still queue the partial file
    if (is_writing_to_file) {
        if (encryptor.close()) {
            central_upload_queue.push(current_filepath);
            std::cerr << "[WARNING] Event saved and queued for upload. However a network drop while recording occurred some footage maybe lost.\n";
        } else {
            std::remove(current_filepath.c_str());
            std::cerr << "[ERROR] Event corrupted during save. Deleted locally.\n";
        }
    }

    avformat_close_input(&in_ctx);
}

void CameraNode::run_sub_stream() {
    AVFormatContext* in_ctx = nullptr;

    if (!openAndFindStreamInfo(in_ctx, sub_rtsp_url, camera_id)) {
        is_healthy = false;
        return;
    }

    // Find the video stream index
    const std::optional<int> video_stream_idx = get_video_stream_idx(in_ctx);

    if (!video_stream_idx) {
        std::cerr << "[ERROR] No video stream found for camera " << static_cast<int>(camera_id) << "\n";
        avformat_close_input(&in_ctx);
        is_healthy = false;
        return;
    }

    // Set up the Decoder
    std::optional<AVCodecContext*> decoder_ctx = setupDecoder(in_ctx, video_stream_idx, camera_id);
    if (!decoder_ctx) {
        is_healthy = false;
        return;
    }

    // Set up the Scaler
    SwsContext* sws_ctx = sws_getContext(
        decoder_ctx.value()->width, decoder_ctx.value()->height, decoder_ctx.value()->pix_fmt,
        FrameData::width, FrameData::height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!sws_ctx) {
        std::cerr << "[ERROR] Failed to create sws context.\n";
        AVCodecContext* decoder_ptr = decoder_ctx.value();
        avcodec_free_context(&decoder_ptr);
        avformat_close_input(&in_ctx);
        is_healthy = false;
        return;
    }

    AVFrame* raw_frame = av_frame_alloc();
    AVFrame* rgb_frame = av_frame_alloc();

    // Allocate memory for the 300x300 RGB frame
    const int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, FrameData::width, FrameData::height, 1);
    std::vector<uint8_t> rgb_buffer(num_bytes);
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, rgb_buffer.data(),
                         AV_PIX_FMT_RGB24, FrameData::width, FrameData::height, 1);


    while (keep_running) {
        AVPacket* pkt = av_packet_alloc();

        if (av_read_frame(in_ctx, pkt) < 0) {
            std::cerr << "[WARNING] Stream dropped for camera " << static_cast<int>(camera_id) << "\n";
            av_packet_free(&pkt);
            is_healthy = false;
            break;
        }

        if (pkt->stream_index == video_stream_idx.value()) {

            // Send compressed packet to decoder
            if (avcodec_send_packet(decoder_ctx.value(), pkt) == 0) {

                // Receive uncompressed frame
                while (avcodec_receive_frame(decoder_ctx.value(), raw_frame) == 0) {

                    // Scale and convert to RGB
                    sws_scale(sws_ctx, raw_frame->data, raw_frame->linesize, 0,
                              decoder_ctx.value()->height, rgb_frame->data, rgb_frame->linesize);

                    FrameData data;
                    data.camera_id = camera_id;
                    data.timestamp = pkt->pts;
                    data.pixels = rgb_buffer;

                    central_ai_queue.push(data);
                }
            }
        }
        av_packet_free(&pkt);
    }

    // Cleanup
    av_frame_free(&raw_frame);
    av_frame_free(&rgb_frame);
    sws_freeContext(sws_ctx);
    AVCodecContext* decoder_ptr = decoder_ctx.value();
    avcodec_free_context(&decoder_ptr);
    avformat_close_input(&in_ctx);
}