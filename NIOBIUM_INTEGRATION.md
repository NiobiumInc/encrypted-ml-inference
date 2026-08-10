# Niobium integration: ML inference (MNIST MLP)

This example runs the server-side FHE computation (a matmul → ReLU → matmul
MNIST classifier, CKKS, BSGS rotations) on Niobium hardware over the public
**niobium-client FHETCH transport**. Everything below is about the *server
compute* stage; key generation, encryption and decryption are ordinary
client-side OpenFHE.

## Recording and replay

The classifier is recorded once and then replayed on the accelerator. Recording
captures the exact sequence of FHE operations (an FHETCH instruction trace);
replay executes that trace on hardware with fresh input data, with no
re-recording per input. One recording therefore serves an unbounded number of
inputs.

`server_encrypted_compute_sdk` links the public SDK `libnbfhetch` (the vendored
`niobium-client` submodule), builds its own OpenFHE, and reaches hardware over
the network, including the Fog "jobs-as-a-service" platform. Replay runs
in-process (the `local` simulator) or is shipped over HTTP to a backend,
selected by `--target`: `FOG` runs on Niobium's stable FPGA alias (the server
resolves it to the currently pinned hardware, so clients never depend on
internal device names); any other backend id is forwarded verbatim to the
transport.

## The record/replay pattern

`server_encrypted_compute_sdk` records once, then ALWAYS replays, in one process:

1. `init(argc, argv)` consumes `--target` / `--opt-level`. The target selects
   the hardware data format (there is no `--niobium_hw` flag).
2. `cache_parameters({"wl": <size>})` + `set_program_info(...)`. The cache key
   is the instance size only, so the trace is recorded once and every batch
   replays against it.
3. Load the crypto context and deserialize **both** eval keys into it: `mk.bin`
   (relin / EvalMult) and `rk.bin` (rotation / EvalAutomorphism). The BSGS matmul
   rotates, so the rotation keys must be present or replay returns NaN.
4. `capture_crypto_context(cc)` → `tag_input("cipher_input", ct, path)` →
   `tag_keys(cc)` (packs both key types).
5. **Cache miss:** `start()` → `enable_hollow_mode(true)` → `mnist(...)` →
   `enable_hollow_mode(false)` → `probe("ctxtResult", …)` → `stop()`. Recording is
   **hollow**: the expensive FHE math is skipped, so it is fast but the recorded
   value is not a real result. **Cache hit:** run no FHE ops.
6. **Always** `replay()` → `result(cc, "ctxtResult", ct)` → write
   `cipher_result_<b>.bin`. Replay is the only path that yields a correct result.
   The **expected** reference for the comparison comes from a separate
   `--cpu-only` full-math pass.

## Building

```bash
make build-sdk          # = bash scripts/build_task.sh
```

This inits the `niobium-client` submodule, builds its bundled OpenFHE +
`libnbfhetch` + the FHETCH transport (`make -C niobium-client release`), then
builds the ML-inference stages against it into `build/`. No compiler checkout
required. The first build compiles OpenFHE from source; later builds are
incremental.

## Running it

```bash
# LOCAL: no accelerator, no credentials (in-process simulator). Start here.
python3 harness/run_submission.py --profile single --target local

# Real FPGA over the Fog (needs a Fog account: python3 niobium-client/scripts/fog login)
python3 harness/run_submission.py --profile single --target FOG
```

`run_submission.py` does, per batch: stage the input → compute the **expected**
CPU reference (`--cpu-only`) → record (once) + replay on the target → decrypt →
compare (expected-vs-got) → aggregate into `runs/<id>/`.

- **`--target local`** (default): in-process simulator, no backend/credentials;
  record + replay in one process.
- **`--target FOG`**: one `fog submit` per batch → real FPGA on the Fog (`fog
  login` first; on macOS `pip3 install certifi`). Replay is serial.
- **`NBCC_FHETCH_SERVER=<url>`**: your own transport server (advanced).

The transport is stateless: each replay re-ships keys + trace + input (the
rotation keys alone are ~2 GB), so the Fog path is upload-bound; validate scale
on a co-located host, not a laptop.

## Run artifact + report

The harness writes a canonical artifact and never plots:

```
runs/<id>/
  run.json                # manifest: profile, target, opt_level, fog, num_batches,
                          #   passed, per-batch {replay_ms, labels, match, rel_error}
  expected_batch<b>.csv   # EXPECTED: CPU plaintext-FHE logits (decrypted before replay)
  scores_batch<b>.csv     # GOT: replay (local/FPGA/Fog) logits (class,score)
  batch<b>.log            # per-batch log
```

After decryption it prints an expected-vs-got summary (per batch: expected label,
returned label, match, max relative error, encrypted-computation elapsed), and writes the CSVs +
`run.json` above for programmatic use.
