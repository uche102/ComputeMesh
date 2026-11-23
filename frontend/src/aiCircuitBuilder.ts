export type CircuitProposal = {
  description: string;
  inputCount: number;
  outputCount: number;
  netlistHex: string;
  purpose: string;
  inputs: string[];
  outputs: string[];
  behaviour: string;
  testCases: Array<{ input: string; output: string }>;
  explanation: string;
  needsClarification: boolean;
  clarification?: string;
};

// These are deliberately conservative starter designs. The proposal is never
// treated as valid until the C++/GenLayer verification stages accept it.
export function proposeCircuit(description: string): CircuitProposal {
  const text = description.trim();
  const lower = text.toLowerCase();
  if (!text) {
    return emptyProposal(text, "Describe the inputs, outputs, and logical condition.");
  }
  if (lower.includes("xor") || lower.includes("different") || lower.includes("not equal")) {
    // XOR from four NAND gates. Signals 0/1 are constants; inputs are 2/3.
    const netlistHex = [
      "00", "000002", "000003", // n1 = NAND(a,b), signal 4
      "00", "000002", "000004", // n2 = NAND(a,n1), signal 5
      "00", "000003", "000004", // n3 = NAND(b,n1), signal 6
      "00", "000005", "000006", // out = NAND(n2,n3), signal 7
    ].join("");
    return {
      description: text, inputCount: 2, outputCount: 1, netlistHex,
      purpose: "Return 1 exactly when the two binary inputs differ.",
      inputs: ["a", "b"], outputs: ["different"],
      behaviour: "different = a XOR b",
      testCases: [{ input: "00", output: "00" }, { input: "01", output: "01" }, { input: "02", output: "01" }, { input: "03", output: "00" }],
      explanation: "Four NAND gates implement XOR without unsupported TAP-02 operations.",
      needsClarification: false,
    };
  }
  if (lower.includes("nand") && (lower.includes("two") || lower.includes("2"))) {
    return {
      description: text, inputCount: 2, outputCount: 1, netlistHex: "00000002000003",
      purpose: "Return the NAND of two binary inputs.", inputs: ["a", "b"], outputs: ["result"],
      behaviour: "result = NOT(a AND b)",
      testCases: [{ input: "00", output: "01" }, { input: "01", output: "01" }, { input: "02", output: "01" }, { input: "03", output: "00" }],
      explanation: "One NAND gate reads input signals 2 and 3 and produces the final output.",
      needsClarification: false,
    };
  }
  return emptyProposal(text, "Should this be NAND, XOR, a latch, or a nested REF circuit? Also specify the number and names of inputs and outputs.");
}

function emptyProposal(description: string, clarification: string): CircuitProposal {
  return { description, inputCount: 0, outputCount: 0, netlistHex: "", purpose: "", inputs: [], outputs: [], behaviour: "", testCases: [], explanation: "", needsClarification: true, clarification };
}
