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
// HEIR v2 key generation (ring 2^16, all instance sizes).
#include <chrono>

#include "utils.h"
#include "mlp_encryption_utils.h"

int main(int argc, char* argv[]){

    if (argc < 2 || !std::isdigit(argv[1][0])) {
        std::cout << "Usage: " << argv[0] << " instance-size\n";
        std::cout << "  Instance-size: 0-single, 1-small, 2-medium (all ring 2^16, FPGA)\n";
        return 0;
    }
    auto size = static_cast<InstanceSize>(std::stoi(argv[1]));
    InstanceParams prms(size);

    // [TIMING] keygen_ms marker (consumed by the nightly extractor) — same
    // [TIMING] <label>_ms convention as the NID workload.
    auto _t0 = std::chrono::high_resolution_clock::now();

    // Step 1: Setup CryptoContext (v2: depth 8, HYBRID; ring dim from prms)
    auto cryptoContext = mlp_generate_crypto_context_v2(prms);

    // Step 2: Key Generation
    auto keyPair = cryptoContext->KeyGen();
    cryptoContext = generate_mult_rot_key_v2(cryptoContext, keyPair.secretKey);

    // Step 3: Serialize cryptocontext and keys
    fs::create_directories(prms.pubkeydir());

    if (!Serial::SerializeToFile(prms.pubkeydir()/"cc.bin", cryptoContext,
                                SerType::BINARY) ||
        !Serial::SerializeToFile(prms.pubkeydir()/"pk.bin",
                                keyPair.publicKey, SerType::BINARY)) {
        throw std::runtime_error("Failed to write keys to " + prms.pubkeydir().string());
    }
    std::ofstream emult_file(prms.pubkeydir()/"mk.bin",
                           std::ios::out | std::ios::binary);
    std::ofstream erot_file(prms.pubkeydir()/"rk.bin",
                            std::ios::out | std::ios::binary);
    if (!emult_file.is_open() || !erot_file.is_open() ||
        !cryptoContext->SerializeEvalMultKey(emult_file, SerType::BINARY) ||
        !cryptoContext->SerializeEvalAutomorphismKey(erot_file, SerType::BINARY)) {
        throw std::runtime_error(
            "Failed to write eval keys to " + prms.pubkeydir().string());
    }

    fs::create_directories(prms.seckeydir());
    if (!Serial::SerializeToFile(prms.seckeydir()/"sk.bin",
                                keyPair.secretKey, SerType::BINARY)) {
        throw std::runtime_error("Failed to write keys to " + prms.seckeydir().string());
    }

    auto _ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::high_resolution_clock::now() - _t0).count();
    std::cout << "[TIMING] keygen_ms: " << _ms << std::endl;
    return 0;
}
