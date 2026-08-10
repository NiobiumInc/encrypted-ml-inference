// Copyright 2023-present Niobium Microsystems, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "weight_loader.h"
#include <fstream>
#include <stdexcept>

std::vector<float> load_weights(const std::string& path, size_t expected_count) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot open weight file: " + path);
    }
    auto size = f.tellg();
    size_t expected_bytes = expected_count * sizeof(float);
    if (static_cast<size_t>(size) != expected_bytes) {
        throw std::runtime_error(
            "Weight file " + path + " has " + std::to_string(size) +
            " bytes, expected " + std::to_string(expected_bytes));
    }
    std::vector<float> data(expected_count);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(data.data()), expected_bytes);
    return data;
}
