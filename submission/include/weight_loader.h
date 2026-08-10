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

#ifndef WEIGHT_LOADER_H_
#define WEIGHT_LOADER_H_

#include <string>
#include <vector>

// Load a binary file of float32 values into a std::vector<float>.
// The file must contain exactly `expected_count` floats (4 bytes each).
std::vector<float> load_weights(const std::string& path, size_t expected_count);

#endif  // WEIGHT_LOADER_H_
