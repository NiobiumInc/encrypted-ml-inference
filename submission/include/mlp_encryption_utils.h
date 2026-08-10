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
// Uses CiphertextT = Ciphertext<DCRTPoly> (mutable).
#ifndef MLP_ENCRYPTION_UTILS_H_
#define MLP_ENCRYPTION_UTILS_H_

#include "mlp_common.h"

using CiphertextT = Ciphertext<DCRTPoly>;
using ConstCiphertextT = ConstCiphertext<DCRTPoly>;
using CCParamsT = CCParams<CryptoContextCKKSRNS>;
using CryptoContextT = CryptoContext<DCRTPoly>;
using EvalKeyT = EvalKey<DCRTPoly>;
using PlaintextT = Plaintext;
using PrivateKeyT = PrivateKey<DCRTPoly>;
using PublicKeyT = PublicKey<DCRTPoly>;

struct Sample {
  float image[MNIST_DIM];
};

// v2 crypto context (depth 8, HYBRID)
// All instance sizes: ring 2^16 (65536), 128-bit security (FPGA).
CryptoContextT mlp_generate_crypto_context_v2(const InstanceParams& prms);
CryptoContextT generate_mult_rot_key_v2(CryptoContextT cc, PrivateKeyT sk);

// v2 encrypt/decrypt (vector<CiphertextT>)
std::vector<CiphertextT> mlp_encrypt_v2(CryptoContextT cc, std::vector<float> v0, PublicKeyT pk);
std::vector<float> mlp_decrypt_v2(CryptoContextT cc, std::vector<CiphertextT> v0, PrivateKeyT sk);

// v2 dataset I/O (reads MNIST_DIM values, no padding)
void load_dataset(std::vector<Sample> &dataset, const char *filename);
void write_dataset(const std::vector<Sample> &dataset, const char *filename);

#endif  // ifndef MLP_ENCRYPTION_UTILS_H_
