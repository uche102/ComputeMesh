import hashlib


def test_sha256_supported(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    # SHA-256 is part of the production circuit commitment path. This verifies
    # that the contract recomputes the commitment rather than exposing a
    # test-only hash helper.
    input_count = 2
    output_count = 1
    netlist_hex = "00000002000003"
    commitment = hashlib.sha256(
        b"ComputeMesh/TAP-02/Circuit/v1"
        + input_count.to_bytes(4, "big")
        + output_count.to_bytes(4, "big")
        + bytes.fromhex(netlist_hex)
    ).hexdigest()

    assert contract.verify_execution(
        input_count,
        output_count,
        netlist_hex,
        commitment,
        "",
        "01",
        "",
        "01",
        [],
        "00" * 20,
        "0000000000000001",
    )
