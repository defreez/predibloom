#pragma once

#include <string>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>

namespace predibloom::api {

struct AuthHeaders {
    std::string key_id;
    std::string timestamp;
    std::string signature;
};

class KalshiAuth {
public:
    KalshiAuth(const std::string& api_key_id, const std::string& key_file)
        : api_key_id_(api_key_id) {
        loadKey(key_file);
    }

    ~KalshiAuth() {
        if (pkey_) {
            EVP_PKEY_free(pkey_);
        }
    }

    KalshiAuth(const KalshiAuth&) = delete;
    KalshiAuth& operator=(const KalshiAuth&) = delete;

    // Generate auth headers for a request
    // method: "GET", "POST", etc.
    // path: full path including query string (query will be stripped for signing)
    AuthHeaders sign(const std::string& method, const std::string& path) const {
        // Get timestamp in milliseconds
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        std::string timestamp = std::to_string(ms);

        // Strip query params from path for signing
        std::string sign_path = path;
        auto qpos = sign_path.find('?');
        if (qpos != std::string::npos) {
            sign_path = sign_path.substr(0, qpos);
        }

        // Message to sign: timestamp + method + path (no query)
        std::string message = timestamp + method + sign_path;

        // Sign with RSA-PSS / SHA256
        std::string sig = rsaPssSign(message);

        // Base64 encode
        std::string sig_b64 = base64Encode(sig);

        return {api_key_id_, timestamp, sig_b64};
    }

private:
    void loadKey(const std::string& key_file) {
        FILE* fp = fopen(key_file.c_str(), "r");
        if (!fp) {
            throw std::runtime_error("Cannot open key file: " + key_file);
        }

        pkey_ = PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr);
        fclose(fp);

        if (!pkey_) {
            unsigned long err = ERR_get_error();
            char buf[256];
            ERR_error_string_n(err, buf, sizeof(buf));
            throw std::runtime_error(std::string("Failed to read private key: ") + buf);
        }
    }

    std::string rsaPssSign(const std::string& message) const {
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) {
            throw std::runtime_error("EVP_MD_CTX_new failed");
        }

        EVP_PKEY_CTX* pkey_ctx = nullptr;

        if (EVP_DigestSignInit(ctx, &pkey_ctx, EVP_sha256(), nullptr, pkey_) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP_DigestSignInit failed");
        }

        // Set RSA-PSS padding
        if (EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("Failed to set PSS padding");
        }

        // Set salt length to digest length (SHA256 = 32 bytes)
        if (EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, RSA_PSS_SALTLEN_DIGEST) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("Failed to set PSS salt length");
        }

        if (EVP_DigestSignUpdate(ctx,
                reinterpret_cast<const unsigned char*>(message.data()),
                message.size()) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP_DigestSignUpdate failed");
        }

        // Get signature length
        size_t sig_len = 0;
        if (EVP_DigestSignFinal(ctx, nullptr, &sig_len) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP_DigestSignFinal (length) failed");
        }

        std::vector<unsigned char> sig(sig_len);
        if (EVP_DigestSignFinal(ctx, sig.data(), &sig_len) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP_DigestSignFinal failed");
        }

        EVP_MD_CTX_free(ctx);
        return std::string(reinterpret_cast<char*>(sig.data()), sig_len);
    }

    static std::string base64Encode(const std::string& data) {
        BIO* b64 = BIO_new(BIO_f_base64());
        BIO* mem = BIO_new(BIO_s_mem());
        b64 = BIO_push(b64, mem);

        // No newlines
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

        BIO_write(b64, data.data(), static_cast<int>(data.size()));
        BIO_flush(b64);

        BUF_MEM* bptr;
        BIO_get_mem_ptr(b64, &bptr);

        std::string result(bptr->data, bptr->length);
        BIO_free_all(b64);
        return result;
    }

    std::string api_key_id_;
    EVP_PKEY* pkey_ = nullptr;
};

} // namespace predibloom::api
