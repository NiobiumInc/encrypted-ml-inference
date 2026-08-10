# FHE Benchmarking Suite - ML Inference Workload

Computer-vision models classify medical scans, scanned documents, and faces.
This workload runs that classification without decrypting the image: the client
encrypts the pixels, a Niobium accelerator runs the neural network on the
ciphertext under fully homomorphic encryption (FHE), and only the client opens
the label. It uses MNIST, the digit-recognition task that produced the first
modern convolutional network, as the stand-in.

This repository contains a reference implementation of the ML-Inference (MNIST
MLP) workload of the FHE benchmarking suite of
[HomomorphicEncryption.org](https://www.HomomorphicEncryption.org), running the
encrypted server compute on Niobium hardware over the public **niobium-client
FHETCH transport**, with a CPU reference and an FPGA path, including the Fog
"jobs-as-a-service" platform. See **[NIOBIUM_INTEGRATION.md](NIOBIUM_INTEGRATION.md)** for the full
record/replay design and the run-artifact/report contract.

## Prerequisites

- **Python 3.10+**, a **C++17 toolchain + CMake ≥ 3.18**, and **git**.
- Python packages (harness + MNIST data-prep): `numpy`, `torch`, `torchvision`;
  see [`requirements.txt`](requirements.txt).
- The first `make build-sdk` compiles OpenFHE from source (a one-time cost);
  later builds are incremental.
- For the **Fog** (real FPGA) run only: a Niobium Fog account
  (`python3 niobium-client/scripts/fog login`); on macOS also `pip install certifi`.

## Setup

```console
# clone WITH the niobium-client submodule
git clone --recurse-submodules <this-repo> && cd ml-inference-fhe
# (already cloned without it? run: git submodule update --init niobium-client)

# Python deps in a virtualenv (creates bmenv/, installs requirements.txt)
make setup-venv && source bmenv/bin/activate

# build the client SDK + OpenFHE + the ML-inference stages
make build-sdk                       # = bash scripts/build_task.sh
```

The `niobium-client` submodule is configured over **HTTPS**, so it clones
anonymously, with no GitHub account or SSH key required. If you push to these repos
over SSH, there is no need to edit `.gitmodules`; tell git once to rewrite the
URLs for all your repositories:

```console
git config --global url."git@github.com:".insteadOf "https://github.com/"
```

## Run

```console
# LOCALLY: no accelerator, no credentials. Records the trace and replays it in
# an in-process simulator, then verifies the FHE result vs a CPU reference.
python3 harness/run_submission.py --profile single --target local

# real FPGA over the Fog (needs a Fog account: `fog login`)
python3 harness/run_submission.py --profile single --target FOG
```

Profiles (all ring 2¹⁶, 128-bit, FPGA):

- **`single`**: 1 batch (smallest)
- **`small`**: 5 batches
- **`medium`**: 20 batches

`--target` selects the backend: `local` (default) runs in-process with no
backend; `FOG` ships to a real accelerator (setting `NBCC_FHETCH_SERVER=<url>`
to point at a transport server is the advanced alternative). Other flags:
`--batches N` (override the profile's batch count), `--seed`, `--skip-build`,
`--skip-data-prep`; run `python3 harness/run_submission.py -h` for the full
list.

After decryption the harness prints an **expected (CPU) vs got (target)** table
and writes a `runs/<id>/` artifact (see NIOBIUM_INTEGRATION.md):

```console
[harness] batch 0: Encrypted computation completed (elapsed: Xs)
[harness] batch 0: PASS (expected=8, got=8, max_rel_err=0.0)
[total latency] batch 0: Xs
...
[harness] === decryption summary — expected (CPU) vs got (local) ===
[harness] batch  expected   got  match  max_rel_err  enc_compute_s
[harness]     0         8     8    YES          0.0              X
[harness] single: 1/1 batch(es) PASS
[total latency] all batches: Xs (serial)
[harness] artifact: runs/single_local_<timestamp>/  (expected_batch*.csv + scores_batch*.csv + run.json)
```

`enc_compute_s` / "Encrypted computation … (elapsed: Xs)" is the client-observed
submit→result round-trip (transport + accelerator + download); `[total latency]`
is the whole-batch wall.

`match=YES` with `max_rel_err ≈ 0` means the accelerator result is bit-faithful
to the CPU FHE reference (this is hardware fidelity, not plaintext accuracy).

## Directory structure

```
ml-inference-fhe/
├─ README.md                # this file
├─ NIOBIUM_INTEGRATION.md   # record/replay design + run-artifact/report contract
├─ CONTRIBUTING.md
├─ LICENSE.md / NOTICE      # Apache-2.0 + attribution
├─ CMakeLists.txt / Makefile / requirements.txt
├─ harness/                 # Python driver + plaintext reference
│   ├─ run_submission.py       # keygen → encrypt → record-once → replay each batch → decrypt → compare
│   ├─ cleartext_impl.py       # plaintext MLP reference (cleartext, no FHE)
│   ├─ verify_result.py
│   ├─ params.py / utils.py
│   ├─ generate_dataset.py / generate_input.py
│   └─ mnist/                  # MNIST model + data helpers
├─ scripts/                 # build_task.sh (SDK build)
├─ submission/              # the FHE workload
│   ├─ src/ include/           # client_* / server_* stages + the MLP circuit
│   ├─ data/ docs/
│   └─ README.md
├─ niobium-client/          # public SDK submodule (libnbfhetch + FHETCH transport)
├─ datasets/                # created by the harness
├─ io/                      # runtime client↔server data (public_keys/, ciphertexts_upload|download/)
├─ runs/                    # per-run artifacts (run.json, expected_batch*.csv, scores_batch*.csv)
└─ measurements/            # performance logs (see measurements/README.md)
```

---

## Acknowledgments

This repository is a Niobium fork of the HomomorphicEncryption.org FHE
Benchmarking Suite's ML-inference workload
([`fhe-benchmarking/ml-inference`](https://github.com/fhe-benchmarking/ml-inference),
Apache-2.0). The MNIST-MLP reference and the original harness come from that
project; Niobium added the `submission/` HEIR-v2 implementation, the record/replay
instrumentation, and the FHETCH-transport path documented above. See `NOTICE` for
attribution and `submission/README.md` for the model details.

The upstream project also ships a CPU-only benchmark harness (a positional
`run_submission.py <instance-size>` driver). This repository instead takes
`--profile {single,small,medium}` (all ring 2^16) for the Niobium accelerator
path. For the upstream CPU flow, see the upstream repository.
