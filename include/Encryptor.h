#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sodium.h>

class Encryptor {
public:

    explicit Encryptor(const unsigned char* secret_key);
    ~Encryptor();

    bool open(const std::string& filepath);
    bool push_packet(const uint8_t* data, size_t len);
    bool close();

    [[nodiscard]] bool is_ready() const { return is_open; }

private:
    std::ofstream out_file;
    crypto_secretstream_xchacha20poly1305_state st{};
    unsigned char key[crypto_secretstream_xchacha20poly1305_KEYBYTES]{};
    bool is_open;

    std::vector<unsigned char> cipher_buffer;
};