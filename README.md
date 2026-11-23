# ComputeMesh

ComputeMesh is a deterministic C++20 TAP-02 circuit execution engine with a
GenLayer verifier. Circuits are ordered NAND, LATCH, and REF records. Signals
0 and 1 are constants, followed by circuit inputs and then element outputs.
Outputs are the final declared signals. LATCH emits its previous state and
updates state after the whole circuit has evaluated; REF executes a registered
child circuit and lays out child state in element order.

The C++ engine parses and serializes canonical TAP-02 records, validates signal
references and resource limits, resolves nested references with cycle/depth
protection, computes SHA-256 circuit commitments, and produces execution
evidence. The GenLayer contract recomputes the commitment and execution rather
than trusting submitted outputs, including child commitments and state
transitions.

## Build and test

Dependencies: CMake 3.20+, a C++20 compiler, and OpenSSL development headers.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The differential test vectors are in
`tests/fixtures/differential_vectors.tsv`. The `computemesh` binary is the
engine entry point; library-style use is exposed through the headers in
`include/`.

## GenLayer verifier

`integrations/genlayer/contracts/computemesh_verifier.py` is written for the
GenLayer SDK version declared in its dependency header. The repository's
Python tests use GenLayer's `direct_deploy` fixture and therefore require the
GenLayer SDK/test harness supplied by the supported GenLayer tooling. Run them
from the repository root after installing that toolchain:

Create/use the project environment and run the complete verifier suite:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-genlayer.txt
python -m pytest -q integrations/genlayer
```

The repository's existing `.venv` already contains the tested GenLayer SDK
(`genlayer-py`), test plugin (`genlayer-test`), and pytest. Local tests use
GLSim/GenLayer test fixtures and default to `http://127.0.0.1:4000/api`.

## Frontend

The Vite/React frontend in `frontend/` uses the documented `genlayer-js`
browser-wallet client. It performs real `register_circuit` writes and
`verify_execution` reads against the configured Intelligent Contract; it does
not hold private keys. Configure it and build it with:

```sh
cd frontend
cp .env.example .env
# Set VITE_CONTRACT_ADDRESS to a deployed contract address.
npm install
npm run build
npm run dev
```

Use a wallet exposing EIP-1193 (`window.ethereum`) and select the same network
as `VITE_GENLAYER_NETWORK`. The current GenLayerJS release supports `localnet`,
`studionet`, `testnetAsimov`, and `testnetBradbury`.

## Deployment

The deployment helper uses the installed `genlayer-py` SDK and never prints the
private key. Put deployment-only values in an ignored `.env`:

```sh
GENLAYER_RPC_URL=http://127.0.0.1:4000/api
GENLAYER_PRIVATE_KEY=0x...
GENLAYER_NETWORK=localnet
```

Then run from the repository root:

```sh
. .venv/bin/activate
python scripts/deploy_genlayer.py
```

Supported network names are `localnet`, `studionet`, `testnet_asimov`, and
`testnet_bradbury`. The deployer becomes the contract administrator and must
be funded on the selected network. Save the deployed contract address from the
GenLayer receipt, place it in `frontend/.env` as `VITE_CONTRACT_ADDRESS`, and
rebuild the frontend. The deployment helper has not been run against a live
network in this repository because no wallet or RPC credentials are present.

The contract administrator is the deployment sender; only that address can
register child commitments. `scripts/deploy_genlayer.py` supplies the actual
SDK deployment path. Supply RPC URL, network, and private key through the
ignored environment file, never source code.

## Configuration and X Layer TapeOut integration

Copy `.env.example` to an ignored `.env` only if the selected deployment tool
loads dotenv files. Local C++ execution needs no credentials. The read-only
TapeOut integration in `include/xlayer_client.hpp` and
`include/xlayer_circuit_repository.hpp` retrieves existing processors and
circuits directly from X Layer. It validates chain ID 196, reads
`circuitInfo`, `netlist`, `eval`, and `step`, and supports pinned block reads.
The verified demonstration target is processor
`0x839bdD6fA7A66416A609A735E11De5411B98574E`, circuit `1` on X Layer Mainnet.
No X Layer private key is required for retrieval or differential verification.
The C++ client uses the system `curl` executable for JSON-RPC transport.

Run the live C++ comparison with network access:

```sh
export XLAYER_RPC_URL=https://rpc.xlayer.tech
./build/xlayer_differential
```

The test prints `SKIP` and exits successfully only when the RPC cannot be
reached before chain validation; it prints `PASS` only after comparing all four
on-chain `eval` results with the ComputeMesh evaluator. A reachable RPC with a
bad processor, malformed circuit, or output mismatch fails. X Layer is not used
for settlement or a custom registry.

Production deployment still requires a funded GenLayer wallet and the RPC/
network settings accepted by the supported GenLayer tooling. Network deployment
cannot be verified from this repository without those external credentials.

## AI Circuit Builder and GenLayer consensus

The frontend now includes a review-gated AI Circuit Builder. It provides
conservative starter proposals for supported TAP-02 NAND and XOR descriptions,
requests clarification for unsupported or ambiguous descriptions, and copies a
reviewed proposal into the existing deterministic verification form. The
proposal is not proof of correctness and is never executed without the existing
parser, validator, evaluator, commitment, and evidence checks.

The new `approve_specification(description, commitment, input_count,
output_count, netlist, draft)` Intelligent Contract method gives GenLayer a
substantive role. It first recomputes the exact circuit commitment and rejects
unsupported TAP-02 records. Its leader independently interprets the natural-language
description with `gl.nondet.exec_prompt`; validators independently repeat that
interpretation through `gl.vm.run_nondet_unsafe` and compare purpose, interface,
behaviour, ambiguity status, and expected test vectors. Only an accepted result
is stored in `approved_specifications`, keyed by the circuit commitment, and the
method returns the hash of the canonical approved specification. Approval
records are immutable. `verify_execution` requires the matching approved record,
rechecks its hash, and deterministically executes every approved test vector
before accepting the claimed execution. An ambiguous or unconsensual
interpretation does not authorize verification.

This method is prepared for a new deployment; the existing deployed contract is
not modified automatically. After deployment, set `VITE_CONTRACT_ADDRESS` to the
new address and rebuild the frontend. The live consensus path requires a
GenLayer network with configured nondeterministic execution and cannot be
proven by the local deterministic test suite alone.
