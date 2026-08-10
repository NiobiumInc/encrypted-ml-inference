# ML Inference FHE Submission

This directory contains the implementation for the ML inference FHE benchmarking
workload (a matmul → ReLU → matmul MNIST classifier, CKKS, BSGS rotations).

## Building

The executables are built from the repository root by the self-contained SDK /
FHETCH-transport build — there is no separate build inside `submission/`:

```bash
make build-sdk          # = bash scripts/build_task.sh  (from the repo root)
```

This builds the `niobium-client` SDK (its bundled OpenFHE + `libnbfhetch` + the
FHETCH transport) and the pipeline stages below into the top-level `build/`.
See [../NIOBIUM_INTEGRATION.md](../NIOBIUM_INTEGRATION.md).

## Executables

The following executables implement the benchmark pipeline stages:

- `client_key_generation` - Generate FHE keys and cryptographic context
- `client_preprocess_input` - Optional cleartext preprocessing of input
- `client_encode_encrypt_input` - Encode and encrypt input data
- `server_preprocess_model` - Optional preprocessing of the model
- `server_encrypted_compute_sdk` - Encrypted server computation over the FHETCH transport
- `client_decrypt_decode` - Decrypt and decode results
- `client_postprocess` - Optional cleartext postprocessing

## Security and parameters

The CKKS crypto context is defined in
[`src/mlp_encryption_utils.cpp`](src/mlp_encryption_utils.cpp)
(`mlp_generate_crypto_context_v2`) and is validated end-to-end on the Niobium
accelerator path — use these values as-is:

| Parameter | Value |
|-----------|-------|
| Scheme | CKKS |
| Ring dimension | 2¹⁶ (65536) |
| Security level | `HEStd_128_classic` (128-bit) |
| Multiplicative depth | 8 |
| Key switching | `HYBRID` |
| Scaling technique | OpenFHE default (`FLEXIBLEAUTOEXT`) |
| Secret-key distribution | OpenFHE default (uniform ternary) |
| Slot count | N/2 = 32768 (the MLP packs 1024) |

All profiles (`single` / `small` / `medium`) share this context. At N=65536 the
modulus budget (log(Q·P) ≈ 744 bits) sits well inside the 128-bit-classic limit.
Rotation keys are generated for the baby-step/giant-step matmul index set (see
`generate_mult_rot_key_v2`); the relin key via `EvalMultKeyGen`. Any parameter
not listed takes OpenFHE's default — see the [OpenFHE documentation](https://openfhe.org)
for the meaning of each knob.

> The upstream reference uses ring 2¹⁵; this fork uses 2¹⁶, so ciphertext/key
> sizes and timings are not directly comparable to upstream.

## Dependencies

- OpenFHE 1.4.2 (Niobium-instrumented) — bundled and built by the `niobium-client`
  SDK; no separate install step.
- The public `niobium-client` SDK (`libnbfhetch`) — vendored as a submodule; the
  server compute records an FHETCH trace and replays it over the transport
  (in-process simulator / FUNC_SIM / FPGA / Fog).

## For More Information

See the root [README.md](../README.md) and
[NIOBIUM_INTEGRATION.md](../NIOBIUM_INTEGRATION.md) for details on running the
benchmarks over the transport.