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
// Shared utilities for all model versions and compute targets.
#include "utils.h"
#include "mlp_common.h"
#include <sstream>
#include <string>

// -------------------------------------------------------------------- //
// Key and crypto context I/O
// -------------------------------------------------------------------- //

PublicKey<DCRTPoly> read_public_key(const InstanceParams& prms) {
    PublicKey<DCRTPoly> pk;
    if (!Serial::DeserializeFromFile(prms.pubkeydir()/"pk.bin", pk,
                                    SerType::BINARY)) {
        throw std::runtime_error("Failed to get public key from  " + prms.pubkeydir().string());
    }
    return pk;
}

PrivateKey<DCRTPoly> read_secret_key(const InstanceParams& prms) {
    PrivateKey<DCRTPoly> sk;
    if (!Serial::DeserializeFromFile(prms.seckeydir()/"sk.bin", sk,
                                    SerType::BINARY)) {
        throw std::runtime_error("Failed to get secret key from  " + prms.seckeydir().string());
    }
    return sk;
}

CryptoContext<DCRTPoly> read_crypto_context(const InstanceParams& prms) {
    CryptoContext<DCRTPoly> cc;
    if (!Serial::DeserializeFromFile(prms.pubkeydir()/"cc.bin", cc, SerType::BINARY)) {
        throw std::runtime_error("Failed to get CryptoContext from " + prms.pubkeydir().string());
    }
    return cc;
}

void read_eval_keys(const InstanceParams& prms, CryptoContext<DCRTPoly> cc) {
    std::ifstream emult_file(prms.pubkeydir()/"mk.bin", std::ios::in | std::ios::binary);
    if (!emult_file.is_open() ||
        !cc->DeserializeEvalMultKey(emult_file, SerType::BINARY)) {
      throw std::runtime_error(
        "Failed to get re-linearization key from " + prms.pubkeydir().string());
    }

    std::ifstream erot_file(prms.pubkeydir()/"rk.bin", std::ios::in | std::ios::binary);
    if (!erot_file.is_open() ||
        !cc->DeserializeEvalAutomorphismKey(erot_file, SerType::BINARY)) {
      throw std::runtime_error(
        "Failed to get rotation keys from " + prms.pubkeydir().string());
    }
}

// -------------------------------------------------------------------- //
// Score I/O
// -------------------------------------------------------------------- //

void load_scores(std::vector<Score> &dataset, const char *filename) {
  std::ifstream file(filename);
  Score score;
  std::string line;
  while (std::getline(file, line)) {
    std::istringstream iss(line);
    for (int i = 0; i < MNIST_LABEL_DIM; i++) {
      iss >> score.score[i];
    }
    dataset.push_back(score);
  }
}

void write_scores(const std::vector<Score> &dataset, const char *filename) {
  std::ofstream file(filename);
  for (const auto &score : dataset) {
    for (int i = 0; i < MNIST_LABEL_DIM; i++) {
      file << score.score[i];
      if (i < MNIST_LABEL_DIM - 1) {
        file << " ";
      }
    }
    file << "\n";
  }
}

// -------------------------------------------------------------------- //
// Utility
// -------------------------------------------------------------------- //

int argmax(float *A, int N) {
  int max_idx = 0;
  for (int i = 1; i < N; i++) {
    if (A[i] > A[max_idx]) {
      max_idx = i;
    }
  }
  return max_idx;
}
