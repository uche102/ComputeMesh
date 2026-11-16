# { "Depends": "py-genlayer:1jb45aa8ynh2a9c9xn3b7qqh8sm5q93hwfp7jqmwsfhh8jpz09h6" }

from genlayer import *
import hashlib


class ComputeMeshVerifier(gl.Contract):

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
    ) -> bool:

        DOMAIN = b"ComputeMesh/TAP-02/Circuit/v1"

        if input_count < 0 or input_count > 65536:
            return False

        if output_count <= 0 or output_count > 65536:
            return False

        try:
            netlist = bytes.fromhex(netlist_hex)
            commitment = bytes.fromhex(commitment_hex)
            initial_state = bytes.fromhex(initial_state_hex)
            inputs = bytes.fromhex(inputs_hex)
            claimed_next_state = bytes.fromhex(
                claimed_next_state_hex
            )
            claimed_outputs = bytes.fromhex(
                claimed_outputs_hex
            )
        except ValueError:
            return False

        if len(commitment) != 32:
            return False

        # Reconstruct the exact C++ commitment preimage.
        preimage = (
            DOMAIN
            + input_count.to_bytes(4, "big")
            + output_count.to_bytes(4, "big")
            + netlist
        )

        computed_commitment = hashlib.sha256(
            preimage
        ).digest()

        if computed_commitment != commitment:
            return False

        signals = [False, True]

        # Read packed input bits, LSB first.
        def read_bit(data, index):
            byte_index = index // 8

            if byte_index >= len(data):
                return False

            return bool(
                (data[byte_index] >> (index % 8)) & 1
            )

        def pack_bits(bits):
            result = bytearray((len(bits) + 7) // 8)

            for index, value in enumerate(bits):
                if value:
                    result[index // 8] |= 1 << (index % 8)

            return bytes(result)

        for index in range(input_count):
            signals.append(read_bit(inputs, index))

        state_count = 0
        elements = []
        offset = 0

        # Parse canonical TAP-02 records.
        while offset < len(netlist):

            opcode = netlist[offset]
            offset += 1

            if opcode == 0x00:  # NAND
                if offset + 6 > len(netlist):
                    return False

                a = int.from_bytes(
                    netlist[offset:offset + 3], "big"
                )
                b = int.from_bytes(
                    netlist[offset + 3:offset + 6], "big"
                )
                offset += 6

                elements.append((opcode, (a, b)))

            elif opcode == 0x01:  # LATCH
                if offset + 3 > len(netlist):
                    return False

                d = int.from_bytes(
                    netlist[offset:offset + 3], "big"
                )
                offset += 3

                elements.append((opcode, (d,)))
                state_count += 1

            else:
                # REF and unknown opcodes are not supported yet.
                return False

        expected_state_size = (state_count + 7) // 8
        expected_output_size = (output_count + 7) // 8

        if len(claimed_next_state) != expected_state_size:
            return False

        if len(claimed_outputs) != expected_output_size:
            return False

        # Execute gates independently.
        latch_updates = []
        next_state_bits = [False] * state_count
        state_offset = 0

        for opcode, refs in elements:

            if opcode == 0x00:
                a, b = refs

                if a >= len(signals) or b >= len(signals):
                    return False

                signals.append(not (signals[a] and signals[b]))

            elif opcode == 0x01:
                d = refs[0]

                signals.append(
                    read_bit(initial_state, state_offset)
                )

                latch_updates.append(
                    (state_offset, d)
                )

                state_offset += 1

        # Apply deferred LATCH updates.
        for state_index, signal_index in latch_updates:

            if signal_index >= len(signals):
                return False

            next_state_bits[state_index] = signals[signal_index]

        if len(signals) < 2 + input_count + output_count:
            return False

        # The final outputCount signals are circuit outputs.
        output_bits = signals[-output_count:]

        computed_outputs = pack_bits(output_bits)
        computed_next_state = pack_bits(next_state_bits)

        return (
            computed_commitment == commitment
            and computed_outputs == claimed_outputs
            and computed_next_state == claimed_next_state
        )