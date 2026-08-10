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
// HEIR v2 decryption: deserializes vector<CiphertextT>, extracts 10 scores.
//
// Two modes:
//   client_decrypt_decode <size>
//       decrypt every batch -> io/<name>/intermediate/model_scores.txt
//       (used by client_postprocess for the batch quality check).
//   client_decrypt_decode <size> --batch_id=N [--save-scores=PATH]
//       decrypt only batch N; with --save-scores also write a per-batch CSV
//       (class,score for the 10 logits) for the transport run artifact.
#include <chrono>

#include "utils.h"
#include "iomanip"
#include "limits"

#include "mlp_encryption_utils.h"

using namespace lbcrypto;

// Decrypt one result ciphertext file into a Score (10 logits).
static Score decrypt_one(CryptoContext<DCRTPoly> cc, PrivateKey<DCRTPoly> sk,
                         const fs::path &ctxt_path) {
    std::vector<Ciphertext<DCRTPoly>> ctxt;
    if (!Serial::DeserializeFromFile(ctxt_path, ctxt, SerType::BINARY)) {
        throw std::runtime_error("Failed to get ciphertext from " + ctxt_path.string());
    }
    auto decrypted_output = mlp_decrypt_v2(cc, ctxt, sk);
    Score score;
    for (int j = 0; j < MNIST_LABEL_DIM; ++j) {
        score.score[j] = decrypted_output[j];
    }
    return score;
}

int main(int argc, char* argv[]) {
    if (argc < 2 || !std::isdigit(argv[1][0])) {
        std::cout << "Usage: " << argv[0]
                  << " instance-size [--batch_id=N] [--save-scores=PATH]\n";
        std::cout << "  Instance-size: 0-single, 1-small, 2-medium (all ring 2^16, FPGA)\n";
        return 0;
    }
    auto size = static_cast<InstanceSize>(std::stoi(argv[1]));
    InstanceParams prms(size);

    int batch_id = -1;           // -1 = all batches (default)
    std::string save_scores;     // empty = don't write a per-batch CSV
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.rfind("--batch_id=", 0) == 0) {
            batch_id = std::stoi(arg.substr(11));
        } else if (arg.rfind("--save-scores=", 0) == 0) {
            save_scores = arg.substr(14);
        }
    }

    // [TIMING] decrypt_ms marker (same convention as the other phases).
    auto _t0 = std::chrono::high_resolution_clock::now();

    CryptoContext<DCRTPoly> cc;
    if (!Serial::DeserializeFromFile(prms.pubkeydir()/"cc.bin", cc,
                                    SerType::BINARY)) {
        throw std::runtime_error("Failed to get CryptoContext from  " + prms.pubkeydir().string());
    }
    PrivateKey<DCRTPoly> sk;
    if (!Serial::DeserializeFromFile(prms.seckeydir()/"sk.bin", sk,
                                    SerType::BINARY)) {
        throw std::runtime_error("Failed to get secret key from  " + prms.seckeydir().string());
    }

    if (batch_id >= 0) {
        // Single-batch mode (transport run artifact).
        auto ctxt_path = prms.ctxtdowndir()/("cipher_result_" + std::to_string(batch_id) + ".bin");
        Score score = decrypt_one(cc, sk, ctxt_path);
        int label = argmax(score.score, MNIST_LABEL_DIM);
        std::cout << "[decrypt] batch " << batch_id << " -> predicted label " << label << std::endl;
        if (!save_scores.empty()) {
            fs::create_directories(fs::path(save_scores).parent_path());
            std::ofstream csv(save_scores);
            csv << "class,score\n";
            for (int j = 0; j < MNIST_LABEL_DIM; ++j) {
                csv << j << "," << std::setprecision(9) << score.score[j] << "\n";
            }
            std::cout << "[decrypt] wrote scores -> " << save_scores << std::endl;
        }
    } else {
        // All-batches mode (batch quality check via client_postprocess).
        std::vector<Score> scores;
        auto result_path = prms.model_scores_file();
        fs::create_directories(prms.iointermdir());
        for (size_t i = 0; i < prms.getBatchSize(); ++i) {
            auto ctxt_path = prms.ctxtdowndir()/("cipher_result_" + std::to_string(i) + ".bin");
            scores.push_back(decrypt_one(cc, sk, ctxt_path));
        }
        write_scores(scores, result_path.c_str());
    }

    auto _ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::high_resolution_clock::now() - _t0).count();
    std::cout << "[TIMING] decrypt_ms: " << _ms << std::endl;
    return 0;
}
