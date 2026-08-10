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
#ifndef PARAMS_H_
#define PARAMS_H_
// params.h - parameters and directory structure for the workload

#include <vector>
#include <string>
#include <stdexcept>
#include <filesystem>
namespace fs = std::filesystem;

// an enum for benchmark size. All sizes use ring 2^16 (128-bit) for the Niobium
// accelerator path.
enum InstanceSize {
    SINGLE = 0,   // 1 batch,   ring 2^16, 128-bit (FPGA)
    SMALL  = 1,   // 5 batches, ring 2^16, 128-bit (FPGA)
    MEDIUM = 2,   // 20 batches, ring 2^16, 128-bit (FPGA)
};
inline std::string instance_name(const InstanceSize size) {
    if (unsigned(size) > unsigned(InstanceSize::MEDIUM)) {
        return "unknown";
    }
    static const std::string names[] = {"single", "small", "medium"};
    return names[int(size)];
}

// Parameters that differ for different instance sizes
class InstanceParams {
    const InstanceSize size;
    size_t batchSize;
    int ringDim;
    fs::path rootdir; // root of the submission dir structure (see below)

public:
    // Constructor
    explicit InstanceParams(InstanceSize _size,
                            fs::path _rootdir = fs::current_path())
                            : size(_size), rootdir(_rootdir)
    {
        if (unsigned(_size) > unsigned(InstanceSize::MEDIUM)) {
            throw std::invalid_argument("Invalid instance size");
        }

        //                        single  small  medium
        const int batchSizes[] = {     1,     5,     20};
        batchSize = batchSizes[int(_size)];

        // All sizes: ring 2^16, 128-bit security (FPGA / transport).
        ringDim = 65536;
    }

    // Getters for all the parameters. There are no setters, once
    // an object is constructed these parameters cannot be modified.
    const InstanceSize getSize() const { return size; }
    const size_t getBatchSize() const { return batchSize; }
    int getRingDim() const { return ringDim; }
    bool isLargeRing() const { return ringDim == 65536; }

    // The relevant directories where things are found
    fs::path rtdir() const  { return rootdir; }
    fs::path iodir() const  { return rootdir/"io"/instance_name(size); }
    fs::path pubkeydir() const { return iodir() / "public_keys"; }
    fs::path seckeydir() const { return iodir() / "secret_key"; }
    fs::path ctxtupdir() const { return iodir() / "ciphertexts_upload"; }
    fs::path ctxtdowndir() const { return iodir() / "ciphertexts_download"; }
    fs::path iointermdir() const { return iodir() / "intermediate"; }
    fs::path datadir() const {
        return rootdir/"datasets"/instance_name(size);
    }
    fs::path dataintermdir() const { return datadir() / "intermediate"; }
    fs::path test_input_file() const { return dataintermdir()/"test_pixels.txt"; }
    fs::path preprocessed_input_file() const { return iointermdir()/"preprocessed_input.txt"; }
    fs::path model_scores_file() const { return iointermdir()/"model_scores.txt"; }
    fs::path encrypted_model_predictions_file() const { return iodir()/"encrypted_model_predictions.txt"; }
    fs::path server_reported_steps_file() const { return iodir()/"server_reported_steps.json"; }
};

#endif  // ifndef PARAMS_H_