#include "Decryptor.h"
#include <iostream>
#include <fstream>
#include <cstring>

Decryptor::Decryptor(const unsigned char* secret_key) {
    if (secret_key == nullptr) {
        std::cerr << "[FATAL ERROR] Decryptor initialized with a null key.\n";
        std::abort();
    }
    std::memcpy(key, secret_key, crypto_secretstream_xchacha20poly1305_KEYBYTES);
}

Decryptor::~Decryptor() {
    sodium_memzero(key, sizeof(key));
}

bool Decryptor::decryptFile(const std::string& input_filepath, const std::string& output_filepath) const {
    std::ifstream in_file(input_filepath, std::ios::binary);
    if (!in_file.is_open()) {
        std::cerr << "[ERROR] Could not open encrypted file for reading: " << input_filepath << "\n";
        return false;
    }

    std::ofstream out_file(output_filepath, std::ios::binary);
    if (!out_file.is_open()) {
        std::cerr << "[ERROR] Could not open output file for writing: " << output_filepath << "\n";
        return false;
    }

    // Read the 24-byte stream header
    unsigned char header[crypto_secretstream_xchacha20poly1305_HEADERBYTES];
    if (!in_file.read(reinterpret_cast<char*>(header), sizeof(header))) {
        std::cerr << "[ERROR] File is too small to contain a valid header.\n";
        return false;
    }

    // Initialise the pull state
    crypto_secretstream_xchacha20poly1305_state st;
    if (crypto_secretstream_xchacha20poly1305_init_pull(&st, header, key) != 0) {
        std::cerr << "[ERROR] Invalid header or incorrect decryption key.\n";
        return false;
    }

    std::vector<uint8_t> cipher_buffer;
    std::vector<uint8_t> plain_buffer;
    unsigned char tag = 0;
    bool finished = false;

    while (in_file.good() && !finished) {

        // Read the 4-byte size header
        uint32_t chunk_len = 0;
        if (in_file.read(reinterpret_cast<char*>(&chunk_len), sizeof(chunk_len))) {

            // Validate chunk size to prevent memory allocation attacks from corrupted files
            if (chunk_len < crypto_secretstream_xchacha20poly1305_ABYTES || chunk_len > 10 * 1024 * 1024) {
                std::cerr << "[ERROR] Invalid chunk length detected: " << chunk_len << " bytes.\n";
                return false;
            }

            // Read the exact ciphertext payload
            cipher_buffer.resize(chunk_len);
            if (!in_file.read(reinterpret_cast<char*>(cipher_buffer.data()), chunk_len)) {
                std::cerr << "[ERROR] File truncated abruptly.\n";
                return false;
            }

            // The plaintext will be exactly ABYTES (17 bytes) smaller than the ciphertext
            size_t expected_plain_len = chunk_len - crypto_secretstream_xchacha20poly1305_ABYTES;
            plain_buffer.resize(expected_plain_len);

            unsigned long long plain_len = 0;

            // Decrypt and verify the MAC tag
            if (crypto_secretstream_xchacha20poly1305_pull(
                    &st, plain_buffer.data(), &plain_len, &tag,
                    cipher_buffer.data(), chunk_len,
                    nullptr, 0) != 0) {
                std::cerr << "[ERROR] Corrupted chunk or incorrect key at byte offset " << in_file.tellg() << ".\n";
                return false;
            }

            // Write to MP4 unless it was just the empty TAG_FINAL
            if (plain_len > 0) {
                out_file.write(reinterpret_cast<const char*>(plain_buffer.data()), plain_len);
            }

            // Stop if we reach the end-of-stream seal
            if (tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL) {
                finished = true;
            }
        }
        else {
            finished = true;
        }


    }

    return out_file.good();
}
