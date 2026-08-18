#pragma once
#include <string>
#include <vector>
#include <sodium.h>

class Decryptor {
    unsigned char key[crypto_secretstream_xchacha20poly1305_KEYBYTES];

public:
    explicit Decryptor(const unsigned char* secret_key);
    ~Decryptor();

    [[nodiscard]] bool decryptFile(const std::string& input_filepath, const std::string& output_filepath) const;
};