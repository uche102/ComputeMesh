import hashlib
from pathlib import Path


def test_nand_execution_verification(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    input_count = 2
    output_count = 1
    netlist_hex = "00000002000003"

    preimage = (
        b"ComputeMesh/TAP-02/Circuit/v1"
        + input_count.to_bytes(4, "big")
        + output_count.to_bytes(4, "big")
        + bytes.fromhex(netlist_hex)
    )
    commitment_hex = hashlib.sha256(preimage).hexdigest()

    # Inputs: 1 and 0. NAND(1, 0) = 1.
    valid = contract.verify_execution(
        input_count, output_count, netlist_hex,
        commitment_hex, "", "01", "", "01", [], "00" * 20, "0000000000000002"
    )
    assert valid is True

    wrong_output = contract.verify_execution(
        input_count, output_count, netlist_hex,
        commitment_hex, "", "01", "", "00", [], "00" * 20, "0000000000000002"
    )
    assert wrong_output is False

    wrong_commitment = contract.verify_execution(
        input_count, output_count, netlist_hex,
        "00" * 32, "", "01", "", "01", [], "00" * 20, "0000000000000002"
    )
    assert wrong_commitment is False


def test_latch_state_transition(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    input_count = 1
    output_count = 1
    netlist_hex = "01000002"

    preimage = (
        b"ComputeMesh/TAP-02/Circuit/v1"
        + input_count.to_bytes(4, "big")
        + output_count.to_bytes(4, "big")
        + bytes.fromhex(netlist_hex)
    )
    commitment_hex = hashlib.sha256(preimage).hexdigest()

    # Old state = 0; input = 1.
    # Output = old state (0); next state = 1.
    assert contract.verify_execution(
        input_count, output_count, netlist_hex,
        commitment_hex, "00", "01", "01", "00", [], "00" * 20, "0000000000000002"
    ) is True

    # Incorrect next state.
    assert contract.verify_execution(
        input_count, output_count, netlist_hex,
        commitment_hex, "00", "01", "00", "00", [], "00" * 20, "0000000000000002"
    ) is False
        # Second execution: previous next state becomes the new initial state.
    # Old state = 1; input = 0.
    # Output = old state (1); next state = 0.
    assert contract.verify_execution(
        input_count, output_count, netlist_hex,
        commitment_hex, "01", "00", "00", "01", [], "00" * 20, "0000000000000002"
    ) is True


def test_ref_with_child_latch_state(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    child_netlist = "01000002"
    child_preimage = (
        b"ComputeMesh/TAP-02/Circuit/v1"
        + (1).to_bytes(4, "big")
        + (1).to_bytes(4, "big")
        + bytes.fromhex(child_netlist)
    )
    child_commitment = hashlib.sha256(child_preimage).hexdigest()

    address_hex = "00" * 20
    circuit_id_hex = "0000000000000001"

    child_record = ":".join([
        address_hex,
        circuit_id_hex,
        "1",
        "1",
        child_commitment,
        child_netlist,
    ])
    assert contract.register_circuit(
    address_hex,
    circuit_id_hex,
    child_commitment,
) is True

    # REF child: one input, one output, input signal index 2.
    parent_netlist = (
        "02"
        + address_hex
        + circuit_id_hex
        + "0101"
        + "000002"
    )

    parent_preimage = (
        b"ComputeMesh/TAP-02/Circuit/v1"
        + (1).to_bytes(4, "big")
        + (1).to_bytes(4, "big")
        + bytes.fromhex(parent_netlist)
    )
    parent_commitment = hashlib.sha256(parent_preimage).hexdigest()

    # Child outputs its old state (0) and advances its state to 1.
    assert contract.verify_execution(
        1, 1, parent_netlist,
        parent_commitment, "00", "01", "01", "00",
        [child_record], "00" * 20, "0000000000000002",
    ) is True

    # Incorrect nested next state must be rejected.
    assert contract.verify_execution(
        1, 1, parent_netlist,
        parent_commitment, "00", "01", "00", "00",
        [child_record], "00" * 20, "0000000000000002",
    ) is False

def test_root_circular_ref_rejected(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    address_hex = "00" * 20
    circuit_id_hex = "0000000000000002"

    # Root circuit references itself.
    netlist_hex = (
        "02"
        + address_hex
        + circuit_id_hex
        + "0101"
        + "000002"
    )

    preimage = (
        b"ComputeMesh/TAP-02/Circuit/v1"
        + (1).to_bytes(4, "big")
        + (1).to_bytes(4, "big")
        + bytes.fromhex(netlist_hex)
    )
    commitment_hex = hashlib.sha256(preimage).hexdigest()

    assert contract.verify_execution(
        1, 1, netlist_hex,
        commitment_hex, "", "01", "", "00",
        [], address_hex, circuit_id_hex,
    ) is False

def test_nested_circular_ref_rejected(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    address = "00" * 20

    def make_ref(target_id):
        return (
            "02" + address + target_id
            + "0101" + "000002"
        )

    def make_commitment(netlist):
        preimage = (
            b"ComputeMesh/TAP-02/Circuit/v1"
            + (1).to_bytes(4, "big")
            + (1).to_bytes(4, "big")
            + bytes.fromhex(netlist)
        )
        return hashlib.sha256(preimage).hexdigest()

    child_a_id = "0000000000000001"
    child_b_id = "0000000000000003"
    root_id = "0000000000000002"

    # Child A references Child B.
    child_a_netlist = make_ref(child_b_id)

    # Child B references Child A, creating a cycle.
    child_b_netlist = make_ref(child_a_id)

    child_a_record = ":".join([
        address, child_a_id, "1", "1",
        make_commitment(child_a_netlist),
        child_a_netlist,
    ])

    child_b_record = ":".join([
        address, child_b_id, "1", "1",
        make_commitment(child_b_netlist),
        child_b_netlist,
    ])

    # Root references Child A.
    root_netlist = make_ref(child_a_id)

    assert contract.verify_execution(
        1, 1, root_netlist,
        make_commitment(root_netlist),
        "", "01", "", "00",
        [child_a_record, child_b_record],
        address, root_id,
    ) is False

def test_malformed_hex_rejected(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    result = contract.verify_execution(
        2, 1,
        "00 000002000003",  # Deliberately contains whitespace.
        "00" * 32,
        "", "01", "", "01",
        [],
        "00" * 20,
        "0000000000000002",
    )

    assert result is False

def test_invalid_child_hex_rejected(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    malformed_child = ":".join([
        "0g" * 20,  # Invalid character: g
        "0000000000000001",
        "1",
        "1",
        "00" * 32,
        "01000002",
    ])

    result = contract.verify_execution(
        2, 1,
        "00000002000003",
        "00" * 32,
        "", "01", "", "01",
        [malformed_child],
        "00" * 20,
        "0000000000000002",
    )

    assert result is False

def test_odd_length_hex_rejected(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    result = contract.verify_execution(
        2, 1,
        "0000000200000",  # Odd number of hex characters.
        "00" * 32,
        "", "01", "", "01",
        [],
        "00" * 20,
        "0000000000000002",
    )

    assert result is False

def test_invalid_commitment_length_rejected(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    result = contract.verify_execution(
        2, 1,
        "00000002000003",
        "00" * 31,  # Only 31 bytes instead of 32.
        "", "01", "", "01",
        [],
        "00" * 20,
        "0000000000000002",
    )

    assert result is False

def test_unregistered_child_circuit_rejected(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    address_hex = "11" * 20
    circuit_id_hex = "0000000000000005"

    # A valid child NAND circuit.
    child_netlist = "00000002000003"

    child_preimage = (
        b"ComputeMesh/TAP-02/Circuit/v1"
        + (2).to_bytes(4, "big")
        + (1).to_bytes(4, "big")
        + bytes.fromhex(child_netlist)
    )
    child_commitment = hashlib.sha256(child_preimage).hexdigest()

    child_record = ":".join([
        address_hex,
        circuit_id_hex,
        "2",
        "1",
        child_commitment,
        child_netlist,
    ])

    # Root circuit references the child.
    root_netlist = (
        "02"
        + address_hex
        + circuit_id_hex
        + "0201"
        + "000002"
        + "000003"
    )

    root_preimage = (
        b"ComputeMesh/TAP-02/Circuit/v1"
        + (2).to_bytes(4, "big")
        + (1).to_bytes(4, "big")
        + bytes.fromhex(root_netlist)
    )
    root_commitment = hashlib.sha256(root_preimage).hexdigest()

    result = contract.verify_execution(
        2,
        1,
        root_netlist,
        root_commitment,
        "",
        "01",
        "",
        "01",
        [child_record],
        "00" * 20,
        "0000000000000002",
    )

    assert result is False
def test_registered_child_commitment_mismatch_rejected(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    address_hex = "11" * 20
    circuit_id_hex = "0000000000000005"

    child_netlist = "00000002000003"

    child_preimage = (
        b"ComputeMesh/TAP-02/Circuit/v1"
        + (2).to_bytes(4, "big")
        + (1).to_bytes(4, "big")
        + bytes.fromhex(child_netlist)
    )
    child_commitment = hashlib.sha256(child_preimage).hexdigest()

    # Register a different commitment for this child.
    wrong_commitment = "00" * 32

    assert contract.register_circuit(
        address_hex,
        circuit_id_hex,
        wrong_commitment,
    ) is True

    child_record = ":".join([
        address_hex,
        circuit_id_hex,
        "2",
        "1",
        child_commitment,
        child_netlist,
    ])

    root_netlist = (
        "02"
        + address_hex
        + circuit_id_hex
        + "0201"
        + "000002"
        + "000003"
    )

    root_preimage = (
        b"ComputeMesh/TAP-02/Circuit/v1"
        + (2).to_bytes(4, "big")
        + (1).to_bytes(4, "big")
        + bytes.fromhex(root_netlist)
    )
    root_commitment = hashlib.sha256(root_preimage).hexdigest()

    result = contract.verify_execution(
        2,
        1,
        root_netlist,
        root_commitment,
        "",
        "01",
        "",
        "01",
        [child_record],
        "00" * 20,
        "0000000000000002",
    )

    assert result is False

def test_unauthorized_registration_rejected(
    direct_vm, direct_deploy, direct_alice
):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    direct_vm.sender = direct_alice

    result = contract.register_circuit(
        "11" * 20,
        "0000000000000005",
        "22" * 32,
    )

    assert result is False

def test_duplicate_registration_rejected(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    address_hex = "11" * 20
    circuit_id_hex = "0000000000000005"
    commitment_hex = "22" * 32

    first = contract.register_circuit(
        address_hex, circuit_id_hex, commitment_hex
    )
    second = contract.register_circuit(
        address_hex, circuit_id_hex, commitment_hex
    )

    assert first is True
    assert second is False

def test_registered_commitment_cannot_be_overwritten(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    address_hex = "11" * 20
    circuit_id_hex = "0000000000000005"
    original_commitment = "22" * 32
    replacement_commitment = "33" * 32

    first = contract.register_circuit(
        address_hex, circuit_id_hex, original_commitment
    )
    second = contract.register_circuit(
        address_hex, circuit_id_hex, replacement_commitment
    )

    stored = contract.get_registered_commitment(
        address_hex, circuit_id_hex
    )

    assert first is True
    assert second is False
    assert stored == original_commitment

def test_invalid_registration_data_rejected(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    valid_address = "11" * 20
    valid_id = "0000000000000005"
    valid_commitment = "22" * 32

    assert contract.register_circuit(
        "ZZ" * 20, valid_id, valid_commitment
    ) is False

    assert contract.register_circuit(
        valid_address, "INVALID", valid_commitment
    ) is False

    assert contract.register_circuit(
        valid_address, valid_id, "ZZ" * 32
    ) is False

def test_registered_commitment_persists(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    address_hex = "11" * 20
    circuit_id_hex = "0000000000000005"
    commitment_hex = "22" * 32

    registered = contract.register_circuit(
        address_hex, circuit_id_hex, commitment_hex
    )

    first_read = contract.get_registered_commitment(
        address_hex, circuit_id_hex
    )
    second_read = contract.get_registered_commitment(
        address_hex, circuit_id_hex
    )

    assert registered is True
    assert first_read == commitment_hex
    assert second_read == commitment_hex

def test_nested_ref_execution(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    address = "11" * 20
    child_a_id = "0000000000000001"
    child_b_id = "0000000000000002"
    root_id = "0000000000000003"

    def make_commitment(netlist):
        preimage = (
            b"ComputeMesh/TAP-02/Circuit/v1"
            + (2).to_bytes(4, "big")
            + (1).to_bytes(4, "big")
            + bytes.fromhex(netlist)
        )
        return hashlib.sha256(preimage).hexdigest()

    def make_ref(target_id):
        return (
            "02" + address + target_id
            + "0201" + "000002000003"
        )

    # Child B executes NAND with inputs 1 and 0.
    child_b_netlist = "00000002000003"
    child_b_commitment = make_commitment(child_b_netlist)

    # Child A references Child B.
    child_a_netlist = make_ref(child_b_id)
    child_a_commitment = make_commitment(child_a_netlist)

    # Register both child circuits.
    assert contract.register_circuit(
        address, child_a_id, child_a_commitment
    ) is True

    assert contract.register_circuit(
        address, child_b_id, child_b_commitment
    ) is True

    child_b_record = ":".join([
        address, child_b_id, "2", "1",
        child_b_commitment, child_b_netlist,
    ])

    child_a_record = ":".join([
        address, child_a_id, "2", "1",
        child_a_commitment, child_a_netlist,
    ])

    # Root references Child A.
    root_netlist = make_ref(child_a_id)
    root_commitment = make_commitment(root_netlist)

    assert contract.verify_execution(
        2, 1, root_netlist,
        root_commitment,
        "", "01",
        "", "01",
        [child_a_record, child_b_record],
        address, root_id,
    ) is True

def test_missing_nested_ref_child_rejected(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    address = "11" * 20
    child_a_id = "0000000000000001"
    child_b_id = "0000000000000002"
    root_id = "0000000000000003"

    def make_commitment(netlist):
        preimage = (
            b"ComputeMesh/TAP-02/Circuit/v1"
            + (2).to_bytes(4, "big")
            + (1).to_bytes(4, "big")
            + bytes.fromhex(netlist)
        )
        return hashlib.sha256(preimage).hexdigest()

    def make_ref(target_id):
        return (
            "02" + address + target_id
            + "0201" + "000002000003"
        )

    # Child A references Child B.
    child_a_netlist = make_ref(child_b_id)
    child_a_commitment = make_commitment(child_a_netlist)

    assert contract.register_circuit(
        address, child_a_id, child_a_commitment
    ) is True

    child_a_record = ":".join([
        address, child_a_id, "2", "1",
        child_a_commitment, child_a_netlist,
    ])

    # Root references Child A, but Child B is deliberately omitted.
    root_netlist = make_ref(child_a_id)
    root_commitment = make_commitment(root_netlist)

    assert contract.verify_execution(
        2, 1, root_netlist,
        root_commitment,
        "", "0100",
        "", "01",
        [child_a_record],
        address, root_id,
    ) is False

def test_maximum_ref_depth_rejected(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    address = "11" * 20
    root_id = "0000000000000000"

    def circuit_id(number):
        return number.to_bytes(8, "big").hex()

    def make_commitment(netlist):
        preimage = (
            b"ComputeMesh/TAP-02/Circuit/v1"
            + (2).to_bytes(4, "big")
            + (1).to_bytes(4, "big")
            + bytes.fromhex(netlist)
        )
        return hashlib.sha256(preimage).hexdigest()

    def make_ref(target_id):
        return (
            "02" + address + target_id
            + "0201" + "000002000003"
        )

    child_records = []

    # Build 64 REF circuits, followed by one terminal NAND circuit.
    for number in range(1, 66):
        current_id = circuit_id(number)

        if number < 65:
            netlist = make_ref(circuit_id(number + 1))
        else:
            netlist = "00000002000003"

        commitment = make_commitment(netlist)

        assert contract.register_circuit(
            address, current_id, commitment
        ) is True

        child_records.append(":".join([
            address,
            current_id,
            "2",
            "1",
            commitment,
            netlist,
        ]))

    root_netlist = make_ref(circuit_id(1))
    root_commitment = make_commitment(root_netlist)

    # Root depth 0 + 65 child calls = depth 65.
    assert contract.verify_execution(
        2, 1, root_netlist,
        root_commitment,
        "", "0100",
        "", "01",
        child_records,
        address, root_id,
    ) is False

def test_nested_ref_with_latch_state(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    address = "11" * 20
    child_a_id = "0000000000000001"
    child_b_id = "0000000000000002"
    root_id = "0000000000000003"

    def make_commitment(netlist):
        preimage = (
            b"ComputeMesh/TAP-02/Circuit/v1"
            + (1).to_bytes(4, "big")
            + (1).to_bytes(4, "big")
            + bytes.fromhex(netlist)
        )
        return hashlib.sha256(preimage).hexdigest()

    def make_ref(target_id):
        return (
            "02" + address + target_id
            + "0101" + "000002"
        )

    # Child B contains a LATCH.
    child_b_netlist = "01000002"
    child_b_commitment = make_commitment(child_b_netlist)

    # Child A references Child B.
    child_a_netlist = make_ref(child_b_id)
    child_a_commitment = make_commitment(child_a_netlist)

    assert contract.register_circuit(
        address, child_a_id, child_a_commitment
    ) is True

    assert contract.register_circuit(
        address, child_b_id, child_b_commitment
    ) is True

    child_a_record = ":".join([
        address, child_a_id, "1", "1",
        child_a_commitment, child_a_netlist,
    ])

    child_b_record = ":".join([
        address, child_b_id, "1", "1",
        child_b_commitment, child_b_netlist,
    ])

    # Root references Child A, which references Child B.
    root_netlist = make_ref(child_a_id)
    root_commitment = make_commitment(root_netlist)

    # The nested LATCH outputs its old state (0)
    # and updates its state to the input (1).
    assert contract.verify_execution(
        1, 1, root_netlist,
        root_commitment,
        "00", "01",
        "01", "00",
        [child_a_record, child_b_record],
        address, root_id,
    ) is True

def test_latch_can_reference_future_signal(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    # LATCH references signal 4, created by the NAND below.
    netlist_hex = (
        "01" + "000004"
        + "00" + "000002000002"
    )

    preimage = (
        b"ComputeMesh/TAP-02/Circuit/v1"
        + (1).to_bytes(4, "big")
        + (1).to_bytes(4, "big")
        + bytes.fromhex(netlist_hex)
    )
    commitment = hashlib.sha256(preimage).hexdigest()

    assert contract.verify_execution(
        1, 1,
        netlist_hex,
        commitment,
        "00", "00",
        "01", "01",
        [],
        "11" * 20,
        "0000000000000001",
    ) is True


def test_input_count_above_max_pins_rejected(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    input_count = 65537
    output_count = 1
    netlist_hex = ""

    preimage = (
        b"ComputeMesh/TAP-02/Circuit/v1"
        + input_count.to_bytes(4, "big")
        + output_count.to_bytes(4, "big")
        + bytes.fromhex(netlist_hex)
    )
    commitment_hex = hashlib.sha256(preimage).hexdigest()

    assert contract.verify_execution(
        input_count, output_count, netlist_hex,
        commitment_hex, "", "", "", "", [],
        "00" * 20, "0000000000000002"
    ) is False


def test_shared_differential_vectors(direct_deploy):
    contract = direct_deploy(
        "integrations/genlayer/contracts/computemesh_verifier.py"
    )

    fixture_path = (
        Path(__file__).resolve().parents[2]
        / "tests"
        / "fixtures"
        / "differential_vectors.tsv"
    )

    for line in fixture_path.read_text().splitlines():
        if not line or line.startswith("#"):
            continue

        (
            name,
            input_count,
            output_count,
            netlist_hex,
            initial_state_hex,
            inputs_hex,
            next_state_hex,
            outputs_hex,
        ) = line.split("\t")

        preimage = (
            b"ComputeMesh/TAP-02/Circuit/v1"
            + int(input_count).to_bytes(4, "big")
            + int(output_count).to_bytes(4, "big")
            + bytes.fromhex(netlist_hex)
        )

        commitment_hex = hashlib.sha256(preimage).hexdigest()

        assert contract.verify_execution(
            int(input_count),
            int(output_count),
            netlist_hex,
            commitment_hex,
            initial_state_hex,
            inputs_hex,
            next_state_hex,
            outputs_hex,
            [],
            "00" * 20,
            "0000000000000002",
        ) is True, f"Shared vector failed: {name}"
