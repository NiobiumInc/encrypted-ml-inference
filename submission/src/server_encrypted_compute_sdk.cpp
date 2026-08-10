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
// SDK / FHETCH-transport server compute (links the niobium-client SDK, libnbfhetch).
// Records the MLP inference once on a cache miss (hollow mode), then ALWAYS
// replays to produce the result; replay dispatches on --target (`local`
// in-process simulator, or `FOG` / another backend id over the transport).
// --cpu-only is a separate plaintext-FHE reference pass. See
// NIOBIUM_INTEGRATION.md for the full record/replay flow and transport notes.

#include <chrono>
#include <fstream>
#include <iostream>

#include "mlp_encryption_utils.h"
#include "mlp_openfhe.h"
#include "openfhe.h"
#include "params.h"
#include "utils.h"
#include "weight_loader.h"

#include "niobium/compiler.h"

using namespace lbcrypto;

// [TIMING] markers consumed by the run harness / breakdown extractor.
static void emit_timing(const char *key, long long ms) {
  std::cout << "[TIMING] " << key << ": " << ms << std::endl;
}
static auto now_ms() { return std::chrono::high_resolution_clock::now(); }
static long long elapsed_ms(std::chrono::high_resolution_clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::high_resolution_clock::now() - t0)
      .count();
}

// Weight file paths (relative to working directory = repo root).
static const std::string WEIGHT_DIR = "submission/data/";

// The compute always reads this stable per-run input path (a single
// Ciphertext). The harness stages the current batch's ciphertext here before
// each call, so a recorded trace can be replayed against new input data by
// refreshing this one file (cooperative replay).
static std::string input_path(const InstanceParams &prms) {
  return (prms.ctxtupdir() / "cipher_input.bin").string();
}

