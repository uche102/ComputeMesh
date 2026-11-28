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

The current Studionet frontend deployment is configured for verifier
`0x3Dc90F4fEd216d0156c2B7c5608ae848DaA94AE9`. Its administrator is the
deployment wallet `0x182AA06f65C5a2b17d0DCB32a5C48Ba7AB5A7ece`, as confirmed by
the deployer. Circuit registration, specification approval, and incompatible
approval recovery require that wallet. Existing registrations and approvals on
the previous contract address are not present on this deployment and must be
re-established there by the administrator.

The contract administrator is the deployment sender, but routine registration
and specification approval are permissionless at the contract level. Each
processor/circuit-ID pair is registered once and stores the registering wallet
as its owner. Approval requires that owner, the exact registered commitment,
and the same circuit identity, so an unrelated wallet cannot occupy the
circuit's approval slot.
Approval also recomputes the commitment and deterministically executes every
submitted direct NAND/LATCH vector before storing the immutable record. REF
vectors are rejected at approval time because their child definitions are not
part of the approval call; REF execution remains supported by `verify_execution`
after registered child records are supplied.

If a legacy deployment contains an incompatible approval, the administrator may
call `recover_incompatible_approval(commitment, input_count, output_count,
netlist, expected_specification_hash)`. The contract recomputes the commitment,
checks the exact stored hash, and revokes the record only when its stored vectors
fail deterministic execution. Valid approvals cannot be revoked or overwritten;
the revoked record can then be replaced through the normal administrator-only
approval flow. The recovery method is not an unrestricted reset.

`scripts/deploy_genlayer.py` supplies the actual SDK deployment path. Supply RPC
URL and private key through the ignored environment file, never source code.

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

## Read-only inspection of the deployed verifier

`scripts/inspect_genlayer_contract.py` performs only RPC connectivity checks,
optional wallet balance reads, and `gen_call` reads against the configured
verifier. It does not import or call deployment, write, or simulation methods.
The installed `genlayer-py==0.16.3` client requires an account object even for
`gen_call`, so the script creates an unfunded ephemeral read-only sender in
memory. It never prints or requires a private key for public reads. Wallet
environment variables are optional and are used only for balance checks.

The repository contains local TAP-02 fixtures but no verified processor/circuit
identity registered on the deployed Studionet contract. When the three
`COMPUTEMESH_*` variables are absent, circuit lookup is reported as `SKIPPED`;
the script does not treat a skipped lookup as a pass or invent identifiers.

Run the inspection from a network-enabled Linux environment:

```sh
. .venv/bin/activate
export COMPUTEMESH_PROCESSOR_HEX=<registered processor, if known>
export COMPUTEMESH_CIRCUIT_ID_HEX=<registered 16-hex circuit id, if known>
export COMPUTEMESH_COMMITMENT_HEX=<matching 64-hex commitment, if known>
python scripts/inspect_genlayer_contract.py
```

Leave the three variables unset to validate Studionet connectivity without a
circuit lookup. Do not put wallet keys in tracked files. If balance checks are
needed, provide `GENLAYER_WALLET_A_PRIVATE_KEY` and optionally
`GENLAYER_WALLET_B_PRIVATE_KEY` in the process environment; the script prints
only derived public addresses and balances.

## AI Circuit Builder and GenLayer consensus

The frontend now includes a review-gated AI Circuit Builder. It provides
conservative starter proposals for supported TAP-02 NAND and XOR descriptions,
requests clarification for unsupported or ambiguous descriptions, and copies a
reviewed proposal into the existing deterministic verification form. The
proposal is not proof of correctness and is never executed without the existing
parser, validator, evaluator, commitment, and evidence checks.

The owner-scoped `approve_specification(processor, circuit_id, description,
commitment, input_count, output_count, netlist, draft)` Intelligent Contract method gives GenLayer a
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

The frontend passes the same SHA-256 commitment as both the circuit argument and
the draft's `circuit_commitment` field. A new deployment is required for these
authorization, vector-validation, and recovery rules; update
`VITE_CONTRACT_ADDRESS` only after recording and testing that new address. The
existing deployed contract and its approvals are not changed automatically.

This method is prepared for a new deployment; the existing deployed contract is
not modified automatically. Because storage schema and the approval ABI changed,
a new deployment (or an explicitly tested migration contract) is required; the
old deployment cannot be upgraded in place by this repository. Existing
registrations and approvals remain on the old address and must be migrated by a
deliberate owner/admin process before switching the frontend. After deployment,
set `VITE_CONTRACT_ADDRESS` to the new address and rebuild the frontend. The live consensus path requires a
GenLayer network with configured nondeterministic execution and cannot be
proven by the local deterministic test suite alone.
