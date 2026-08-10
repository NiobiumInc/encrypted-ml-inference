#!/usr/bin/env python3
# Copyright 2025 Google LLC
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
#
# Modifications Copyright 2023-present Niobium Microsystems, Inc.
# Licensed under the Apache License, Version 2.0.

"""
run_submission.py — drive the ML-inference (MNIST MLP) workload over the
niobium-client FHETCH transport, FBS-submission style.

Flow:  (build) -> keygen(once) -> data-prep + encrypt(each batch) ->
       RECORD once (local CPU) -> REPLAY each batch -> decrypt/score each batch
       -> aggregate into a run artifact.

Batches: the batches are INDEPENDENT (same matmul->ReLU->matmul circuit,
different MNIST images, no cross-batch FHE reduce), so the trace is recorded
ONCE and each batch is replayed as a SELF-CONTAINED request. This matches the
niobium-client transport being STATELESS — the worker holds nothing between
requests, so every replay ships the whole trace + keys + that batch's input.

Profiles are the ring 2^16 (128-bit, FPGA) instance sizes:
  single (1 batch, smallest), small (5 batches), medium (20 batches);
  all ring 2^16 (128-bit, FPGA).
All profiles are ring 2^16 (128-bit) — the FPGA-real sizes.

Backends (`--target`, the sole selector):
  local   in-process simulator (default; no server, no credentials)
  FOG     real FPGA via jobs-as-a-service (`niobium-client/scripts/fog submit`),
          or POSTed to your own server if NBCC_FHETCH_SERVER is set
  NBCC_FHETCH_SERVER=<url>   your own transport server (advanced)
  (record always runs locally on CPU, no backend needed)

  python3 harness/run_submission.py --profile single --target FOG
"""
import argparse
import json
import math
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent            # repo root
BUILD = ROOT / "build"                                    # scripts/build_task.sh output
CLIENT = ROOT / "niobium-client"
HARNESS = ROOT / "harness"
FOG = CLIENT / "scripts" / "fog"

# Profile name -> instance size id (params.h / harness/params.py). All sizes are
# ring 2^16 (128-bit, FPGA).
PROFILE_SIZE = {"single": 0, "small": 1, "medium": 2}
# Batch count per size (mirrors params.h batchSizes).
BATCH_SIZE = [1, 5, 20]


def instance_name(size: int) -> str:
    return ["single", "small", "medium"][size]


def io_dir(size: int) -> Path:
    return ROOT / "io" / instance_name(size)


def program_dir_glob(size: int):
    # server_encrypted_compute_sdk: cache_parameters {"wl": size} ->
    # program dir "ml-inference_wl_<size>".
    return list(ROOT.glob(f"ml-inference_wl_{size}*"))


