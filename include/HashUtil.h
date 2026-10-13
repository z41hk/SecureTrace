#pragma once

#include <cstdint>
#include <fstream>
#include <openssl/evp.h>
#include <string>
#include <vector>

class HashUtil {
public:
    static std::string sha256File(const std::string& path, double& sizeMb) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return {};
        }
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) {
            return {};
        }
        if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
            EVP_MD_CTX_free(ctx);
            return {};
        }
        std::vector<char> buffer(8192);
        std::uint64_t total = 0;
        while (file) {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize got = file.gcount();
            if (got > 0) {
                EVP_DigestUpdate(ctx, buffer.data(), static_cast<std::size_t>(got));
                total += static_cast<std::uint64_t>(got);
            }
        }
        unsigned char hash[32];
        unsigned int length = 0;
        EVP_DigestFinal_ex(ctx, hash, &length);
        EVP_MD_CTX_free(ctx);
        sizeMb = static_cast<double>(total) / (1024.0 * 1024.0);
        if (sizeMb < 0.01) {
            sizeMb = 0.01;
        }
        return toHex(hash, length);
    }

    static std::string sha256Text(const std::string& text, double& sizeMb) {
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) {
            return {};
        }
        if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
            EVP_MD_CTX_free(ctx);
            return {};
        }
        EVP_DigestUpdate(ctx, text.data(), text.size());
        unsigned char hash[32];
        unsigned int length = 0;
        EVP_DigestFinal_ex(ctx, hash, &length);
        EVP_MD_CTX_free(ctx);
        sizeMb = static_cast<double>(text.size()) / (1024.0 * 1024.0);
        if (sizeMb < 0.01) {
            sizeMb = 0.01;
        }
        return toHex(hash, length);
    }

    static std::string toHex(const unsigned char* data, unsigned int length) {
        static const char* digits = "0123456789abcdef";
        std::string out(static_cast<std::size_t>(length) * 2, '0');
        for (unsigned int i = 0; i < length; ++i) {
            out[i * 2] = digits[(data[i] >> 4) & 0x0F];
            out[i * 2 + 1] = digits[data[i] & 0x0F];
        }
        return out;
    }
};
