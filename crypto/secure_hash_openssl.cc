// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crypto/secure_hash.h"

#include <openssl/mem.h>
#include <openssl/sha.h>
#include <stddef.h>

#include "base/logging.h"
#include "base/pickle.h"
#include "crypto/openssl_util.h"

namespace crypto {

namespace {

class SecureHashSHA256OpenSSL : public SecureHash {
 public:
  SecureHashSHA256OpenSSL() {
    SHA256_Init(&ctx_);
  }

  SecureHashSHA256OpenSSL(const SecureHashSHA256OpenSSL& other) {
    memcpy(&ctx_, &other.ctx_, sizeof(ctx_));
  }

  ~SecureHashSHA256OpenSSL() override {
    OPENSSL_cleanse(&ctx_, sizeof(ctx_));
  }

  void Update(const void* input, size_t len) override {
    SHA256_Update(&ctx_, static_cast<const unsigned char*>(input), len);
  }

  void Finish(void* output, size_t len) override {
    ScopedOpenSSLSafeSizeBuffer<SHA256_DIGEST_LENGTH> result(
        static_cast<unsigned char*>(output), len);
    SHA256_Final(result.safe_buffer(), &ctx_);
  }

  SecureHash* Clone() const override {
    return new SecureHashSHA256OpenSSL(*this);
  }

  size_t GetHashLength() const override { return SHA256_DIGEST_LENGTH; }

 private:
  SHA256_CTX ctx_;
};

}  // namespace

SecureHash* SecureHash::Create(Algorithm algorithm) {
  switch (algorithm) {
    case SHA256:
      return new SecureHashSHA256OpenSSL();
    default:
      NOTIMPLEMENTED();
      return NULL;
  }
}

}  // namespace crypto
