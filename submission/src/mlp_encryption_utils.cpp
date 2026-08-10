// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Modifications Copyright 2023-present Niobium Microsystems, Inc.
// Licensed under the Apache License, Version 2.0.
//
// HEIR v2 encryption utilities for all instance sizes.
#include "utils.h"
#include "mlp_encryption_utils.h"
#include <sstream>
#include <string>

// -------------------------------------------------------------------- //
// v2 crypto context generation
// -------------------------------------------------------------------- //

CryptoContextT mlp_generate_crypto_context_v2(const InstanceParams& prms) {
  CCParamsT params;
  // HEIR v2 model: depth 8, HYBRID key switching.
  //
  // All sizes use ring 2^16 (65536), HEStd_128_classic — required for the
  // Niobium FPGA hardware. At N=65536, log(Q*P)~744 bits, well within
  // HEStd_128_classic (~1740 bits). Slot capacity N/2 = 32768 (the MLP uses
  // 1024). Upstream (Google reference) uses ring 2^15 — our 2^16 results are
  // not directly comparable (2x larger ciphertexts/keys).
  params.SetMultiplicativeDepth(8);
  params.SetKeySwitchTechnique(HYBRID);
  params.SetRingDim(65536);
  params.SetSecurityLevel(HEStd_128_classic);
  std::cout << "         [crypto] Ring dim 2^16 (65536), HEStd_128_classic (FPGA)" << std::endl;
  CryptoContextT cc = GenCryptoContext(params);
  cc->Enable(PKE);
  cc->Enable(KEYSWITCH);
  cc->Enable(LEVELEDSHE);
  return cc;
}

CryptoContextT generate_mult_rot_key_v2(CryptoContextT cc, PrivateKeyT sk) {
  cc->EvalMultKeyGen(sk);
  cc->EvalRotateKeyGen(sk, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 32, 46, 64, 69, 92, 115, 128, 138, 161, 184, 207, 230, 253, 256, 276, 299, 322, 345, 368, 391, 414, 437, 460, 483, 506, 512});
  return cc;
}

// -------------------------------------------------------------------- //
// v2 encrypt/decrypt
// -------------------------------------------------------------------- //

std::vector<CiphertextT> mlp_encrypt_v2(CryptoContextT cc, std::vector<float> v0, PublicKeyT pk) {
  [[maybe_unused]] size_t v1 = 0;
  std::vector<float> v2(1024, 0);
  [[maybe_unused]] int32_t v3 = 0;
  [[maybe_unused]] int32_t v4 = 1;
  [[maybe_unused]] int32_t v5 = 784;
  std::vector<float> v6 = v2;
  for (auto v7 = 0; v7 < 784; ++v7) {
    size_t v9 = static_cast<size_t>(v7);
    float v10 = v0[v9 + 784 * (0)];
    v6[v9 + 1024 * (0)] = v10;
  }
  std::vector<float> v12(1024);
  for (int64_t v12_i0 = 0; v12_i0 < 1; ++v12_i0) {
    for (int64_t v12_i1 = 0; v12_i1 < 1024; ++v12_i1) {
      v12[v12_i1 + 1024 * (v12_i0)] = v6[0 + v12_i1 * 1 + 1024 * (0 + v12_i0 * 1)];
    }
  }
  std::vector<double> v13(std::begin(v12), std::end(v12));
  auto pt_filled_n = cc->GetCryptoParameters()->GetElementParams()->GetRingDimension() / 2;
  auto pt_filled = v13;
  pt_filled.clear();
  pt_filled.reserve(pt_filled_n);
  for (size_t i = 0; i < pt_filled_n; ++i) {
    pt_filled.push_back(v13[i % v13.size()]);
  }
  auto pt = cc->MakeCKKSPackedPlaintext(pt_filled);
  const auto& ct = cc->Encrypt(pk, pt);
  const std::vector<CiphertextT> v14 = {ct};
  return v14;
}

std::vector<float> mlp_decrypt_v2(CryptoContextT cc, std::vector<CiphertextT> v0, PrivateKeyT sk) {
  [[maybe_unused]] size_t v1 = 0;
  [[maybe_unused]] int32_t v2 = 1024;
  [[maybe_unused]] int32_t v3 = 16;
  [[maybe_unused]] int32_t v4 = 6;
  [[maybe_unused]] int32_t v5 = 1;
  [[maybe_unused]] int32_t v6 = 0;
  std::vector<float> v7(10, 0);
  const auto& ct = v0[0];
  PlaintextT pt;
  cc->Decrypt(sk, ct, &pt);
  pt->SetLength(1024);
  const auto& v8_cast = pt->GetCKKSPackedValue();
  std::vector<float> v8(v8_cast.size());
  std::transform(std::begin(v8_cast), std::end(v8_cast), std::begin(v8), [](const std::complex<double>& c) { return c.real(); });
  std::vector<float> v9 = v7;
  for (auto v10 = 0; v10 < 1024; ++v10) {
    int32_t v12 = v10 + v4;
    int32_t v13 = v12 % v3;
    bool v14 = v13 >= v4;
    if (v14) {
      int32_t v16 = v10 % v3;
      size_t v17 = static_cast<size_t>(v10);
      float v18 = v8[v17 + 1024 * (0)];
      size_t v19 = static_cast<size_t>(v16);
      v9[v19 + 10 * (0)] = v18;
    } else {
    }
  }
  return v9;
}

// -------------------------------------------------------------------- //
// v2 dataset I/O (no padding, raw MNIST_DIM)
// -------------------------------------------------------------------- //

void load_dataset(std::vector<Sample> &dataset, const char *filename) {
  std::ifstream file(filename);
  Sample sample;
  std::string line;
  while (std::getline(file, line)) {
    std::istringstream iss(line);
    for (int i = 0; i < MNIST_DIM; i++) {
      iss >> sample.image[i];
    }
    dataset.push_back(sample);
  }
}

void write_dataset(const std::vector<Sample> &dataset, const char *filename) {
  std::ofstream file(filename);
  for (const auto &sample : dataset) {
    for (int i = 0; i < MNIST_DIM; i++) {
      file << sample.image[i];
      if (i < MNIST_DIM - 1) {
        file << " ";
      }
    }
    file << "\n";
  }
}
