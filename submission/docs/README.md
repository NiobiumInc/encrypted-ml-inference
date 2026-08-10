## Workload implementation — ML inference

The submission is built with the [HEIR](https://heir.dev) compiler.

## Model architecture
An MNIST digit classifier (encrypted inference over CKKS). The server holds the
trained model; the client sends an encrypted 28×28 image and receives the
encrypted 10-class logits.
- FC1: 784 → 512   (input is the flattened 28×28 image)
- Activation: Approx-ReLU (polynomial approximation)
- FC2: 512 → 10    (one score per digit class)

The input is padded to the ciphertext slot count; the output is post-processed
(argmax) to the predicted label. Reference weights live in `submission/data/*.bin`
and are loaded at runtime; the `harness/mnist` package can retrain/evaluate the
plaintext model.

## Compilation details
HEIR compiles the encrypted function with:
- Halevi–Shoup (baby-step/giant-step) matrix–vector products for the FC layers.
- Approximate sign / approximate ReLU, as specified in `docs/mlp.mlir`.

See further HEIR compilation details: https://github.com/google/heir/issues/1232

## Build details
Built from source by `scripts/build_task.sh` (`make build-sdk`): the
`niobium-client` SDK builds its bundled OpenFHE + `libnbfhetch`, then the
ML-inference stages compile against it (~30–60 min the first time; incremental
afterwards). Weights are read from `submission/data/*.bin` at runtime.