int main(int argc, char *argv[]) {
  if (argc < 2 || !std::isdigit(argv[1][0])) {
    std::cout << "Usage: " << argv[0]
              << " instance-size [--batch_id=<N>] [--target <TARGET>]"
                 " [--opt-level <O0|O1|O2|O3>] [--cpu-only]\n";
    std::cout << "  Instance-size: 0-single, 1-small, 2-medium"
                 " (all ring 2^16, FPGA)\n";
    std::cout << "  --batch_id:  batch index, used only for the output filename"
                 " (default 0)\n";
    std::cout << "  --target:    replay target — local (default, in-process sim,"
                 " no backend) or FOG; other backend ids are forwarded to init()\n";
    std::cout << "  --opt-level: replay opt level (default from init; O3 for"
                 " a hardware target)\n";
    std::cout << "  --cpu-only:  compute the plaintext-FHE reference on CPU and"
                 " write the result (no record/replay)\n";
    return 0;
  }

  // Parse OUR flags from the raw argv BEFORE init(): the SDK's init() consumes
  // several Niobium flags (--target / --opt-level / -O) and compacts argv.
  auto size = static_cast<InstanceSize>(std::stoi(argv[1]));
  InstanceParams prms(size);
  int batch_id = 0;
  bool cpu_only = false;
  for (int i = 2; i < argc; i++) {
    std::string arg = argv[i];
    if (arg.rfind("--batch_id=", 0) == 0) {
      batch_id = std::stoi(arg.substr(11));
    } else if (arg == "--cpu-only") {
      cpu_only = true;
    }
  }

  niobium::compiler().init(argc, argv);

  // Load model weights (needed for the record pass and for --cpu-only).
  std::cout << "[server-sdk] Loading model weights..." << std::endl;
  auto fc1_weight = load_weights(WEIGHT_DIR + "fc1_weight.bin", 512 * 784);
  auto fc1_bias   = load_weights(WEIGHT_DIR + "fc1_bias.bin", 512);
  auto fc2_weight = load_weights(WEIGHT_DIR + "fc2_weight.bin", 10 * 512);
  auto fc2_bias   = load_weights(WEIGHT_DIR + "fc2_bias.bin", 10);

  fs::create_directories(prms.ctxtdowndir());
  auto result_ctxt_path =
      prms.ctxtdowndir() / ("cipher_result_" + std::to_string(batch_id) + ".bin");

  // ---------- CPU-only mode: the plaintext-FHE reference ("expected") --------
  // Run mnist() on the CPU without touching the recorder (auto-tagging off,
  // start() never called), then write the result. Used by the harness for the
  // expected-vs-got comparison.
  if (cpu_only) {
    CryptoContext<DCRTPoly> cc = read_crypto_context(prms);
    read_eval_keys(prms, cc);
    std::vector<CiphertextT> iv;
    if (!Serial::DeserializeFromFile(input_path(prms), iv, SerType::BINARY)) {
      std::cerr << "[ERROR] cannot read input " << input_path(prms) << std::endl;
      return 1;
    }
    auto _t = now_ms();
    auto ctxtResults =
        mnist(cc, fc1_weight, fc1_bias, fc2_weight, fc2_bias, {iv[0]});
    emit_timing("cpu_compute_ms", elapsed_ms(_t));
    if (!Serial::SerializeToFile(result_ctxt_path, ctxtResults, SerType::BINARY)) {
      std::cerr << "[ERROR] cannot write " << result_ctxt_path << std::endl;
      return 1;
    }
    std::cout << "[server-sdk] cpu-only (expected) done -> " << result_ctxt_path
              << std::endl;
    return 0;
  }

  // ---------- Cooperative record / replay (auto-tagging) ---------------------
  {
    niobium::Compiler::CacheParameters params;
    params.push_back({"wl", argv[1]});  // cache key = size only (batch-independent)
    niobium::compiler().cache_parameters(params);
  }
  niobium::compiler().set_program_info("ml-inference", "1.0",
                                       "ML inference server compute (SDK)");
  niobium::compiler().set_build_info(__FILE__, __LINE__, __TIMESTAMP__);

  // Load the crypto context + BOTH eval keys (relin mk.bin + rotation rk.bin —
  // the BSGS matmul rotates, so both must be present) + the input. These
  // deserializes happen HERE (a TU compiled with OPENFHE_CPROBES) so the
  // auto-facade hooks fire and capture the context/keys/inputs — including the
  // ring dimension replay() needs. (Delegating to mlp_common's read_* helpers,
  // compiled without cprobes, would skip the hooks.)
  CryptoContext<DCRTPoly> cc;
  {
    auto _t = now_ms();
    if (!Serial::DeserializeFromFile(prms.pubkeydir() / "cc.bin", cc, SerType::BINARY)) {
      std::cerr << "[ERROR] cannot read " << (prms.pubkeydir() / "cc.bin") << std::endl;
      return 1;
    }
    std::ifstream mk(prms.pubkeydir() / "mk.bin", std::ios::in | std::ios::binary);
    if (!mk.is_open() || !cc->DeserializeEvalMultKey(mk, SerType::BINARY)) {
      std::cerr << "[ERROR] cannot read relin key mk.bin" << std::endl;
      return 1;
    }
    std::ifstream rk(prms.pubkeydir() / "rk.bin", std::ios::in | std::ios::binary);
    if (!rk.is_open() || !cc->DeserializeEvalAutomorphismKey(rk, SerType::BINARY)) {
      std::cerr << "[ERROR] cannot read rotation keys rk.bin" << std::endl;
      return 1;
    }
    emit_timing("cc_key_load_ms", elapsed_ms(_t));
  }
  // Set the ring dimension / context for replay(). (Auto-tagging streams the
  // input+key data, but the crypto context still needs to be captured for
  // replay to know the ring dimension + modulus chain.)
  niobium::compiler().capture_crypto_context(cc);

  std::vector<CiphertextT> ctxt_vec;
  if (!Serial::DeserializeFromFile(input_path(prms), ctxt_vec, SerType::BINARY)) {
    std::cerr << "[ERROR] cannot read input " << input_path(prms) << std::endl;
    return 1;
  }
  // Tag the input + keys explicitly. libnbfhetch instantiates tag_input for a
  // single Ciphertext, so re-serialize the single input to a sidecar and tag
  // that (its path is re-read at replay). tag_keys packs relin + rotation keys.
  {
    auto single = prms.ctxtupdir() / "cipher_input_single.bin";
    if (!Serial::SerializeToFile(single, ctxt_vec[0], SerType::BINARY)) {
      std::cerr << "[ERROR] cannot write " << single << std::endl;
      return 1;
    }
    niobium::compiler().tag_input("cipher_input", ctxt_vec[0], single.string());
  }
  niobium::compiler().tag_keys(cc);

  const bool replaying = niobium::compiler().is_cache_valid();

  // Record the trace once (cache miss), in hollow mode (skips the heavy math;
  // the value left in ctxtResult is not real — the replay below produces the
  // real result). A cache hit runs no FHE ops.
  Ciphertext<DCRTPoly> out;
  if (!replaying) {
    std::cout << "[server-sdk] recording trace (first run) ..." << std::endl;
    niobium::compiler().start();
    niobium::compiler().enable_hollow_mode(true);
    auto _t = now_ms();
    auto ctxtResults =
        mnist(cc, fc1_weight, fc1_bias, fc2_weight, fc2_bias, {ctxt_vec[0]});
    emit_timing("record_ms", elapsed_ms(_t));
    niobium::compiler().enable_hollow_mode(false);  // must be OFF for probe/stop
    niobium::compiler().probe("ctxtResult", ctxtResults[0]);
    niobium::compiler().stop();
  }

  // Always replay to get the correct result: hollow recording computed nothing
  // usable and a cache hit ran no FHE. replay() dispatches to --target (local
  // in-process sim, or a backend); result() reconstructs the output ciphertext.
  std::cout << "[server-sdk] replaying (target from --target) ..." << std::endl;
  auto _t = now_ms();
  if (!niobium::compiler().replay()) {
    std::cerr << "[ERROR] replay failed!" << std::endl;
    return 1;
  }
  if (!niobium::compiler().result(cc, "ctxtResult", out) || !out) {
    std::cerr << "[ERROR] result reconstruction failed!" << std::endl;
    return 1;
  }
  emit_timing("replay_ms", elapsed_ms(_t));

  std::vector<CiphertextT> ctxtResults = {out};
  if (!Serial::SerializeToFile(result_ctxt_path, ctxtResults, SerType::BINARY)) {
    std::cerr << "[ERROR] cannot write " << result_ctxt_path << std::endl;
    return 1;
  }
  std::cout << "[server-sdk] replay done -> " << result_ctxt_path << std::endl;
  return 0;
}