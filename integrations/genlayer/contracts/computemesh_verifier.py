# { "Depends": "py-genlayer:1jb45aa8ynh2a9c9xn3b7qqh8sm5q93hwfp7jqmwsfhh8jpz09h6" }

from genlayer import *
import hashlib
import json


class ComputeMeshVerifier(gl.Contract):
    administrator: Address
    registered_commitments: TreeMap[str, str]
    approved_specifications: TreeMap[str, str]

    def __init__(self):
        self.administrator = gl.message.sender_address
    @gl.public.write
    def register_circuit(
        self,
        processor_address_hex: str,
        circuit_id_hex: str,
        commitment_hex: str,
    ) -> bool:
        if gl.message.sender_address != self.administrator:
            return False

        if len(processor_address_hex) != 40:
            return False
        if len(circuit_id_hex) != 16:
            return False
        if len(commitment_hex) != 64:
            return False

        allowed = "0123456789abcdefABCDEF"
        if not all(c in allowed for c in (
            processor_address_hex + circuit_id_hex + commitment_hex
        )):
            return False

        key = (
            processor_address_hex.lower()
            + ":"
            + circuit_id_hex.lower()
        )

        if key in self.registered_commitments:
            return False

        self.registered_commitments[key] = commitment_hex.lower()
        return True

    @gl.public.view
    def get_registered_commitment(
        self,
        processor_address_hex: str,
        circuit_id_hex: str,
    ) -> str:
        key = (
            processor_address_hex.lower()
            + ":"
            + circuit_id_hex.lower()
        )
        return self.registered_commitments.get(key, "")

    @gl.public.write
    def approve_specification(
        self,
        description: str,
        circuit_commitment_hex: str,
        input_count: int,
        output_count: int,
        netlist_hex: str,
        proposed_specification_json: str,
    ) -> str:
        """Reach consensus on the meaning of a natural-language circuit request.

        The leader and each validator independently interpret the same request.
        Validators compare substantive semantic fields, not merely JSON shape.
        Only the accepted interpretation is persisted and it is keyed by the
        circuit commitment, preventing an approved description being reused for
        a different circuit.
        """
        if not description or len(description) > 4096:
            raise gl.vm.UserError("Circuit description is empty or too long")
        if len(circuit_commitment_hex) != 64 or len(netlist_hex) % 2 != 0:
            raise gl.vm.UserError("Circuit commitment must be 32-byte hex")
        allowed = "0123456789abcdefABCDEF"
        if not all(c in allowed for c in circuit_commitment_hex + netlist_hex):
            raise gl.vm.UserError("Circuit commitment is not hexadecimal")
        if not 0 <= input_count <= 65536 or not 1 <= output_count <= 65536:
            raise gl.vm.UserError("Invalid circuit dimensions")

        netlist = bytes.fromhex(netlist_hex)
        expected_commitment = hashlib.sha256(
            b"ComputeMesh/TAP-02/Circuit/v1"
            + input_count.to_bytes(4, "big")
            + output_count.to_bytes(4, "big")
            + netlist
        ).hexdigest()
        if expected_commitment != circuit_commitment_hex.lower():
            raise gl.vm.UserError("Circuit commitment does not match netlist")
        approval_key = circuit_commitment_hex.lower()
        if self.approved_specifications.get(approval_key, ""):
            raise gl.vm.UserError("Circuit commitment already has an immutable approval")

        def is_strict_hex(value):
            return (
                isinstance(value, str)
                and len(value) % 2 == 0
                and all(c in allowed for c in value)
            )

        def canonical_bits(data, bit_count):
            expected = (bit_count + 7) // 8
            if len(data) != expected:
                return False
            return not (bit_count % 8 and data and (data[-1] & (0xff << (bit_count % 8))))

        # Reject malformed or unsupported TAP-02 records before consensus is
        # allowed to approve a specification for this commitment.
        offset = 0
        while offset < len(netlist):
            opcode = netlist[offset]
            offset += 1
            if opcode in (0x00, 0x01):
                width = 6 if opcode == 0x00 else 3
                if offset + width > len(netlist):
                    raise gl.vm.UserError("Truncated TAP-02 instruction")
                offset += width
            elif opcode == 0x02:
                if offset + 30 > len(netlist):
                    raise gl.vm.UserError("Truncated TAP-02 REF")
                input_width = netlist[offset + 28]
                output_width = netlist[offset + 29]
                if output_width == 0 or offset + 30 + 3 * input_width > len(netlist):
                    raise gl.vm.UserError("Malformed TAP-02 REF")
                offset += 30 + 3 * input_width
            else:
                raise gl.vm.UserError("Unsupported TAP-02 opcode")

        prompt = f"""
You are interpreting a digital logic circuit request for ComputeMesh.
The circuit uses only TAP-02 operations: NAND (0x00), LATCH (0x01), and REF (0x02).
Interpret the request conservatively. If it is ambiguous, set ambiguous=true and
list the missing decisions instead of guessing.

Developer request:
{description}

The proposed circuit commitment is: {circuit_commitment_hex.lower()}
The developer's draft interpretation is:
{proposed_specification_json}

Return JSON only with exactly these fields:
purpose (string), inputs (array of strings), outputs (array of strings),
behaviour (string), test_cases (array of objects with hexadecimal input/output
strings and optional state/next_state strings for LATCH circuits),
assumptions (array of strings), ambiguities (array of strings), ambiguous (boolean),
and circuit_commitment (lowercase string).
Do not invent requirements absent from the request. The circuit commitment must
be copied exactly.
"""

        def interpret() -> dict:
            result = gl.nondet.exec_prompt(prompt, response_format="json")
            if isinstance(result, dict):
                return result
            return json.loads(result)

        def validate(leader_result) -> bool:
            if not isinstance(leader_result, gl.vm.Return):
                return False
            try:
                leader = leader_result.calldata
                validator = interpret()
                required = (
                    "purpose", "inputs", "outputs", "behaviour", "test_cases",
                    "assumptions", "ambiguities", "ambiguous", "circuit_commitment",
                )
                if any(key not in leader or key not in validator for key in required):
                    return False
                if leader["circuit_commitment"].lower() != circuit_commitment_hex.lower():
                    return False
                if validator["circuit_commitment"].lower() != circuit_commitment_hex.lower():
                    return False
                # Reasoning text may differ, but the accepted interpretation may
                # not: validators must agree on purpose, interfaces, behaviour,
                # ambiguity status, assumptions, and expected vectors.
                semantic_fields = (
                    "purpose", "inputs", "outputs", "behaviour", "test_cases",
                    "assumptions", "ambiguous", "ambiguities",
                )
                return all(leader[field] == validator[field] for field in semantic_fields)
            except (AttributeError, KeyError, TypeError, ValueError):
                return False

        accepted = gl.vm.run_nondet_unsafe(interpret, validate)
        if not isinstance(accepted, dict):
            raise gl.vm.UserError("Consensus returned an invalid specification")

        required = (
            "purpose", "inputs", "outputs", "behaviour", "test_cases",
            "assumptions", "ambiguities", "ambiguous", "circuit_commitment",
        )
        if any(field not in accepted for field in required):
            raise gl.vm.UserError("Consensus specification is missing required fields")
        if not isinstance(accepted["purpose"], str) or not accepted["purpose"].strip():
            raise gl.vm.UserError("Specification purpose is empty")
        if not isinstance(accepted["behaviour"], str) or not accepted["behaviour"].strip():
            raise gl.vm.UserError("Specification behaviour is empty")
        if not isinstance(accepted["inputs"], list) or not isinstance(accepted["outputs"], list):
            raise gl.vm.UserError("Specification interfaces must be arrays")
        if len(accepted["inputs"]) != input_count or len(accepted["outputs"]) != output_count:
            raise gl.vm.UserError("Specification dimensions do not match circuit")
        if len(set(accepted["inputs"])) != len(accepted["inputs"]):
            raise gl.vm.UserError("Duplicate input name")
        if len(set(accepted["outputs"])) != len(accepted["outputs"]):
            raise gl.vm.UserError("Duplicate output name")
        if not all(isinstance(value, str) and value.strip() for value in accepted["inputs"] + accepted["outputs"]):
            raise gl.vm.UserError("Invalid input or output name")
        if not isinstance(accepted["assumptions"], list) or not all(isinstance(value, str) and value.strip() for value in accepted["assumptions"]):
            raise gl.vm.UserError("Invalid assumptions")
        if not isinstance(accepted["ambiguities"], list) or not all(isinstance(value, str) for value in accepted["ambiguities"]):
            raise gl.vm.UserError("Invalid ambiguities")
        if not isinstance(accepted["ambiguous"], bool) or accepted["ambiguous"] or accepted["ambiguities"]:
            raise gl.vm.UserError("Consensus found an ambiguous specification")
        if accepted["circuit_commitment"].lower() != approval_key:
            raise gl.vm.UserError("Consensus commitment mismatch")

        vectors = accepted["test_cases"]
        if not isinstance(vectors, list) or not vectors:
            raise gl.vm.UserError("Specification requires test vectors")
        seen_vectors = set()
        for vector in vectors:
            if not isinstance(vector, dict):
                raise gl.vm.UserError("Malformed test vector")
            vector_keys = set(vector.keys())
            if not ({"input", "output"} <= vector_keys) or not vector_keys <= {"input", "output", "state", "next_state"}:
                raise gl.vm.UserError("Test vectors contain unsupported fields")
            vector_input = vector["input"]
            vector_output = vector["output"]
            if not isinstance(vector_input, str) or not isinstance(vector_output, str):
                raise gl.vm.UserError("Malformed test vector values")
            if not is_strict_hex(vector_input) or not is_strict_hex(vector_output):
                raise gl.vm.UserError("Test vectors must be even-length hexadecimal")
            input_bytes = bytes.fromhex(vector_input)
            output_bytes = bytes.fromhex(vector_output)
            if not canonical_bits(input_bytes, input_count) or not canonical_bits(output_bytes, output_count):
                raise gl.vm.UserError("Test vector width does not match circuit")
            if "state" in vector and (not isinstance(vector["state"], str) or not is_strict_hex(vector["state"])):
                raise gl.vm.UserError("Malformed test vector state")
            if "next_state" in vector and (not isinstance(vector["next_state"], str) or not is_strict_hex(vector["next_state"])):
                raise gl.vm.UserError("Malformed test vector next state")
            if ("state" in vector) != ("next_state" in vector):
                raise gl.vm.UserError("Stateful vectors require state and next_state")
            vector_key = vector_input.lower()
            if vector_key in seen_vectors:
                raise gl.vm.UserError("Contradictory or duplicate test vector")
            seen_vectors.add(vector_key)

        # Only the fields that are validated above become the persisted record.
        canonical_specification = json.dumps(accepted, sort_keys=True, separators=(",", ":"))
        specification_hash = hashlib.sha256(canonical_specification.encode("utf-8")).hexdigest()
        record = json.dumps({
            "specification": canonical_specification,
            "specification_hash": specification_hash,
            "circuit_commitment": approval_key,
            "status": "APPROVED",
            "version": 1,
            "input_count": input_count,
            "output_count": output_count,
            "test_vectors": vectors,
        }, sort_keys=True, separators=(",", ":"))
        self.approved_specifications[approval_key] = record
        return specification_hash

    @gl.public.view
    def get_approved_specification(self, circuit_commitment_hex: str) -> str:
        return self.approved_specifications.get(circuit_commitment_hex.lower(), "")

    @gl.public.view
    def verify_execution(
        self,
        input_count: int,
        output_count: int,
        netlist_hex: str,
        commitment_hex: str,
        initial_state_hex: str,
        inputs_hex: str,
        claimed_next_state_hex: str,
        claimed_outputs_hex: str,
        child_records: list[str],
        root_processor_address_hex: str,
        root_circuit_id_hex: str,
    ) -> bool:

        MAX_PINS = 65536
        MAX_STATE = 1 << 24
        MAX_SIGNALS = 1 << 24
        MAX_DEPTH = 64
        MAX_CHILD_RECORDS = 256
        MAX_CHILD_NETLIST_BYTES = 1_048_576

        if len(child_records) > MAX_CHILD_RECORDS:
            return False

        def is_strict_hex(value):
            if not isinstance(value, str):
                return False
            if len(value) % 2 != 0:
                return False
            return all(c in "0123456789abcdefABCDEF" for c in value)

        def read_bit(data, index):
            pos = index // 8
            if pos >= len(data):
                return False
            return bool((data[pos] >> (index % 8)) & 1)

        def pack_bits(bits):
            result = bytearray((len(bits) + 7) // 8)
            for i, bit in enumerate(bits):
                if bit:
                    result[i // 8] |= 1 << (i % 8)
            return bytes(result)

        def canonical_bits(data, bit_count):
            expected = (bit_count + 7) // 8
            if len(data) != expected:
                return False
            if bit_count % 8 and data:
                return (data[-1] & (0xff << (bit_count % 8))) == 0
            return True

        def commitment_for(ins, outs, netlist):
            preimage = (
                b"ComputeMesh/TAP-02/Circuit/v1"
                + ins.to_bytes(4, "big")
                + outs.to_bytes(4, "big")
                + netlist
            )
            return hashlib.sha256(preimage).digest()

        # Child record format:
        # address_hex:id_hex:input_count:output_count:commitment_hex:netlist_hex
        # id_hex is exactly 16 hexadecimal characters.
        total_child_netlist_bytes = 0
        children = {}

        try:
            for record in child_records:
                fields = record.split(":")
                if len(fields) != 6:
                    return False
                if not all(is_strict_hex(fields[i]) for i in (0, 1, 4, 5)):
                    return False
                total_child_netlist_bytes += len(fields[5]) // 2
                if total_child_netlist_bytes > MAX_CHILD_NETLIST_BYTES:
                    return False
                address = bytes.fromhex(fields[0])
                circuit_id = int(fields[1], 16)
                child_inputs = int(fields[2])
                child_outputs = int(fields[3])
                child_commitment = bytes.fromhex(fields[4])
                child_netlist = bytes.fromhex(fields[5])

                if len(address) != 20 or len(child_commitment) != 32:
                    return False
                if not 0 <= child_inputs <= MAX_PINS:
                    return False
                if not 1 <= child_outputs <= MAX_PINS:
                    return False
                if len(fields[1]) != 16:
                    return False

                key = (address, circuit_id)
                if key in children:
                    return False

                if commitment_for(
                    child_inputs, child_outputs, child_netlist
                ) != child_commitment:
                    return False


                children[key] = (
                    child_inputs,
                    child_outputs,
                    child_netlist,
                    child_commitment,
                )

            if not all(is_strict_hex(value) for value in (
                root_processor_address_hex,
                root_circuit_id_hex,
                netlist_hex,
                commitment_hex,
                initial_state_hex,
                inputs_hex,
                claimed_next_state_hex,
                claimed_outputs_hex,
            )):
                return False
            root_address = bytes.fromhex(root_processor_address_hex)
            if len(root_address) != 20 or len(root_circuit_id_hex) != 16:
                return False
            root_circuit_id = int(root_circuit_id_hex, 16)

            root_netlist = bytes.fromhex(netlist_hex)
            root_commitment = bytes.fromhex(commitment_hex)
            initial_state = bytes.fromhex(initial_state_hex)
            inputs = bytes.fromhex(inputs_hex)
            claimed_state = bytes.fromhex(claimed_next_state_hex)
            claimed_outputs = bytes.fromhex(claimed_outputs_hex)

            if len(root_commitment) != 32:
                return False
            if not 0 <= input_count <= MAX_PINS:
                return False
            if not 1 <= output_count <= MAX_PINS:
                return False
            if commitment_for(
                input_count, output_count, root_netlist
            ) != root_commitment:
                return False

            approval_record = self.approved_specifications.get(root_commitment.hex(), "")
            if not approval_record:
                return False
            approval = json.loads(approval_record)
            if approval.get("status") != "APPROVED" or approval.get("version") != 1:
                return False
            if approval.get("circuit_commitment") != root_commitment.hex():
                return False
            if approval.get("input_count") != input_count or approval.get("output_count") != output_count:
                return False
            canonical_specification = approval.get("specification", "")
            if not isinstance(canonical_specification, str) or hashlib.sha256(
                canonical_specification.encode("utf-8")
            ).hexdigest() != approval.get("specification_hash"):
                return False
            approved_specification = json.loads(canonical_specification)
            if approved_specification.get("circuit_commitment", "").lower() != root_commitment.hex():
                return False
            approved_vectors = approval.get("test_vectors", [])
            if not isinstance(approved_vectors, list) or not approved_vectors:
                return False

            def parse(netlist):
                elements = []
                offset = 0

                while offset < len(netlist):
                    opcode = netlist[offset]
                    offset += 1

                    if opcode == 0x00:
                        if offset + 6 > len(netlist):
                            raise ValueError("Truncated NAND")
                        a = int.from_bytes(netlist[offset:offset + 3], "big")
                        b = int.from_bytes(netlist[offset + 3:offset + 6], "big")
                        offset += 6
                        elements.append((opcode, (a, b)))

                    elif opcode == 0x01:
                        if offset + 3 > len(netlist):
                            raise ValueError("Truncated LATCH")
                        d = int.from_bytes(netlist[offset:offset + 3], "big")
                        offset += 3
                        elements.append((opcode, (d,)))

                    elif opcode == 0x02:
                        if offset + 30 > len(netlist):
                            raise ValueError("Truncated REF")
                        address = netlist[offset:offset + 20]
                        circuit_id = int.from_bytes(
                            netlist[offset + 20:offset + 28], "big"
                        )
                        n_inputs = netlist[offset + 28]
                        n_outputs = netlist[offset + 29]
                        offset += 30

                        if n_outputs == 0:
                            raise ValueError("Invalid REF outputs")
                        if offset + 3 * n_inputs > len(netlist):
                            raise ValueError("Truncated REF inputs")

                        refs = []
                        for _ in range(n_inputs):
                            refs.append(int.from_bytes(
                                netlist[offset:offset + 3], "big"
                            ))
                            offset += 3

                        elements.append((
                            opcode,
                            (address, circuit_id, n_outputs, tuple(refs))
                        ))

                    else:
                        raise ValueError("Unknown opcode")

                return elements

            def execute(ins, outs, netlist, state, circuit_inputs,
                        recursion_stack, depth):

                if depth > MAX_DEPTH:
                    raise ValueError("REF depth exceeded")
                if not 0 <= ins <= MAX_PINS:
                    raise ValueError("Invalid input count")
                if not 1 <= outs <= MAX_PINS:
                    raise ValueError("Invalid output count")

                elements = parse(netlist)
                signals = [False, True]

                for i in range(ins):
                    signals.append(read_bit(circuit_inputs, i))

                state_count = 0
                child_layout = []
                latch_updates = []
                signal_count = 2 + ins

                # Determine recursive state layout before execution.
                for opcode, refs in elements:
                    if opcode == 0x00:
                        a, b = refs
                        if a >= signal_count or b >= signal_count:
                            raise ValueError("Invalid NAND reference")
                        signal_count += 1

                    elif opcode == 0x01:
                        latch_updates.append((state_count, refs[0]))
                        signal_count += 1
                        state_count += 1

                    else:
                        address, circuit_id, n_outputs, child_refs = refs

                        for ref in child_refs:
                            if ref >= signal_count:
                                raise ValueError("Invalid REF input")

                        key = (address, circuit_id)

                        if key in recursion_stack:
                            raise ValueError("Circular REF")

                        if key not in children:
                            raise ValueError("Missing child circuit")

                        registered_key = (
                            address.hex()
                            + ":"
                            + circuit_id.to_bytes(8, "big").hex()
                        )

                        registered_commitment = self.registered_commitments.get(
                            registered_key, ""
                        )

                        if not registered_commitment:
                            raise ValueError("Unregistered child circuit")

                        if registered_commitment != children[key][3].hex():
                            raise ValueError("Child commitment mismatch")

                        child_ins, child_outs, child_netlist, _ = children[key]

                        if child_ins != len(child_refs):
                            raise ValueError("REF input mismatch")
                        if child_outs != n_outputs:
                            raise ValueError("REF output mismatch")

                        child_meta = execute(
                            child_ins,
                            child_outs,
                            child_netlist,
                            b"",
                            b"",
                            recursion_stack + (key,),
                            depth + 1,
                        )

                        child_state_count = child_meta[0]
                        child_layout.append(child_state_count)
                        state_count += child_state_count
                        signal_count += n_outputs

                    if state_count > MAX_STATE or signal_count > MAX_SIGNALS:
                        raise ValueError("Circuit resource limit")

                for _, ref in latch_updates:
                    if ref >= signal_count:
                        raise ValueError("Invalid LATCH reference")

                if outs > signal_count - 2:
                    raise ValueError("Insufficient circuit outputs")

                next_state = [False] * state_count
                state_offset = 0
                child_index = 0

                for opcode, refs in elements:
                    if opcode == 0x00:
                        a, b = refs
                        signals.append(not (signals[a] and signals[b]))

                    elif opcode == 0x01:
                        d = refs[0]
                        signals.append(read_bit(state, state_offset))
                        state_offset += 1
                        latch_updates_index = state_offset - 1
                        # Save the D signal for deferred state update.
                        next_state[latch_updates_index] = False

                    else:
                        address, circuit_id, n_outputs, child_refs = refs
                        key = (address, circuit_id)
                        child_ins, child_outs, child_netlist, _ = children[key]

                        child_inputs = pack_bits([
                            signals[index] for index in child_refs
                        ])
                        child_state_count = child_layout[child_index]
                        child_index += 1

                        child_state = pack_bits([
                            read_bit(state, state_offset + i)
                            for i in range(child_state_count)
                        ])

                        child_result = execute(
                            child_ins,
                            child_outs,
                            child_netlist,
                            child_state,
                            child_inputs,
                            recursion_stack + (key,),
                            depth + 1,
                        )

                        child_next, child_outputs = child_result[1], child_result[2]

                        for i in range(child_state_count):
                            next_state[state_offset + i] = read_bit(
                                child_next, i
                            )

                        state_offset += child_state_count

                        for i in range(n_outputs):
                            signals.append(read_bit(child_outputs, i))

                # Apply deferred LATCH updates at their absolute state offsets.
                for latch_offset, signal_ref in latch_updates:
                    next_state[latch_offset] = signals[signal_ref]

                if state_offset != state_count:
                    raise ValueError("State layout mismatch")

                outputs = pack_bits(signals[-outs:])

                return (
                    state_count,
                    pack_bits(next_state),
                    outputs,
                )

            if not canonical_bits(inputs, input_count):
                return False

            result = execute(
                input_count,
                output_count,
                root_netlist,
                initial_state,
                inputs,
                ((root_address, root_circuit_id),),
                0,
            )

            if not canonical_bits(initial_state, result[0]):
                return False
            if result[1] != claimed_state or result[2] != claimed_outputs:
                return False

            # Consensus approves the expected vectors, but deterministic TAP-02
            # execution is the authority that decides whether this circuit
            # actually satisfies them.
            for vector in approved_vectors:
                if not isinstance(vector, dict):
                    return False
                vector_inputs = bytes.fromhex(vector.get("input", ""))
                vector_outputs = bytes.fromhex(vector.get("output", ""))
                vector_state = bytes.fromhex(vector.get("state", ""))
                vector_result = execute(
                    input_count,
                    output_count,
                    root_netlist,
                    vector_state,
                    vector_inputs,
                    ((root_address, root_circuit_id),),
                    0,
                )
                if not canonical_bits(vector_inputs, input_count):
                    return False
                if not canonical_bits(vector_outputs, output_count):
                    return False
                if vector_result[2] != vector_outputs:
                    return False
                if "state" not in vector:
                    if vector_result[0] != 0:
                        return False
                else:
                    if not canonical_bits(vector_state, vector_result[0]):
                        return False
                    expected_next = bytes.fromhex(vector.get("next_state", ""))
                    if vector_result[1] != expected_next:
                        return False

            return True

        except (ValueError, TypeError, OverflowError, IndexError):
            return False
