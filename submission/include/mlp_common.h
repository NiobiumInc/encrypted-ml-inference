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
// Shared utilities for all compute targets (CPU/GPU/FPGA).
// This header is type-neutral — it does NOT define CiphertextT.
#ifndef MLP_COMMON_H_
#define MLP_COMMON_H_

#include "openfhe.h"
#include "params.h"

using namespace lbcrypto;

// MNIST constants
constexpr int MNIST_DIM = 784;
constexpr int MNIST_LABEL_DIM = 10;

// Score data structure (shared: same 10-class output for all models)
struct Score {
  float score[MNIST_LABEL_DIM];
};

// Key and crypto context I/O (shared across all model versions and targets)
PublicKey<DCRTPoly> read_public_key(const InstanceParams& prms);
PrivateKey<DCRTPoly> read_secret_key(const InstanceParams& prms);
CryptoContext<DCRTPoly> read_crypto_context(const InstanceParams& prms);
void read_eval_keys(const InstanceParams& prms, CryptoContext<DCRTPoly> cc);

// Score I/O
void load_scores(std::vector<Score> &dataset, const char *filename);
void write_scores(const std::vector<Score> &dataset, const char *filename);

// Utility
int argmax(float *A, int N);

#endif  // ifndef MLP_COMMON_H_
