# { "Depends": "py-genlayer:1jb45aa8ynh2a9c9xn3b7qqh8sm5q93hwfp7jqmwsfhh8jpz09h6" }

from genlayer import *
import hashlib


class ComputeMeshVerifier(gl.Contract):
    administrator: Address
    registered_commitments: TreeMap[str, str]

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

            return (
                result[1] == claimed_state
                and result[2] == claimed_outputs
            )

        except (ValueError, TypeError, OverflowError, IndexError):
            return False
