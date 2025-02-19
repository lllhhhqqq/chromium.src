// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/certificate_provider/certificate_provider_service.h"

#include <stdint.h>
#include <set>
#include <utility>

#include "base/bind.h"
#include "base/macros.h"
#include "base/stl_util.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "chrome/browser/chromeos/certificate_provider/certificate_provider.h"
#include "net/base/net_errors.h"
#include "net/base/test_data_directory.h"
#include "net/ssl/client_key_store.h"
#include "net/test/cert_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {

namespace {

const char kExtension1[] = "extension1";
const char kExtension2[] = "extension2";

void ExpectEmptySignatureAndStoreError(net::Error* out_error,
                                       net::Error error,
                                       const std::vector<uint8_t>& signature) {
  EXPECT_TRUE(signature.empty());
  *out_error = error;
}

void ExpectOKAndStoreSignature(std::vector<uint8_t>* out_signature,
                               net::Error error,
                               const std::vector<uint8_t>& signature) {
  EXPECT_EQ(net::OK, error);
  *out_signature = signature;
}

void StoreCertificates(net::CertificateList* out_certs,
                       const net::CertificateList& certs) {
  if (out_certs)
    *out_certs = certs;
}

certificate_provider::CertificateInfo CreateCertInfo(
    const std::string& cert_filename) {
  certificate_provider::CertificateInfo cert_info;
  cert_info.certificate =
      net::ImportCertFromFile(net::GetTestCertsDirectory(), cert_filename);
  EXPECT_NE(nullptr, cert_info.certificate) << "Could not load "
                                            << cert_filename;
  cert_info.type = net::SSLPrivateKey::Type::RSA;
  cert_info.supported_hashes.push_back(net::SSLPrivateKey::Hash::SHA256);
  cert_info.max_signature_length_in_bytes = 123;

  return cert_info;
}

bool IsKeyEqualToCertInfo(const certificate_provider::CertificateInfo& info,
                          net::SSLPrivateKey* key) {
  if (info.supported_hashes != key->GetDigestPreferences())
    return false;

  return key->GetType() == info.type &&
         key->GetMaxSignatureLengthInBytes() ==
             info.max_signature_length_in_bytes;
}

class TestDelegate : public CertificateProviderService::Delegate {
 public:
  enum class RequestType { NONE, SIGN, GET_CERTIFICATES };

  TestDelegate() {}

  std::vector<std::string> CertificateProviderExtensions() override {
    return std::vector<std::string>(provider_extensions_.begin(),
                                    provider_extensions_.end());
  }

  void BroadcastCertificateRequest(int cert_request_id) override {
    EXPECT_EQ(expected_request_type_, RequestType::GET_CERTIFICATES);
    last_cert_request_id_ = cert_request_id;
    expected_request_type_ = RequestType::NONE;
  }

  bool DispatchSignRequestToExtension(
      const std::string& extension_id,
      int sign_request_id,
      net::SSLPrivateKey::Hash hash,
      const scoped_refptr<net::X509Certificate>& certificate,
      const std::string& input) override {
    EXPECT_EQ(expected_request_type_, RequestType::SIGN);
    last_sign_request_id_ = sign_request_id;
    last_extension_id_ = extension_id;
    last_certificate_ = certificate;
    expected_request_type_ = RequestType::NONE;
    return true;
  }

  // Prepares this delegate for the dispatch of a request of type
  // |expected_request_type|. The first request of the right type will cause
  // |expected_request_type_| to be reset to NONE. The request's arguments will
  // be stored in |last_*_request_id_| and |last_extension_id_|. Any additional
  // request and any request of the wrong type will fail the test.
  void ClearAndExpectRequest(RequestType expected_request_type) {
    last_extension_id_.clear();
    last_sign_request_id_ = -1;
    last_cert_request_id_ = -1;
    last_certificate_ = nullptr;
    expected_request_type_ = expected_request_type;
  }

  int last_sign_request_id_ = -1;
  int last_cert_request_id_ = -1;
  scoped_refptr<net::X509Certificate> last_certificate_;
  std::string last_extension_id_;
  std::set<std::string> provider_extensions_;
  RequestType expected_request_type_ = RequestType::NONE;

 private:
  DISALLOW_COPY_AND_ASSIGN(TestDelegate);
};

}  // namespace

