#!/usr/bin/env python3
"""Deploy the ComputeMesh Intelligent Contract with the supported GenLayer SDK."""

from __future__ import annotations

import argparse
import os
from pathlib import Path

from dotenv import load_dotenv
from genlayer_py import create_account, create_client
from genlayer_py.chains import localnet, studionet, testnet_asimov, testnet_bradbury
from genlayer_py.types import TransactionStatus


CHAINS = {
    "localnet": localnet,
    "studionet": studionet,
    "testnet_asimov": testnet_asimov,
    "testnet_bradbury": testnet_bradbury,
}


def required(name: str) -> str:
    value = os.getenv(name, "").strip()
    if not value:
        raise SystemExit(f"Missing required environment variable: {name}")
    return value


def main() -> int:
    repository_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--contract",
        default=str(repository_root / "integrations/genlayer/contracts/computemesh_verifier.py"),
        help="Intelligent Contract source file",
    )
    args = parser.parse_args()

    load_dotenv(repository_root / ".env", override=False)
    network = os.getenv("GENLAYER_NETWORK", "localnet").strip().lower()
    if network not in CHAINS:
        choices = ", ".join(sorted(CHAINS))
        raise SystemExit(f"GENLAYER_NETWORK must be one of: {choices}")

    rpc_url = required("GENLAYER_RPC_URL")
    private_key = required("GENLAYER_PRIVATE_KEY")
    if not private_key.startswith("0x"):
        raise SystemExit("GENLAYER_PRIVATE_KEY must start with 0x")

    source = Path(args.contract).expanduser()
    if not source.is_absolute():
        source = repository_root / source
    if not source.is_file():
        raise SystemExit(f"Contract source does not exist: {source}")

    account = create_account(private_key)
    client = create_client(chain=CHAINS[network], endpoint=rpc_url, account=account)
    transaction_hash = client.deploy_contract(source.read_text(encoding="utf-8"))
    receipt = client.wait_for_transaction_receipt(
        transaction_hash,
        status=TransactionStatus.ACCEPTED,
    )

    # Do not print private keys or the complete account object.
    print(f"deployer={account.address}")
    print(f"network={network}")
    print(f"transaction_hash={transaction_hash.hex()}")
    print(f"status={receipt.status}")
    print("Save the deployed Intelligent Contract address from the GenLayer receipt.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
