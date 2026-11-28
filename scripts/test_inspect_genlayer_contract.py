import contextlib
import io
import os
import unittest
from unittest.mock import patch

import inspect_genlayer_contract as inspector


class FakeEth:
    chain_id = 61999
    block_number = 123


class FakeClient:
    class Chain:
        id = 61999

    chain = Chain()
    eth = FakeEth()

    def get_balance(self, address):
        return 0

    def read_contract(self, address, function_name, args=None):
        return {"get_registered_owner": "", "get_registered_commitment": "", "get_approved_specification": ""}[function_name]


class InspectionTests(unittest.TestCase):
    def setUp(self):
        self.env = {
            "GENLAYER_RPC_URL": "https://studio.genlayer.com/api",
            "GENLAYER_NETWORK": "studionet",
        }

    def run_main(self, extra=None):
        values = dict(self.env)
        values.update(extra or {})
        with patch.dict(os.environ, values, clear=True), patch.object(
            inspector, "create_client", return_value=FakeClient()
        ), patch.object(inspector, "load_dotenv"), patch.object(
            inspector, "check_rpc_endpoint", return_value=True
        ):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = inspector.main()
            return result, output.getvalue()

    def test_successful_rpc_configuration_without_wallet_or_circuit(self):
        result, output = self.run_main()
        self.assertEqual(result, 0)
        self.assertIn("config_endpoint=loaded", output)
        self.assertIn("circuit_lookup=SKIPPED", output)
        self.assertNotIn("circuit_lookup=PASS", output)

    def test_missing_circuit_identifiers_are_skipped(self):
        result, output = self.run_main({"GENLAYER_WALLET_A_PRIVATE_KEY": ""})
        self.assertEqual(result, 0)
        self.assertIn("SKIPPED", output)

    def test_partial_circuit_identifiers_are_rejected(self):
        with self.assertRaises(RuntimeError):
            self.run_main({"COMPUTEMESH_PROCESSOR_HEX": "11" * 20})

    def test_wallet_balances_are_optional(self):
        result, output = self.run_main()
        self.assertEqual(result, 0)
        self.assertIn("wallet_a=not configured", output)
        self.assertNotIn("wallet_a_balance_wei", output)

    def test_rpc_configuration_is_checked_before_reads(self):
        with patch.dict(os.environ, self.env, clear=True), patch.object(
            inspector, "create_client", return_value=FakeClient()
        ), patch.object(inspector, "load_dotenv"), patch.object(
            inspector, "check_rpc_endpoint", return_value=True
        ) as probe:
            inspector.main()
        probe.assert_called_once_with(self.env["GENLAYER_RPC_URL"])

    def test_read_only_path_does_not_call_write_apis(self):
        with patch.object(inspector, "write_contract", create=True) as write:
            result, _ = self.run_main()
        self.assertEqual(result, 0)
        write.assert_not_called()


if __name__ == "__main__":
    unittest.main()