class CertificateProviderServiceTest : public testing::Test {
 public:
  CertificateProviderServiceTest()
      : task_runner_(new base::TestMockTimeTaskRunner()),
        task_runner_handle_(task_runner_),
        client_key_store_(net::ClientKeyStore::GetInstance()),
        service_(new CertificateProviderService()),
        cert_info1_(CreateCertInfo("client_1.pem")),
        cert_info2_(CreateCertInfo("client_2.pem")) {
    std::unique_ptr<TestDelegate> test_delegate(new TestDelegate);
    test_delegate_ = test_delegate.get();
    service_->SetDelegate(std::move(test_delegate));

    certificate_provider_ = service_->CreateCertificateProvider();
    EXPECT_TRUE(certificate_provider_);

    test_delegate_->provider_extensions_.insert(kExtension1);
  }

  // Triggers a GetCertificates request and returns the request id. Assumes that
  // at least one extension is registered as a certificate provider.
  int RequestCertificatesFromExtensions(net::CertificateList* certs) {
    test_delegate_->ClearAndExpectRequest(
        TestDelegate::RequestType::GET_CERTIFICATES);

    certificate_provider_->GetCertificates(
        base::Bind(&StoreCertificates, certs));

    task_runner_->RunUntilIdle();
    EXPECT_EQ(TestDelegate::RequestType::NONE,
              test_delegate_->expected_request_type_);
    return test_delegate_->last_cert_request_id_;
  }

  // Provides |cert_info1_| through kExtension1.
  void ProvideDefaultCert() {
    const int cert_request_id = RequestCertificatesFromExtensions(nullptr);
    SetCertificateProvidedByExtension(kExtension1, cert_request_id,
                                      cert_info1_);
    task_runner_->RunUntilIdle();
  }

  // Like service_->SetCertificatesProvidedByExtension but taking a single
  // CertificateInfo instead of a list.
  void SetCertificateProvidedByExtension(
      const std::string& extension_id,
      int cert_request_id,
      const certificate_provider::CertificateInfo& cert_info) {
    certificate_provider::CertificateInfoList infos;
    infos.push_back(cert_info);
    service_->SetCertificatesProvidedByExtension(extension_id, cert_request_id,
                                                 infos);
  }

  bool CheckLookUpCertificate(
      const certificate_provider::CertificateInfo& cert_info,
      bool expected_is_certificate_known,
      bool expected_is_currently_provided,
      const std::string& expected_extension_id) {
    bool is_currently_provided = !expected_is_currently_provided;
    std::string extension_id;
    if (expected_is_certificate_known !=
        service_->LookUpCertificate(*cert_info.certificate,
                                    &is_currently_provided, &extension_id)) {
      LOG(ERROR) << "Wrong return value.";
      return false;
    }
    if (expected_is_currently_provided != is_currently_provided) {
      LOG(ERROR) << "Wrong |is_currently_provided|.";
      return false;
    }
    if (expected_extension_id != extension_id) {
      LOG(ERROR) << "Wrong extension id. Got " << extension_id;
      return false;
    }
    return true;
  }

  scoped_refptr<base::TestMockTimeTaskRunner> task_runner_;
  base::ThreadTaskRunnerHandle task_runner_handle_;
  TestDelegate* test_delegate_ = nullptr;
  net::ClientKeyStore* const client_key_store_;
  std::unique_ptr<CertificateProvider> certificate_provider_;
  std::unique_ptr<CertificateProviderService> service_;
  const certificate_provider::CertificateInfo cert_info1_;
  const certificate_provider::CertificateInfo cert_info2_;

 private:
  DISALLOW_COPY_AND_ASSIGN(CertificateProviderServiceTest);
};

TEST_F(CertificateProviderServiceTest, GetCertificates) {
  test_delegate_->provider_extensions_.insert(kExtension2);

  net::CertificateList certs;
  const int cert_request_id = RequestCertificatesFromExtensions(&certs);

  task_runner_->RunUntilIdle();
  // No certificates set until all registered extensions replied.
  EXPECT_TRUE(certs.empty());

  SetCertificateProvidedByExtension(kExtension1, cert_request_id, cert_info1_);

  task_runner_->RunUntilIdle();
  // No certificates set until all registered extensions replied.
  EXPECT_TRUE(certs.empty());

  SetCertificateProvidedByExtension(kExtension2, cert_request_id, cert_info2_);

  task_runner_->RunUntilIdle();
  EXPECT_EQ(2u, certs.size());

  // Verify that the ClientKeyStore returns key handles for the provide certs.
  EXPECT_TRUE(
      client_key_store_->FetchClientCertPrivateKey(*cert_info1_.certificate));
  EXPECT_TRUE(
      client_key_store_->FetchClientCertPrivateKey(*cert_info2_.certificate));

  // Deregister the extensions as certificate providers. The next
  // GetCertificates call must report an empty list of certs.
  test_delegate_->provider_extensions_.clear();

  // No request expected.
  test_delegate_->ClearAndExpectRequest(TestDelegate::RequestType::NONE);

  certificate_provider_->GetCertificates(
      base::Bind(&StoreCertificates, &certs));

  task_runner_->RunUntilIdle();
  // As |certs| was not empty before, this ensures that StoreCertificates() was
  // called.
  EXPECT_TRUE(certs.empty());
}

