import { isSuccessful } from "genlayer-js";

export function classifyTransaction(transaction) {
  if (isSuccessful(transaction)) return "success";
  if (transaction?.statusName === "FINALIZED" || transaction?.statusName === "ACCEPTED") {
    return "execution-error";
  }
  return "pending";
}

export async function waitForFinalizedTransaction(client, hash) {
  try {
    const transaction = await client.waitForFinalization({
      hash,
      interval: 5000,
      retries: 120,
      fullTransaction: true,
    });
    return { kind: classifyTransaction(transaction), transaction };
  } catch (pollError) {
    try {
      const transaction = await client.getTransaction({ hash });
      if (transaction) {
        const kind = classifyTransaction(transaction);
        if (kind !== "pending") return { kind, transaction, pollError };
        return { kind: "pending-timeout", transaction, pollError };
      }
      return { kind: "network-timeout", pollError };
    } catch (statusError) {
      return { kind: "network-timeout", pollError, statusError };
    }
  }
}
