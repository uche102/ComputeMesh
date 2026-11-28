#!/usr/bin/env python3
"""Read-only inspection for the deployed ComputeMesh GenLayer contract.

This script intentionally imports and calls only read APIs. It never calls
write_contract, deploy_contract, or simulate_write_contract.
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

import requests
from dotenv import load_dotenv
from eth_account import Account
from genlayer_py import create_account, create_client
from genlayer_py.chains import studionet


ROOT = Path(__file__).resolve().parents[1]
CONTRACT = "0xdE8929d0a13CAFEEDe118D24b96DB84dB6331B43"


def required(name: str) -> str:
    value = os.getenv(name, "").strip()
    if not value:
        raise RuntimeError(f"Missing required configuration: {name}")
    return value


def account_from(name: str):
    value = os.getenv(name, "").strip()
    if not value:
        return None
    if not value.startswith("0x") or len(value) != 66:
        raise RuntimeError(f"{name} must be a 32-byte 0x-prefixed key")
    # The key is never printed or included in an exception.
    return create_account(value)


def safe_call(label: str, callback):
    try:
        return callback()
    except Exception as exc:  # Do not expose provider/calldata details.
        print(f"{label}: ERROR ({type(exc).__name__})")
        return None


def check_rpc_endpoint(endpoint: str) -> bool:
    """Confirm the configured JSON-RPC endpoint accepts POST JSON-RPC traffic."""
    response = requests.post(
        endpoint,
        json={"jsonrpc": "2.0", "id": 1, "method": "web3_clientVersion", "params": []},
        headers={"Content-Type": "application/json", "User-Agent": "computemesh-readonly-inspector"},
        timeout=15,
    )
    # A JSON-RPC method error still proves DNS, TLS, HTTP, and JSON-RPC reachability.
    if response.status_code != 200:
        raise RuntimeError(f"RPC endpoint returned HTTP {response.status_code}")
    response.json()
    return True


def main() -> int:
    stage = "environment loading"
    load_dotenv(ROOT / ".env", override=False)

    stage = "RPC endpoint configuration"
    print(f"inspection_stage={stage}")
    endpoint = required("GENLAYER_RPC_URL")
    print("config_endpoint=loaded")
    print("config_network=" + os.getenv("GENLAYER_NETWORK", "<missing>").strip().lower())
    print("config_wallet_a=" + ("configured" if os.getenv("GENLAYER_WALLET_A_PRIVATE_KEY", "").strip() else "missing"))
    print("config_wallet_b=" + ("configured" if os.getenv("GENLAYER_WALLET_B_PRIVATE_KEY", "").strip() else "missing"))
    if os.getenv("GENLAYER_NETWORK", "").strip().lower() != "studionet":
        raise RuntimeError("GENLAYER_NETWORK must be studionet for this inspection")

    stage = "circuit lookup configuration"
    print(f"inspection_stage={stage}")
    for name in ("COMPUTEMESH_PROCESSOR_HEX", "COMPUTEMESH_CIRCUIT_ID_HEX", "COMPUTEMESH_COMMITMENT_HEX"):
        print(f"config_{name.lower()}={'configured' if os.getenv(name, '').strip() else 'missing'}")
    raw_circuit = [os.getenv(name, "").strip() for name in (
        "COMPUTEMESH_PROCESSOR_HEX", "COMPUTEMESH_CIRCUIT_ID_HEX",
        "COMPUTEMESH_COMMITMENT_HEX",
    )]
    if any(raw_circuit) and not all(raw_circuit):
        raise RuntimeError("Circuit lookup requires all three COMPUTEMESH_* values")
    processor = raw_circuit[0].lower().removeprefix("0x")
    circuit_id = raw_circuit[1].lower().removeprefix("0x")
    commitment = raw_circuit[2].lower().removeprefix("0x")
    has_circuit = all(raw_circuit)
    if has_circuit and (len(processor) != 40 or len(circuit_id) != 16 or len(commitment) != 64):
        raise RuntimeError("Circuit identity or commitment has an invalid length")

    stage = "wallet configuration"
    print(f"inspection_stage={stage}")
    wallet_a = account_from("GENLAYER_WALLET_A_PRIVATE_KEY")
    wallet_b = account_from("GENLAYER_WALLET_B_PRIVATE_KEY")

    stage = "SDK client initialization"
    print(f"inspection_stage={stage}")
    # genlayer-py 0.16.3 requires an account on the client for gen_call, even
    # for view methods. This ephemeral account is never funded or used to sign
    # a transaction; configured wallets are used only for optional balances.
    read_only_account = Account.create()
    client = create_client(chain=studionet, endpoint=endpoint, account=read_only_account)
    print(f"network=studionet chain_id={client.chain.id}")
    print(f"endpoint_host={endpoint.split('://', 1)[-1].split('/', 1)[0]}")
    print(f"contract={CONTRACT}")
    print(f"wallet_a={wallet_a.address if wallet_a else 'not configured'}")
    print(f"wallet_b={wallet_b.address if wallet_b else 'not configured'}")

    stage = "RPC connectivity and balances"
    print(f"inspection_stage={stage}")
    endpoint_ok = safe_call("rpc_endpoint", lambda: check_rpc_endpoint(endpoint))
    if endpoint_ok:
        print("rpc_endpoint=REACHABLE")
    balance_a = safe_call("wallet_a_balance_wei", lambda: client.get_balance(wallet_a.address)) if wallet_a else None
    balance_b = safe_call("wallet_b_balance_wei", lambda: client.get_balance(wallet_b.address)) if wallet_b else None
    if balance_a is not None:
        print(f"wallet_a_balance_wei={balance_a}")
    if balance_b is not None:
        print(f"wallet_b_balance_wei={balance_b}")

    stage = "read-only contract calls via gen_call"
    print(f"inspection_stage={stage}")
    if not has_circuit:
        print("circuit_lookup=SKIPPED (no verified registered circuit identifiers available)")
        return 0
    read = lambda function, args: client.read_contract(CONTRACT, function, args=args)
    owner = safe_call(
        "get_registered_owner",
        lambda: read("get_registered_owner", [processor, circuit_id]),
    )
    registered = safe_call(
        "get_registered_commitment",
        lambda: read("get_registered_commitment", [processor, circuit_id]),
    )
    approval = safe_call(
        "get_approved_specification",
        lambda: read("get_approved_specification", [commitment]),
    )

    if owner is not None:
        print(f"registered_owner={owner}")
    if registered is not None:
        print(f"registered_commitment={registered}")
    if approval is not None:
        try:
            record = json.loads(approval) if approval else None
            if record is None:
                print("approved_specification=EMPTY")
            else:
                print(f"approved_specification_status={record.get('status', 'UNKNOWN')}")
                print(f"approved_specification_hash={record.get('specification_hash', '')}")
                print(f"approved_specification_bytes={len(approval.encode('utf-8'))}")
        except (TypeError, ValueError):
            print("approved_specification=NON_JSON_RESULT")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        # Keep diagnostics useful without printing exception text, calldata,
        # endpoint contents, or any credential-derived data.
        print(f"inspection_error={type(exc).__name__}: configuration or connectivity check failed", file=sys.stderr)
        raise SystemExit(2)
