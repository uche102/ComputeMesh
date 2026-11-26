export type TransactionOutcomeKind = "success" | "execution-error" | "pending-timeout" | "network-timeout";

export function classifyTransaction(transaction: unknown): "success" | "execution-error" | "pending";
export function waitForFinalizedTransaction(client: unknown, hash: string): Promise<{
  kind: TransactionOutcomeKind;
  transaction?: unknown;
  pollError?: unknown;
  statusError?: unknown;
}>;
