# Copyright 2023-present Niobium Microsystems, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
cmake_minimum_required(VERSION 3.18)
project(ml-inference-fhe-sdk LANGUAGES C CXX)

# ── Self-contained SDK build (customer / FHETCH-transport flow) ────────────────
# Build the ML-inference stages against the niobium-client's OWN bundled OpenFHE +
# libnbfhetch (no compiler checkout). Enabled by scripts/build_task.sh:
#   cmake -S . -B build -DNIOBIUM_SDK_BUILD=ON \
#         -DCMAKE_PREFIX_PATH=<repo>/niobium-client/vendor/lib/openfhe
# Only server_encrypted_compute_sdk records/replays over the transport; the client
# stages (keygen, encrypt, decrypt, ...) are pure OpenFHE and link libnbfhetch
# passively to resolve the instrumented-OpenFHE globals.
# See NIOBIUM_INTEGRATION.md.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NIOBIUM_SDK_BUILD "Self-contained build against the niobium-client SDK" ON)
if(NOT NIOBIUM_SDK_BUILD)
  message(FATAL_ERROR
    "This root CMakeLists builds the SDK/transport flavor only (run scripts/build_task.sh).")
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(NB_CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/niobium-client")
# Overridable so CI can point at a prebuilt niobium-fhetch (matching a prebuilt
# libnbfhetch via -DNBFHETCH_LIB=...); defaults to the client's vendored copy.
set(NB_FHETCH_DIR "${NB_CLIENT_DIR}/vendor/niobium-fhetch" CACHE PATH
    "niobium-fhetch dir providing the SDK niobium/compiler.h")
set(SUB_SRC  "${CMAKE_CURRENT_SOURCE_DIR}/submission/src")
set(SUB_INC  "${CMAKE_CURRENT_SOURCE_DIR}/submission/include")

# ── OpenFHE discovery from CMAKE_PREFIX_PATH (client's bundled copy) ────
find_package(OpenFHE CONFIG REQUIRED)
message(STATUS "SDK build: OpenFHE ${OpenFHE_INCLUDE}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenFHE_CXX_FLAGS} -Wno-error")

# ── Include paths (niobium-fhetch FIRST is applied per-target below) ────
include_directories(
  ${OpenFHE_INCLUDE} ${OpenFHE_INCLUDE}/third-party/include
  ${OpenFHE_INCLUDE}/core ${OpenFHE_INCLUDE}/pke ${OpenFHE_INCLUDE}/binfhe
  ${SUB_INC}
  ${NB_FHETCH_DIR}/include                      # niobium/compiler.h (SDK)
  ${NB_FHETCH_DIR}/vendor/json/single_include)  # nlohmann/json (if vendored)
link_directories(${OpenFHE_LIBDIR})

# ── Locate libnbfhetch: search both possible build layouts ────
find_library(NBFHETCH_LIB NAMES nbfhetch
  PATHS "${NB_CLIENT_DIR}/build/vendor/niobium-fhetch"
        "${NB_CLIENT_DIR}/build/_deps/niobium-fhetch-build" NO_DEFAULT_PATH)
if(NOT NBFHETCH_LIB)
  message(FATAL_ERROR "libnbfhetch not found — run 'make -C niobium-client release' first"
                      " (or pass -DNBFHETCH_LIB=/path/to/libnbfhetch.{so,dylib}).")
endif()
message(STATUS "SDK build: nbfhetch ${NBFHETCH_LIB}")
get_filename_component(NBFHETCH_DIR "${NBFHETCH_LIB}" DIRECTORY)
set(CMAKE_BUILD_RPATH "${OpenFHE_LIBDIR};${NBFHETCH_DIR}")

# zlib is OPTIONAL: the client's bundled OpenFHE already pulls in libz at runtime.
find_package(ZLIB QUIET)
set(SDK_ZLIB "")
if(ZLIB_FOUND)
  set(SDK_ZLIB ZLIB::ZLIB)
endif()

# ── Linux linker flags: keep OpenFHE in DT_NEEDED for --as-needed ────
# libnbfhetch defers its OpenFHE (lbcrypto) symbols to the final link.
if(UNIX AND NOT APPLE)
  add_link_options(-Wl,--no-as-needed -Wl,--disable-new-dtags)
endif()

# ── Shared libraries (pure OpenFHE; link libnbfhetch passively) ────
add_library(mlp_common            ${SUB_SRC}/mlp_common.cpp)
add_library(mlp_encryption_utils  ${SUB_SRC}/mlp_encryption_utils.cpp)
target_link_libraries(mlp_encryption_utils PUBLIC mlp_common)
add_library(weight_loader         ${SUB_SRC}/weight_loader.cpp)

# ── Circuit TU: mlp_openfhe.cpp holds mnist() and all the ct·ct EvalMult +
#    rotation ops. It MUST be recorded, so it gets NIOBIUM_COMPILER +
#    OPENFHE_CPROBES (else the recorder sees 0 muls) and the niobium-fhetch
#    include FIRST so niobium::compiler() resolves to the SDK (libnbfhetch). ──
add_library(mlp_openfhe ${SUB_SRC}/mlp_openfhe.cpp)
target_include_directories(mlp_openfhe BEFORE PRIVATE ${NB_FHETCH_DIR}/include)
target_compile_definitions(mlp_openfhe PRIVATE NIOBIUM_COMPILER OPENFHE_CPROBES)
target_link_libraries(mlp_openfhe PRIVATE ${NBFHETCH_LIB})

# ── Client stages: plain OpenFHE; link nbfhetch to resolve globals ────
add_executable(client_key_generation      ${SUB_SRC}/client_key_generation.cpp)
add_executable(client_preprocess_input     ${SUB_SRC}/client_preprocess_input.cpp)
add_executable(client_encode_encrypt_input ${SUB_SRC}/client_encode_encrypt_input.cpp)
add_executable(client_decrypt_decode       ${SUB_SRC}/client_decrypt_decode.cpp)
add_executable(client_postprocess          ${SUB_SRC}/client_postprocess.cpp)
add_executable(server_preprocess_model     ${SUB_SRC}/server_preprocess_model.cpp)
foreach(stage client_key_generation client_preprocess_input client_encode_encrypt_input
               client_decrypt_decode client_postprocess)
  target_link_libraries(${stage} mlp_encryption_utils ${OpenFHE_SHARED_LIBRARIES} ${NBFHETCH_LIB} ${SDK_ZLIB})
endforeach()
target_link_libraries(server_preprocess_model ${OpenFHE_SHARED_LIBRARIES} ${NBFHETCH_LIB} ${SDK_ZLIB})

# ── SDK-linked compute: record/replay via libnbfhetch -> FHETCH transport ──
add_executable(server_encrypted_compute_sdk ${SUB_SRC}/server_encrypted_compute_sdk.cpp)
target_include_directories(server_encrypted_compute_sdk BEFORE PRIVATE ${NB_FHETCH_DIR}/include)
target_compile_definitions(server_encrypted_compute_sdk PRIVATE NIOBIUM_COMPILER OPENFHE_CPROBES)
target_link_libraries(server_encrypted_compute_sdk
  mlp_openfhe mlp_encryption_utils weight_loader mlp_common
  ${OpenFHE_SHARED_LIBRARIES} ${NBFHETCH_LIB} ${SDK_ZLIB})

# Everything lands in build/ (harness/run_submission.py expects it there).
set(SDK_TARGETS
  client_key_generation client_preprocess_input client_encode_encrypt_input
  client_decrypt_decode client_postprocess server_preprocess_model
  server_encrypted_compute_sdk)
set_target_properties(${SDK_TARGETS} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
