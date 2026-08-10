# Measurements

`harness/run_submission.py` writes per-run diagnostics here — the local CPU
**record** log for each profile (`<profile>_record.log`). These are regenerated
every run and are git-ignored.

The canonical, portable **run artifact** for a transport run is written under
`runs/<id>/` (also git-ignored), not here. It contains, per batch:

- `expected_batch<b>.csv` — the plaintext-FHE (CPU) reference logits ("expected")
- `scores_batch<b>.csv` — the replay (FUNC_SIM / FPGA / Fog) logits ("got")
- `batch<b>.log` — the per-batch cpu-only + replay + decrypt log
- `run.json` — the manifest (profile, target, opt_level, per-batch
  `expected_label` / `got_label` / `match` / `rel_error` / `replay_ms`)

The harness prints an expected-vs-got summary after decryption and writes the
CSVs + `run.json` for programmatic use. See
[NIOBIUM_INTEGRATION.md](../NIOBIUM_INTEGRATION.md) for the full run-artifact
contract.