import hashlib
import json


CONTRACT = "integrations/genlayer/contracts/computemesh_verifier.py"
NETLIST = "00000002000003"
PROCESSOR = "11" * 20
CIRCUIT_ID = "0000000000000001"


def commitment(input_count=2, output_count=1, netlist=NETLIST):
    return hashlib.sha256(
        b"ComputeMesh/TAP-02/Circuit/v1"
        + input_count.to_bytes(4, "big")
        + output_count.to_bytes(4, "big")
        + bytes.fromhex(netlist)
    ).hexdigest()


def seed_approval(contract, circuit_commitment, input_count=2, output_count=1, vectors=None):
    vectors = vectors or [{"input": "01", "output": "01"}]
    specification = {
        "purpose": "Return NAND",
        "inputs": [f"input_{i}" for i in range(input_count)],
        "outputs": [f"output_{i}" for i in range(output_count)],
        "behaviour": "result = NOT(a AND b)",
        "test_cases": vectors,
        "assumptions": ["inputs are binary"],
        "ambiguities": [],
        "ambiguous": False,
        "circuit_commitment": circuit_commitment,
    }
    canonical = json.dumps(specification, sort_keys=True, separators=(",", ":"))
    record = json.dumps({
        "specification": canonical,
        "specification_hash": hashlib.sha256(canonical.encode()).hexdigest(),
        "circuit_commitment": circuit_commitment,
        "status": "APPROVED",
        "version": 1,
        "input_count": input_count,
        "output_count": output_count,
        "test_vectors": specification["test_cases"],
        "processor_address": PROCESSOR,
        "circuit_id": CIRCUIT_ID,
        "owner": "",
    }, sort_keys=True, separators=(",", ":"))
    contract.approved_specifications[circuit_commitment] = record


def register(contract, commitment_value):
    assert contract.register_circuit(PROCESSOR, CIRCUIT_ID, commitment_value) is True


def test_verification_requires_specification_approval(direct_deploy):
    contract = direct_deploy(CONTRACT)
    assert contract.verify_execution(
        2, 1, NETLIST, commitment(), "", "01", "", "01", [], "00" * 20,
        "0000000000000001",
    ) is False


def test_approved_vectors_are_enforced(direct_deploy):
    contract = direct_deploy(CONTRACT)
    circuit_commitment = commitment()
    seed_approval(contract, circuit_commitment)

    assert contract.verify_execution(
        2, 1, NETLIST, circuit_commitment, "", "01", "", "01", [], "00" * 20,
        "0000000000000001",
    ) is True
    assert contract.verify_execution(
        2, 1, NETLIST, circuit_commitment, "", "01", "", "00", [], "00" * 20,
        "0000000000000001",
    ) is False


def test_tampered_approval_hash_is_rejected(direct_deploy):
    contract = direct_deploy(CONTRACT)
    circuit_commitment = commitment()
    seed_approval(contract, circuit_commitment)
    record = json.loads(contract.approved_specifications[circuit_commitment])
    record["specification_hash"] = "00" * 32
    contract.approved_specifications[circuit_commitment] = json.dumps(record)
    assert contract.verify_execution(
        2, 1, NETLIST, circuit_commitment, "", "01", "", "01", [], "00" * 20,
        "0000000000000001",
    ) is False


def test_consensus_approval_persists_immutable_record(direct_vm, direct_deploy):
    contract = direct_deploy(CONTRACT)
    circuit_commitment = commitment()
    register(contract, circuit_commitment)
    specification = {
        "purpose": "Return NAND",
        "inputs": ["a", "b"],
        "outputs": ["result"],
        "behaviour": "result = NOT(a AND b)",
        "test_cases": [{"input": "01", "output": "01"}],
        "assumptions": ["inputs are binary"],
        "ambiguities": [],
        "ambiguous": False,
        "circuit_commitment": circuit_commitment,
    }
    direct_vm.mock_llm("ComputeMesh", json.dumps(specification))
    result = contract.approve_specification(
        PROCESSOR, CIRCUIT_ID, "Return one when two inputs are not both one.",
        circuit_commitment,
        2,
        1,
        NETLIST,
        json.dumps(specification),
    )
    assert len(result) == 64
    assert direct_vm.run_validator() is True
    approved = json.loads(contract.get_approved_specification(circuit_commitment))
    assert approved["status"] == "APPROVED"
    assert json.loads(approved["specification"])["circuit_commitment"] == circuit_commitment
    with direct_vm.expect_revert():
        contract.approve_specification(
            PROCESSOR, CIRCUIT_ID, "Return one when two inputs are not both one.",
            circuit_commitment,
            2,
            1,
            NETLIST,
            json.dumps(specification),
        )