TEST_F(CertificateProviderServiceTest, LookUpCertificate) {
  // Provide only |cert_info1_|.
  {
    const int cert_request_id = RequestCertificatesFromExtensions(nullptr);
    SetCertificateProvidedByExtension(kExtension1, cert_request_id,
                                      cert_info1_);
    task_runner_->RunUntilIdle();
  }

  EXPECT_TRUE(CheckLookUpCertificate(cert_info1_, true /* is known */,
                                     true /* is currently provided */,
                                     kExtension1));

  EXPECT_TRUE(CheckLookUpCertificate(cert_info2_, false /* is not known */,
                                     false /* is currently not provided */,
                                     std::string()));

  // Provide only |cert_info2_| from |kExtension2|.
  test_delegate_->provider_extensions_.insert(kExtension2);
  {
    const int cert_request_id = RequestCertificatesFromExtensions(nullptr);
    service_->SetCertificatesProvidedByExtension(
        kExtension1, cert_request_id,
        certificate_provider::CertificateInfoList());
    SetCertificateProvidedByExtension(kExtension2, cert_request_id,
                                      cert_info2_);
    task_runner_->RunUntilIdle();
  }

  EXPECT_TRUE(CheckLookUpCertificate(cert_info1_, true /* is known */,
                                     false /* is currently not provided */,
                                     std::string()));

  EXPECT_TRUE(CheckLookUpCertificate(cert_info2_, true /* is known */,
                                     true /* is currently provided */,
                                     kExtension2));

  // Deregister |kExtension2| as certificate provider and provide |cert_info1_|
  // from |kExtension1|.
  test_delegate_->provider_extensions_.erase(kExtension2);

  {
    const int cert_request_id = RequestCertificatesFromExtensions(nullptr);
    SetCertificateProvidedByExtension(kExtension1, cert_request_id,
                                      cert_info1_);
    task_runner_->RunUntilIdle();
  }

  EXPECT_TRUE(CheckLookUpCertificate(cert_info1_, true /* is known */,
                                     true /* is currently provided */,
                                     kExtension1));

  EXPECT_TRUE(CheckLookUpCertificate(cert_info2_, true /* is known */,
                                     false /* is currently not provided */,
                                     std::string()));

  // Provide |cert_info2_| from |kExtension1|.
  {
    const int cert_request_id = RequestCertificatesFromExtensions(nullptr);
    SetCertificateProvidedByExtension(kExtension1, cert_request_id,
                                      cert_info2_);
    task_runner_->RunUntilIdle();
  }

  {
    bool is_currently_provided = true;
    std::string extension_id;
    // |cert_info1_.certificate| was provided before, so this must return true.
    EXPECT_TRUE(service_->LookUpCertificate(
        *cert_info1_.certificate, &is_currently_provided, &extension_id));
    EXPECT_FALSE(is_currently_provided);
    EXPECT_TRUE(extension_id.empty());
  }

  {
    bool is_currently_provided = false;
    std::string extension_id;
    EXPECT_TRUE(service_->LookUpCertificate(
        *cert_info2_.certificate, &is_currently_provided, &extension_id));
    EXPECT_TRUE(is_currently_provided);
    EXPECT_EQ(kExtension1, extension_id);
  }

  EXPECT_TRUE(CheckLookUpCertificate(cert_info1_, true /* is known */,
                                     false /* is currently not provided */,
                                     std::string()));

  EXPECT_TRUE(CheckLookUpCertificate(cert_info2_, true /* is known */,
                                     true /* is currently provided */,
                                     kExtension1));
}

TEST_F(CertificateProviderServiceTest, GetCertificatesTimeout) {
  test_delegate_->provider_extensions_.insert(kExtension2);

  net::CertificateList certs;
  const int cert_request_id = RequestCertificatesFromExtensions(&certs);

  certificate_provider::CertificateInfoList infos;
  SetCertificateProvidedByExtension(kExtension1, cert_request_id, cert_info1_);

  task_runner_->RunUntilIdle();
  // No certificates set until all registered extensions replied or a timeout
  // occurred.
  EXPECT_TRUE(certs.empty());

  task_runner_->FastForwardUntilNoTasksRemain();
  // After the timeout, only extension1_'s certificates are returned.
  // This verifies that the timeout delay is > 0 but not how long the delay is.
  EXPECT_EQ(1u, certs.size());

  EXPECT_TRUE(
      client_key_store_->FetchClientCertPrivateKey(*cert_info1_.certificate));
}

