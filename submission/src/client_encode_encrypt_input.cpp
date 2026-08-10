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
// HEIR v2 encryption: encrypts preprocessed input as vector<CiphertextT>.
#include <chrono>

#include "utils.h"
#include "mlp_encryption_utils.h"

using namespace lbcrypto;

int main(int argc, char* argv[]){

    if (argc < 2 || !std::isdigit(argv[1][0])) {
        std::cout << "Usage: " << argv[0] << " instance-size\n";
        std::cout << "  Instance-size: 0-single, 1-small, 2-medium (all ring 2^16, FPGA)\n";
        return 0;
    }
    auto size = static_cast<InstanceSize>(std::stoi(argv[1]));
    InstanceParams prms(size);

    // [TIMING] encrypt_ms marker (consumed by the nightly extractor) — same
    // [TIMING] <label>_ms convention as the NID workload.
    auto _t0 = std::chrono::high_resolution_clock::now();

    CryptoContext<DCRTPoly> cc = read_crypto_context(prms);
    PublicKey<DCRTPoly> pk = read_public_key(prms);

    std::vector<Sample> dataset;
    load_dataset(dataset, prms.preprocessed_input_file().c_str());
    if (dataset.empty()) {
        throw std::runtime_error("No data found in " + prms.preprocessed_input_file().string());
    }
    if (dataset.size() != prms.getBatchSize()) {
        throw std::runtime_error("Dataset size does not match instance size");
    }

    std::vector<CiphertextT> ctxt;
    fs::create_directories(prms.ctxtupdir());
    for (size_t i = 0; i < dataset.size(); ++i) {
        auto *input = dataset[i].image;
        std::vector<float> input_vector(input, input + MNIST_DIM);
        ctxt = mlp_encrypt_v2(cc, input_vector, pk);
        auto ctxt_path = prms.ctxtupdir()/("cipher_input_" + std::to_string(i) + ".bin");
        Serial::SerializeToFile(ctxt_path, ctxt, SerType::BINARY);
    }

    auto _ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::high_resolution_clock::now() - _t0).count();
    std::cout << "[TIMING] encrypt_ms: " << _ms << std::endl;
    return 0;
}