def test_unrelated_caller_cannot_consume_approval_slot(direct_vm, direct_deploy, direct_alice):
    contract = direct_deploy(CONTRACT)
    circuit_commitment = commitment()
    register(contract, circuit_commitment)
    specification = {
        "purpose": "Return NAND",
        "inputs": ["a", "b"],
        "outputs": ["result"],
        "behaviour": "result = NOT(a AND b)",
        "test_cases": [{"input": "01", "output": "01"}],
        "assumptions": [],
        "ambiguities": [],
        "ambiguous": False,
        "circuit_commitment": circuit_commitment,
    }
    direct_vm.sender = direct_alice
    direct_vm.mock_llm("ComputeMesh", json.dumps(specification))
    with direct_vm.expect_revert():
        contract.approve_specification(
            PROCESSOR, CIRCUIT_ID, "Unrelated intent", circuit_commitment, 2, 1, NETLIST,
            json.dumps(specification),
        )
    assert contract.get_approved_specification(circuit_commitment) == ""


def test_incompatible_vectors_are_rejected_before_storage(direct_vm, direct_deploy):
    contract = direct_deploy(CONTRACT)
    circuit_commitment = commitment()
    register(contract, circuit_commitment)
    specification = {
        "purpose": "Return NAND",
        "inputs": ["a", "b"],
        "outputs": ["result"],
        "behaviour": "result = NOT(a AND b)",
        "test_cases": [{"input": "01", "output": "00"}],
        "assumptions": [],
        "ambiguities": [],
        "ambiguous": False,
        "circuit_commitment": circuit_commitment,
    }
    direct_vm.mock_llm("ComputeMesh", json.dumps(specification))
    with direct_vm.expect_revert():
        contract.approve_specification(
            PROCESSOR, CIRCUIT_ID, "Return NAND", circuit_commitment, 2, 1, NETLIST,
            json.dumps(specification),
        )
    assert contract.get_approved_specification(circuit_commitment) == ""


def test_approval_rejects_commitment_that_does_not_match_netlist(direct_vm, direct_deploy):
    contract = direct_deploy(CONTRACT)
    wrong_commitment = "00" * 32
    with direct_vm.expect_revert():
        contract.approve_specification(
            PROCESSOR, CIRCUIT_ID, "Return NAND", wrong_commitment, 2, 1, NETLIST,
            json.dumps({"circuit_commitment": wrong_commitment}),
        )
    assert contract.get_approved_specification(wrong_commitment) == ""


def test_incompatible_approval_can_be_recovered_then_reapproved(direct_vm, direct_deploy):
    contract = direct_deploy(CONTRACT)
    circuit_commitment = commitment()
    register(contract, circuit_commitment)
    seed_approval(contract, circuit_commitment, vectors=[{"input": "01", "output": "00"}])
    record = json.loads(contract.approved_specifications[circuit_commitment])
    assert contract.recover_incompatible_approval(
        circuit_commitment, 2, 1, NETLIST, record["specification_hash"]
    ) is True
    assert json.loads(contract.get_approved_specification(circuit_commitment))["status"] == "REVOKED"

    specification = {
        "purpose": "Return NAND",
        "inputs": ["a", "b"],
        "outputs": ["result"],
        "behaviour": "result = NOT(a AND b)",
        "test_cases": [{"input": "01", "output": "01"}],
        "assumptions": [],
        "ambiguities": [],
        "ambiguous": False,
        "circuit_commitment": circuit_commitment,
    }
    direct_vm.mock_llm("ComputeMesh", json.dumps(specification))
    assert len(contract.approve_specification(
        PROCESSOR, CIRCUIT_ID, "Return NAND", circuit_commitment, 2, 1, NETLIST,
        json.dumps(specification),
    )) == 64


def test_compatible_approval_cannot_be_recovered(direct_vm, direct_deploy):
    contract = direct_deploy(CONTRACT)
    circuit_commitment = commitment()
    seed_approval(contract, circuit_commitment)
    record = json.loads(contract.approved_specifications[circuit_commitment])

    with direct_vm.expect_revert():
        contract.recover_incompatible_approval(
            circuit_commitment, 2, 1, NETLIST, record["specification_hash"]
        )

    assert json.loads(contract.get_approved_specification(circuit_commitment))["status"] == "APPROVED"