TEST_F(CertificateProviderServiceTest, UnloadExtensionAfterGetCertificates) {
  test_delegate_->provider_extensions_.insert(kExtension2);

  const int cert_request_id = RequestCertificatesFromExtensions(nullptr);

  SetCertificateProvidedByExtension(kExtension1, cert_request_id, cert_info1_);
  SetCertificateProvidedByExtension(kExtension2, cert_request_id, cert_info2_);
  task_runner_->RunUntilIdle();

  // Private key handles for both certificates must be available now.
  EXPECT_TRUE(
      client_key_store_->FetchClientCertPrivateKey(*cert_info1_.certificate));
  EXPECT_TRUE(
      client_key_store_->FetchClientCertPrivateKey(*cert_info2_.certificate));

  // Unload one of the extensions.
  service_->OnExtensionUnloaded(kExtension2);

  // extension1 isn't affected by the uninstall.
  EXPECT_TRUE(
      client_key_store_->FetchClientCertPrivateKey(*cert_info1_.certificate));
  // No key handles that were backed by the uninstalled extension must be
  // returned.
  EXPECT_FALSE(
      client_key_store_->FetchClientCertPrivateKey(*cert_info2_.certificate));
}

TEST_F(CertificateProviderServiceTest, UnloadExtensionDuringGetCertificates) {
  test_delegate_->provider_extensions_.insert(kExtension2);

  net::CertificateList certs;
  const int cert_request_id = RequestCertificatesFromExtensions(&certs);

  SetCertificateProvidedByExtension(kExtension1, cert_request_id, cert_info1_);

  // The pending certificate request is only waiting for kExtension2. Unloading
  // that extension must cause the request to be finished.
  service_->OnExtensionUnloaded(kExtension2);

  task_runner_->RunUntilIdle();
  EXPECT_EQ(1u, certs.size());
}

// Trying to sign data using the exposed SSLPrivateKey must cause a sign
// request. The reply must be correctly routed back to the private key.
TEST_F(CertificateProviderServiceTest, SignRequest) {
  ProvideDefaultCert();

  scoped_refptr<net::SSLPrivateKey> private_key(
      client_key_store_->FetchClientCertPrivateKey(*cert_info1_.certificate));

  ASSERT_TRUE(private_key);
  EXPECT_TRUE(IsKeyEqualToCertInfo(cert_info1_, private_key.get()));

  test_delegate_->ClearAndExpectRequest(TestDelegate::RequestType::SIGN);

  std::vector<uint8_t> received_signature;
  private_key->SignDigest(
      net::SSLPrivateKey::Hash::SHA256, std::string("any input data"),
      base::Bind(&ExpectOKAndStoreSignature, &received_signature));

  task_runner_->RunUntilIdle();

  const int sign_request_id = test_delegate_->last_sign_request_id_;
  EXPECT_EQ(TestDelegate::RequestType::NONE,
            test_delegate_->expected_request_type_);
  EXPECT_TRUE(
      cert_info1_.certificate->Equals(test_delegate_->last_certificate_.get()));

  // No signature received until the extension replied to the service.
  EXPECT_TRUE(received_signature.empty());

  std::vector<uint8_t> signature_reply;
  signature_reply.push_back(5);
  signature_reply.push_back(7);
  signature_reply.push_back(8);
  service_->ReplyToSignRequest(kExtension1, sign_request_id, signature_reply);

  task_runner_->RunUntilIdle();
  EXPECT_EQ(signature_reply, received_signature);
}

TEST_F(CertificateProviderServiceTest, UnloadExtensionDuringSign) {
  ProvideDefaultCert();

  scoped_refptr<net::SSLPrivateKey> private_key(
      client_key_store_->FetchClientCertPrivateKey(*cert_info1_.certificate));
  ASSERT_TRUE(private_key);

  test_delegate_->ClearAndExpectRequest(TestDelegate::RequestType::SIGN);

  net::Error error = net::OK;
  private_key->SignDigest(
      net::SSLPrivateKey::Hash::SHA256, std::string("any input data"),
      base::Bind(&ExpectEmptySignatureAndStoreError, &error));

  task_runner_->RunUntilIdle();

  // No signature received until the extension replied to the service or is
  // unloaded.
  EXPECT_EQ(net::OK, error);

  // Unload the extension.
  service_->OnExtensionUnloaded(kExtension1);

  task_runner_->RunUntilIdle();
  EXPECT_EQ(net::ERR_FAILED, error);
}

}  // namespace chromeos