def lib_env() -> dict:
    """Runtime env for the stage binaries: the client's bundled OpenFHE +
    libnbfhetch. Honors a caller-supplied DYLD/LD path (used by the reuse-a-
    prebuilt-OpenFHE flow). Points NBCC_FHETCH_REPLAY at the client forwarder
    when a transport server is configured."""
    env = os.environ.copy()
    libs = [CLIENT / "vendor" / "lib" / "openfhe" / "lib"]
    for d in (CLIENT / "build" / "vendor" / "niobium-fhetch",
              CLIENT / "build" / "_deps" / "niobium-fhetch-build"):
        if d.exists():
            libs.append(d)
    libp = os.pathsep.join(str(x) for x in libs)
    for var in ("LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
        env[var] = libp + (os.pathsep + env[var] if env.get(var) else "")
    env.setdefault("PYTHONPATH", str(HARNESS))
    fwd = CLIENT / "build" / "src" / "fhetch_transport" / "nbcc_fhetch_replay"
    if fwd.exists() and not env.get("NBCC_FHETCH_REPLAY_BIN"):
        env["NBCC_FHETCH_REPLAY_BIN"] = str(fwd)
    if env.get("NBCC_FHETCH_SERVER") and not env.get("NBCC_FHETCH_REPLAY"):
        env["NBCC_FHETCH_REPLAY"] = str(fwd)
    return env


def sh(cmd, env=None, capture=False, cwd=None):
    return subprocess.run([str(c) for c in cmd], cwd=str(cwd or ROOT),
                          check=not capture, capture_output=capture, text=True, env=env)


def parse_output(text: str) -> dict:
    out = {}
    for key, pat in (("compute_ms", r"\[TIMING\] compute_ms:\s*(\d+)"),
                     ("replay_ms", r"\[TIMING\] replay_ms:\s*(\d+)"),
                     ("fpga_ms", r"fpga=(\d+)ms")):
        m = re.search(pat, text)
        if m:
            out[key] = int(m.group(1))
    return out


def read_scores(csv_path: Path):
    """Read a scores CSV (class,score) -> (list[float], argmax_label)."""
    if not csv_path.exists():
        return None, None
    vals = []
    for line in csv_path.read_text().splitlines()[1:]:
        parts = line.split(",")
        if len(parts) == 2:
            vals.append(float(parts[1]))
    if not vals:
        return None, None
    label = max(range(len(vals)), key=lambda i: vals[i])
    return vals, label


def rel_error(a, b) -> float:
    """Max relative error between two logit vectors (denominator = max|ref|)."""
    if not a or not b or len(a) != len(b):
        return float("nan")
    denom = max(abs(x) for x in a) or 1.0
    return max(abs(x - y) for x, y in zip(a, b)) / denom


# ---------------------------------------------------------------------------

def data_prep(size: int, seed, env):
    """Client-side: generate MNIST inputs, preprocess, and encrypt every batch
    to io/<name>/ciphertexts_upload/cipher_input_<b>.bin. Uses the repo's real
    data-prep scripts. If those aren't runnable (no torchvision / offline), a
    synthetic single-image input can be supplied by writing test_pixels.txt."""
    datadir = ROOT / "datasets" / instance_name(size)
    datadir.mkdir(parents=True, exist_ok=True)
    # 1) sample MNIST (writes datasets/<name>/... + test_pixels/labels)
    gi = [sys.executable, HARNESS / "generate_input.py", str(size)]
    if seed is not None:
        gi += ["--seed", str(seed)]
    sh([sys.executable, HARNESS / "generate_dataset.py", str(datadir / "dataset.txt")], env=env)
    sh(gi, env=env)
    # 2) preprocess + encrypt
    sh([BUILD / "client_preprocess_input", str(size)], env=env)
    sh([BUILD / "client_encode_encrypt_input", str(size)], env=env)


def stage_input(size: int, b: int):
    """Stage batch b's ciphertext to the stable path the compute reads
    (cipher_input.bin), so each batch feeds the same recorded trace."""
    import shutil
    up = io_dir(size) / "ciphertexts_upload"
    shutil.copyfile(up / f"cipher_input_{b}.bin", up / "cipher_input.bin")


def fhetch_sim_bin():
    """The client's in-tree fhetch_sim worker (used by --target local). Built by
    scripts/build_task.sh; its path depends on the client's OpenFHE layout
    (vendored vs FetchContent), so check both."""
    for d in ("build/vendor/niobium-fhetch", "build/_deps/niobium-fhetch-build"):
        p = CLIENT / d / "fhetch_sim"
        if p.exists():
            return p
    return None


def compute_cmd(size, b, args):
    base = [str(size), f"--batch_id={b}", "--target", args.target,
            "--opt-level", args.optimization]
    if args.fog:                       # Fog jobs-as-a-service (set in main for --target FOG)
        return [FOG, "submit", BUILD / "server_encrypted_compute_sdk", *base]
    return [BUILD / "server_encrypted_compute_sdk", *base]


def run_compute(size, b, args, env):
    """Run the compute for one batch: records the trace on the first call (cache
    miss) and ALWAYS replays on the --target backend to produce the result.
    Retries transient transport failures; removes the result first so a FAILED
    run can't leave decrypt reading a stale ciphertext.

    For --target local the replay is in-process and uses the in-memory capture,
    so record + replay must happen in the SAME process — the caller clears the
    trace per batch so each batch is a fresh single-process record+replay."""
    result_ct = io_dir(size) / "ciphertexts_download" / f"cipher_result_{b}.bin"
    cmd = compute_cmd(size, b, args)
    t0 = time.time()
    cp = None
    for attempt in range(1, args.retries + 2):
        result_ct.unlink(missing_ok=True)
        cp = sh(cmd, env=env, capture=True)
        if cp.returncode == 0 and "replay done" in cp.stdout and result_ct.exists():
            break
        if attempt <= args.retries:
            print(f"[harness] batch {b}: attempt {attempt} failed, retrying ...")
    return cp, round(time.time() - t0, 2)


def decrypt_batch(size, b, env, scores_path):
    cp = sh([BUILD / "client_decrypt_decode", str(size), f"--batch_id={b}",
             f"--save-scores={scores_path}"], env=env, capture=True)
    return cp


def run_cpu_expected(size, b, env):
    """Compute the plaintext-FHE reference ("expected") for one batch on the CPU
    (no transport, no recording). Reads the staged cipher_input.bin, writes
    cipher_result_<b>.bin, which the caller decrypts BEFORE the compute run
    overwrites it."""
    e = dict(env)
    e.pop("NBCC_FHETCH_SERVER", None)          # pure local CPU compute
    return sh([BUILD / "server_encrypted_compute_sdk", str(size),
               f"--batch_id={b}", "--cpu-only"], env=e, capture=True)


def main() -> int:
    p = argparse.ArgumentParser(description="Run ML-inference over the FHETCH transport.")
    p.add_argument("--profile", default="single",
                   choices=list(PROFILE_SIZE),
                   help="Instance profile: single (1 batch), small (5), medium (20); "
                        "all ring 2^16 FPGA. Default single.")
    p.add_argument("--target", default="local",
                   help="Backend: local (default; in-process simulator, no backend or "
                        "credentials) or FOG (real FPGA via jobs-as-a-service). Other "
                        "values are passed through to a transport server (advanced).")
    p.add_argument("-O", "--opt-level", dest="optimization",
                   choices=["O0", "O1", "O2", "O3"], default="O3",
                   help="Replay opt level (default O3).")
    # Deprecated alias for `--target FOG`; still works but hidden.
    p.add_argument("--fog", action="store_true", help=argparse.SUPPRESS)
    p.add_argument("--batches", type=int, default=None,
                   help="Cap the number of batches. Default: the profile's batch count.")
    p.add_argument("--retries", type=int, default=2,
                   help="Per-batch replay retries on transient transport failure (default 2).")
    p.add_argument("--jobs", type=int, default=1,
                   help="Reserved. Only serial replay is supported: concurrent batches "
                        "would share one program dir (serialized_probes) and race. "
                        ">1 currently warns and falls back to serial.")
    p.add_argument("--seed", type=int, default=None, help="MNIST sampling seed.")
    p.add_argument("--skip-build", action="store_true",
                   help="Skip scripts/build_task.sh (assume build/ is present).")
    p.add_argument("--skip-data-prep", action="store_true",
                   help="Assume io/<name>/ciphertexts_upload is already populated.")
    args = p.parse_args()
    if args.fog:                       # deprecated alias -> --target FOG
        args.target = "FOG"

    if not args.skip_build and not (BUILD / "server_encrypted_compute_sdk").exists():
        print("[harness] building (scripts/build_task.sh) ...")
        subprocess.run(["bash", str(ROOT / "scripts" / "build_task.sh")], check=True)

    size = PROFILE_SIZE[args.profile]
    n = BATCH_SIZE[size]
    if args.batches is not None:
        n = min(n, args.batches)
    env = lib_env()
    # --target is the sole backend selector. Dispatch (matches the sibling apps):
    #   local  -> the client's in-tree fhetch_sim worker (NBCC_FHETCH_SIM), in-process.
    #   FOG    -> jobs-as-a-service (scripts/fog submit) UNLESS NBCC_FHETCH_SERVER
    #             points at your own replay server; --fog forces the jobs path.
    #   other  -> a running nbcc_fhetch_replay_server (NBCC_FHETCH_SERVER).
    fog_jobs = (args.target == "FOG") and (args.fog or not env.get("NBCC_FHETCH_SERVER"))
    args.fog = fog_jobs      # downstream (compute_cmd, run.json) keys off this
    if args.target == "local":
        sim = fhetch_sim_bin()
        if sim is None:
            print("[harness] ERROR: --target local needs the fhetch_sim worker — "
                  "build the client first (scripts/build_task.sh / make build-sdk).")
            return 2
        env["NBCC_FHETCH_SIM"] = str(sim)
    elif not fog_jobs and not env.get("NBCC_FHETCH_SERVER"):
        print("[harness] ERROR: target '%s' needs a transport server: set "
              "NBCC_FHETCH_SERVER to a running nbcc_fhetch_replay_server, or use "
              "--target FOG (jobs-as-a-service), or --target local (in-process)."
              % args.target)
        return 2
    is_local = args.target == "local"
    print(f"\n[harness] === {args.profile} (size {size}): {n} batch(es), "
          f"target={args.target}, opt={args.optimization}, fog_jobs={fog_jobs} ===")

    # 1) keygen once (cc + pk/sk + relin mk.bin + rotation rk.bin)
    if not (io_dir(size) / "public_keys" / "cc.bin").exists():
        sh([BUILD / "client_key_generation", str(size)], env=env)
    sh([BUILD / "server_preprocess_model"], env=env)

    # 2) data-prep + encrypt every batch
    if not args.skip_data_prep:
        data_prep(size, args.seed, env)

    # 3) Per batch: record-once + replay + decrypt into a canonical run artifact
    #    (runs/<id>/). The artifact — scores_batch*.csv + run.json — is the
    #    contract consumed by the report/plot tools; the runner never plots.
    run_dir = ROOT / "runs" / f"{args.profile}_{args.target}_{time.strftime('%Y%m%d_%H%M%S')}"
    run_dir.mkdir(parents=True, exist_ok=True)

    def process_batch(b):
        stage_input(size, b)  # feed this batch to the stable input path

        # 1) EXPECTED — the plaintext-FHE reference for THIS batch, computed on
        #    the CPU, decrypted before the compute run overwrites the result.
        exp_scores = exp_label = None
        ecp = run_cpu_expected(size, b, env)
        if ecp.returncode == 0:
            ed = decrypt_batch(size, b, env, run_dir / f"expected_batch{b}.csv")
            if ed.returncode == 0:
                exp_scores, exp_label = read_scores(run_dir / f"expected_batch{b}.csv")

        # 2) GOT — record (once) + replay on --target. For local the replay is
        #    in-process (in-memory capture), so clear the trace to force a fresh
        #    single-process record+replay for each batch. For a backend target
        #    the trace is recorded on batch 0 and reused (cross-process replay).
        if is_local:
            import shutil
            for d in program_dir_glob(size):
                shutil.rmtree(d, ignore_errors=True)
        stage_input(size, b)  # cpu-only overwrote nothing, but re-stage to be safe
        cp, wall = run_compute(size, b, args, env)
        got_csv = run_dir / f"scores_batch{b}.csv"
        dp = decrypt_batch(size, b, env, got_csv)
        log = ((ecp.stdout + ecp.stderr + "\n") + cp.stdout + cp.stderr + "\n"
               + dp.stdout + dp.stderr)
        (run_dir / f"batch{b}.log").write_text(log)
        got_scores, got_label = read_scores(got_csv)
        finite = bool(got_scores) and all(math.isfinite(x) for x in got_scores)
        replay_ok = (cp.returncode == 0 and "replay done" in cp.stdout)
        err = (rel_error(exp_scores, got_scores)
               if (exp_scores and got_scores) else float("nan"))
        match = (exp_label is not None and exp_label == got_label)
        m = parse_output(log)
        m.update(batch=b, wall_s=wall,
                 expected_label=exp_label, got_label=got_label, match=match,
                 rel_error=(round(err, 6) if err == err else None),
                 scores_csv=(got_csv.name if got_csv.exists() else None))
        m["passed"] = replay_ok and finite and match and (err == err and err < 0.10)
        m["total_latency_ms"] = int(wall * 1000)          # shared key with FBS results JSON
        verdict = "PASS" if m["passed"] else "FAIL"
        tail = "" if m["passed"] else "\n" + "\n".join(log.strip().splitlines()[-20:])
        # FBS-style timing: the encrypted computation elapsed (client-observed
        # submit->result round-trip: transport + accelerator + download), then
        # the verdict, then the per-batch [total latency] (whole-batch wall).
        comp_ms = m.get("replay_ms")
        comp = f"{comp_ms / 1000:.1f}s" if comp_ms is not None else "-"
        print(f"[harness] batch {b}: Encrypted computation completed (elapsed: {comp})")
        print(f"[harness] batch {b}: {verdict} (expected={exp_label}, got={got_label}, "
              f"max_rel_err={m['rel_error']}){tail}")
        print(f"[total latency] batch {b}: {wall}s")
        return m

    if args.jobs > 1:
        print(f"[harness] WARNING: --jobs {args.jobs} is not supported — concurrent "
              "replays share the program dir (serialized_probes) and would race. "
              "Falling back to serial.")
    # Serial only: each batch replays into the shared program dir, so batches must
    # not overlap. (Per-batch working-dir isolation is a future addition.)
    t_replay = time.time()
    results = [process_batch(b) for b in range(n)]
    replay_wall = round(time.time() - t_replay, 1)
    results = sorted(results, key=lambda r: r["batch"])

    nok = sum(1 for r in results if r["passed"])
    manifest = {
        "profile": args.profile, "size": size, "target": args.target,
        "opt_level": args.optimization, "fog": args.fog, "jobs": 1,
        "num_batches": len(results), "passed": nok, "replay_wall_s": replay_wall,
        "total_latency_ms": int(replay_wall * 1000),      # shared key with FBS results JSON
        "created": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "batches": results,
    }
    (run_dir / "run.json").write_text(json.dumps(manifest, indent=2))

    # --- Decryption summary: expected (CPU plaintext-FHE) vs got (target).
    # enc_compute_s = encrypted computation elapsed (submit->result round-trip). ---
    print(f"\n[harness] === decryption summary — expected (CPU) vs got ({args.target}) ===")
    print(f"[harness] {'batch':>5} {'expected':>9} {'got':>5} {'match':>6} "
          f"{'max_rel_err':>12} {'enc_compute_s':>14}")
    for r in results:
        cs = r.get('replay_ms')
        cs = f"{cs / 1000:.1f}" if cs is not None else "-"
        print(f"[harness] {r['batch']:>5} {str(r.get('expected_label')):>9} "
              f"{str(r.get('got_label')):>5} {('YES' if r.get('match') else 'NO'):>6} "
              f"{str(r.get('rel_error')):>12} {cs:>14}")

    rel = run_dir.relative_to(ROOT)
    print(f"\n[harness] {args.profile}: {nok}/{len(results)} batch(es) PASS")
    print(f"[total latency] all batches: {replay_wall}s (serial)")
    print(f"[harness] artifact: {rel}/  (expected_batch*.csv + scores_batch*.csv + run.json)")
    return 0 if nok == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
