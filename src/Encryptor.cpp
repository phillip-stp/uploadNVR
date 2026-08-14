#include "../include/Encryptor.h"
#include <iostream>
#include <cstring>

Encryptor::Encryptor(const unsigned char* secret_key) : is_open(false) {
    if (secret_key == nullptr) {
        std::cerr << "[FATAL ERROR] Encryptor initialised with a null key.\n";
        std::abort();
    }

    std::memcpy(key, secret_key, crypto_secretstream_xchacha20poly1305_KEYBYTES);

    // Pre-allocate 1 Megabyte to prevent resizing
    cipher_buffer.reserve(1024 * 1024);
}

Encryptor::~Encryptor() {
    if (is_open) {
        close();
    }
    sodium_memzero(key, sizeof(key));
    sodium_memzero(&st, sizeof(st));
}

bool Encryptor::open(const std::string& filepath) {
    if (is_open) close();

    out_file.open(filepath, std::ios::binary | std::ios::out);
    if (!out_file.is_open()) {
        std::cerr << "[ERROR] Encryptor could not open file: " << filepath << "\n";
        return false;
    }

    unsigned char header[crypto_secretstream_xchacha20poly1305_HEADERBYTES];
    
    // Initialise the state and generate the header
    if (crypto_secretstream_xchacha20poly1305_init_push(&st, header, key) != 0) {
        std::cerr << "[ERROR] Cryptographic state initialization failed.\n";
        out_file.close();
        return false;
    }

    out_file.write(reinterpret_cast<const char*>(header), sizeof(header));
    if (!out_file.good()) {
        std::cerr << "[ERROR] I/O failure writing header to: " << filepath << ". Disk could be full" << "\n";
        out_file.close();
        return false;
    }
    
    is_open = true;
    return true;
}

bool Encryptor::push_packet(const uint8_t* data, size_t len) {
    if (!is_open || data == nullptr || len == 0) return false;

    const size_t required_size = len + crypto_secretstream_xchacha20poly1305_ABYTES;
    if (cipher_buffer.size() < required_size) {
        cipher_buffer.resize(required_size);
    }

    unsigned long long ciphertext_len;

    // Encrypt the packet
    if (crypto_secretstream_xchacha20poly1305_push(
            &st, cipher_buffer.data(), &ciphertext_len,
            data, len,
            nullptr, 0,
            crypto_secretstream_xchacha20poly1305_TAG_MESSAGE) != 0) {

        std::cerr << "[ERROR] Encryption failed during push_packet.\n";
        return false;
    }

    out_file.write(reinterpret_cast<const char*>(cipher_buffer.data()), static_cast<std::streamsize>(ciphertext_len));
    return out_file.good();
}

bool Encryptor::close() {
    if (!is_open) return false;

    unsigned char ciphertext[crypto_secretstream_xchacha20poly1305_ABYTES];
    unsigned long long ciphertext_len;

    if (crypto_secretstream_xchacha20poly1305_push(
            &st, ciphertext, &ciphertext_len,
            nullptr, 0,
            nullptr, 0,
            crypto_secretstream_xchacha20poly1305_TAG_FINAL) != 0) {
        std::cerr << "[ERROR] Failed to generate TAG_FINAL seal.\n";
    }
    else {
        out_file.write(reinterpret_cast<const char*>(ciphertext), static_cast<std::streamsize>(ciphertext_len));
    }

    out_file.flush();

    // Check if the file is mathematically complete AND safely flushed to the physical disk
    bool is_valid_file = out_file.good();
    if (!is_valid_file) {
        std::cerr << "[WARNING] Encrypted file closed with I/O errors. It may be corrupted.\n";
    }

    out_file.close();
    is_open = false;
    return is_valid_file;
}