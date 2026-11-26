import assert from "node:assert/strict";
import test from "node:test";
import { classifyTransaction, waitForFinalizedTransaction } from "./transactionLifecycle.mjs";

const successful = { statusName: "FINALIZED", txExecutionResultName: "FINISHED_WITH_RETURN" };
const failed = { statusName: "FINALIZED", txExecutionResultName: "FINISHED_WITH_ERROR" };

test("classifies finalized successful execution separately from finalized errors", () => {
  assert.equal(classifyTransaction(successful), "success");
  assert.equal(classifyTransaction(failed), "execution-error");
  assert.equal(classifyTransaction({ statusName: "COMMITTING" }), "pending");
});

test("reconciles a delayed finalization after the polling window", async () => {
  const client = {
    waitForFinalization: async () => { throw new Error("poll timeout"); },
    getTransaction: async () => successful,
  };
  const result = await waitForFinalizedTransaction(client, "0x" + "1".repeat(64));
  assert.equal(result.kind, "success");
  assert.equal(result.transaction, successful);
});

test("keeps an unresolved transaction pending after timeout", async () => {
  const transaction = { statusName: "COMMITTING" };
  const client = {
    waitForFinalization: async () => { throw new Error("poll timeout"); },
    getTransaction: async () => transaction,
  };
  const result = await waitForFinalizedTransaction(client, "0x" + "2".repeat(64));
  assert.equal(result.kind, "pending-timeout");
});
