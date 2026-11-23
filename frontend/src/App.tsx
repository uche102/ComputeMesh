import { useEffect, useMemo, useState } from "react";
import { createClient } from "genlayer-js";
import { localnet, studionet, testnetAsimov, testnetBradbury } from "genlayer-js/chains";
import { proposeCircuit, type CircuitProposal } from "./aiCircuitBuilder";

type NetworkName = "localnet" | "studionet" | "testnetAsimov" | "testnetBradbury";
type Chain = typeof localnet | typeof studionet | typeof testnetAsimov | typeof testnetBradbury;

function formatError(error: unknown): string {
  if (error instanceof Error) return error.message;
  if (typeof error === "string") return error;
  if (error && typeof error === "object") {
    const value = error as { message?: unknown; shortMessage?: unknown; details?: unknown };
    if (typeof value.shortMessage === "string") return value.shortMessage;
    if (typeof value.message === "string") return value.message;
    if (typeof value.details === "string") return value.details;
    try { return JSON.stringify(error); } catch { return "The request failed."; }
  }
  return "The request failed.";
}

const networks: Record<NetworkName, Chain> = {
  localnet, studionet, testnetAsimov, testnetBradbury,
};

const contractAddress = import.meta.env.VITE_CONTRACT_ADDRESS as string | undefined;
const initialNetwork = (import.meta.env.VITE_GENLAYER_NETWORK || "localnet") as NetworkName;
const xlayerRpc = (import.meta.env.VITE_XLAYER_RPC_URL || "https://rpc.xlayer.tech") as string;
const xlayerChainId = Number(import.meta.env.VITE_XLAYER_CHAIN_ID || "196");
const xlayerProcessor = (import.meta.env.VITE_XLAYER_PROCESSOR || "0x839bdD6fA7A66416A609A735E11De5411B98574E") as string;
const xlayerCircuitId = Number(import.meta.env.VITE_XLAYER_CIRCUIT_ID || "1");
const defaultRegisteredProcessor = xlayerProcessor.replace(/^0x/i, "").toLowerCase();
const defaultRegisteredCircuitId = Number.isSafeInteger(xlayerCircuitId) && xlayerCircuitId > 0
  ? xlayerCircuitId.toString(16).padStart(16, "0")
  : "";
const xlayerConfigError = (() => {
  try {
    new URL(xlayerRpc);
  } catch {
    return "VITE_XLAYER_RPC_URL is not a valid URL.";
  }
  if (!Number.isInteger(xlayerChainId) || xlayerChainId !== 196) return "VITE_XLAYER_CHAIN_ID must be 196.";
  if (!/^0x[0-9a-fA-F]{40}$/.test(xlayerProcessor)) return "VITE_XLAYER_PROCESSOR is not a valid address.";
  if (!Number.isSafeInteger(xlayerCircuitId) || xlayerCircuitId < 1) return "VITE_XLAYER_CIRCUIT_ID must be a positive integer.";
  return "";
})();

const xword = (value: number | bigint) => BigInt(value).toString(16).padStart(64, "0");
async function xrpc(method: string, params: unknown[]) {
  const response = await fetch(xlayerRpc, { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ jsonrpc: "2.0", id: 1, method, params }) });
  if (!response.ok) throw new Error(`X Layer RPC HTTP ${response.status}`);
  const body = await response.json() as { result?: string; error?: { message?: string } };
  if (body.error) throw new Error(`X Layer RPC: ${body.error.message || "request failed"}`);
  if (typeof body.result !== "string") throw new Error("X Layer RPC returned no result");
  return body.result;
}
async function xcall(to: string, data: string, block: string) { return xrpc("eth_call", [{ to, data }, block]); }
function dynamicBytesResult(value: string) { const body = value.slice(2); const offset = Number(BigInt(`0x${body.slice(0, 64)}`)) * 2; const length = Number(BigInt(`0x${body.slice(offset, offset + 64)}`)); return body.slice(offset + 64, offset + 64 + length * 2); }
function localTap02(netlistHex: string, input: number) { const bytes = netlistHex.match(/../g)?.map((value) => Number.parseInt(value, 16)) || []; const signals = [false, true, Boolean(input & 1), Boolean(input & 2)]; let offset = 0; while (offset < bytes.length) { const opcode = bytes[offset++]; if (opcode === 0) { const a = (bytes[offset] << 16) | (bytes[offset + 1] << 8) | bytes[offset + 2]; const b = (bytes[offset + 3] << 16) | (bytes[offset + 4] << 8) | bytes[offset + 5]; offset += 6; signals.push(!(signals[a] && signals[b])); } else throw new Error("The browser demo currently supports the retrieved combinational NAND circuit only."); } return signals[signals.length - 1] ? "01" : "00"; }

function App() {
  const [network, setNetwork] = useState<NetworkName>(networks[initialNetwork] ? initialNetwork : "localnet");
  const [account, setAccount] = useState<string>("");
  const [status, setStatus] = useState("Ready");
  const [error, setError] = useState("");
  const [registration, setRegistration] = useState({ processor: "", id: "", commitment: "" });
  const [verification, setVerification] = useState({
    inputs: "2", outputs: "1", netlist: "00000002000003", commitment: "",
    state: "", inputBits: "01", nextState: "", outputBits: "01", children: "[]",
    rootProcessor: "00".repeat(20), rootId: "0000000000000001",
  });
  const [result, setResult] = useState<boolean | null>(null);
  const [registeredCircuit, setRegisteredCircuit] = useState({ processor: defaultRegisteredProcessor, circuitId: defaultRegisteredCircuitId, commitment: null as string | null });
  const [xlayer, setXlayer] = useState<{ block: string; metadata: string; netlist: string; rows: Array<{ input: string; local: string; remote: string; match: boolean }> } | null>(null);
  const [builderDescription, setBuilderDescription] = useState("");
  const [proposal, setProposal] = useState<CircuitProposal | null>(null);
  const [proposalAccepted, setProposalAccepted] = useState(false);
  const [consensus, setConsensus] = useState<{ status: string; hash: string }>({ status: "Not submitted", hash: "" });

  const chain = networks[network];
  const client = useMemo(() => createClient({ chain, account: account ? account as `0x${string}` : undefined, provider: window.ethereum }), [chain, account]);

  useEffect(() => {
    const onAccounts = (accounts: unknown) => setAccount(Array.isArray(accounts) && typeof accounts[0] === "string" ? accounts[0] : "");
    window.ethereum?.on?.("accountsChanged", onAccounts);
    return () => window.ethereum?.removeListener?.("accountsChanged", onAccounts);
  }, []);

  const requireContract = () => {
    if (!contractAddress) throw new Error("Set VITE_CONTRACT_ADDRESS before using the contract.");
    if (!/^0x[0-9a-fA-F]{40}$/.test(contractAddress)) throw new Error("VITE_CONTRACT_ADDRESS is not a valid address.");
    return contractAddress as `0x${string}`;
  };

  async function connectWallet() {
    setError(""); setStatus("Connecting wallet…");
    try {
      if (!window.ethereum) throw new Error("No EIP-1193 wallet was found. Install or open a supported wallet.");
      const accounts = await window.ethereum.request({ method: "eth_requestAccounts" }) as string[];
      if (!accounts[0]) throw new Error("The wallet returned no account.");
      setAccount(accounts[0]);
      await createClient({ chain, account: accounts[0] as `0x${string}`, provider: window.ethereum }).connect(network);
      setStatus("Wallet connected");
    } catch (e) { setStatus("Ready"); setError(formatError(e)); }
  }

  async function registerCircuit() {
    setError(""); setStatus("Preparing registration…");
    try {
      if (!account) throw new Error("Connect a wallet before registering a circuit.");
      const address = requireContract();
      const write = { address, functionName: "register_circuit", args: [registration.processor, registration.id, registration.commitment] };
      const hash = await client.writeContract({ ...write, value: 0n });
      setStatus(`Registration submitted: ${hash}`);
    } catch (e) { setStatus("Registration failed"); setError(formatError(e)); }
  }

  async function verifyExecution() {
    setError(""); setResult(null); setStatus("Verifying execution…");
    try {
      const address = requireContract();
      const children = JSON.parse(verification.children) as string[];
      if (!children.every((child) => typeof child === "string")) throw new Error("Children must be a JSON array of strings.");
      const value = await client.readContract({ address, functionName: "verify_execution", args: [
        Number(verification.inputs), Number(verification.outputs), verification.netlist,
        verification.commitment, verification.state, verification.inputBits,
        verification.nextState, verification.outputBits, children,
        verification.rootProcessor, verification.rootId,
      ] });
      setResult(Boolean(value)); setStatus("Verification complete");
    } catch (e) { setStatus("Verification failed"); setError(formatError(e)); }
  }

  async function checkRegisteredCircuit() {
    setError(""); setStatus("Checking registered circuit…");
    setRegisteredCircuit((current) => ({ ...current, commitment: null }));
    try {
      const address = requireContract();
      const processor = registeredCircuit.processor.trim();
      const circuitId = registeredCircuit.circuitId.trim();
      if (!/^[0-9a-fA-F]{40}$/.test(processor)) throw new Error("Processor address must be exactly 40 hexadecimal characters without 0x.");
      if (!/^[0-9a-fA-F]{16}$/.test(circuitId)) throw new Error("Circuit ID must be exactly 16 hexadecimal characters.");
      const commitment = await client.readContract({ address, functionName: "get_registered_commitment", args: [processor.toLowerCase(), circuitId.toLowerCase()] });
      if (typeof commitment !== "string") throw new Error("The contract returned an invalid commitment value.");
      setRegisteredCircuit((current) => ({ ...current, commitment }));
      setStatus("Registered circuit lookup complete");
    } catch (e) { setStatus("Registered circuit lookup failed"); setError(formatError(e)); }
  }

  async function loadXLayerCircuit() {
    setError(""); setStatus("Reading X Layer TapeOut circuit…"); setXlayer(null);
    try {
      if (xlayerConfigError) throw new Error(xlayerConfigError);
      const chainId = await xrpc("eth_chainId", []);
      if (Number(BigInt(chainId)) !== xlayerChainId) throw new Error(`Expected X Layer chain ${xlayerChainId}, received ${Number(BigInt(chainId))}.`);
      const block = await xrpc("eth_blockNumber", []);
      const blockTag = block;
      const info = await xcall(xlayerProcessor, `0x084d60f1${xword(xlayerCircuitId)}`, blockTag);
      const values = info.slice(2).match(/.{64}/g)?.map((value) => Number(BigInt(`0x${value}`))) || [];
      if (values.length !== 4) throw new Error("Invalid TapeOut circuit metadata.");
      const netlist = dynamicBytesResult(await xcall(xlayerProcessor, `0x3fc4be56${xword(xlayerCircuitId)}`, blockTag));
      const rows = [];
      for (let input = 0; input < 4; input += 1) {
        const encoded = `0x934d06ea${xword(xlayerCircuitId)}${xword(64)}${xword(1)}${input.toString(16).padStart(2, "0")}${"00".repeat(31)}`;
        const remote = dynamicBytesResult(await xcall(xlayerProcessor, encoded, blockTag));
        const local = localTap02(netlist, input);
        rows.push({ input: input.toString(2).padStart(2, "0"), local, remote, match: local === remote });
      }
      setXlayer({ block: Number(BigInt(block)).toString(), metadata: `${values[0]} in · ${values[1]} out · ${values[2]} state · ${values[3]} gates`, netlist, rows });
      setStatus("X Layer comparison complete");
    } catch (e) { setStatus("X Layer read failed"); setError(formatError(e)); }
  }

  function generateProposal() {
    setError(""); setProposalAccepted(false); setConsensus({ status: "Not submitted", hash: "" });
    const next = proposeCircuit(builderDescription);
    setProposal(next);
    if (!next.needsClarification) {
      setVerification((current) => ({ ...current, inputs: String(next.inputCount), outputs: String(next.outputCount), netlist: next.netlistHex, inputBits: "00", outputBits: "00" }));
    }
    setStatus("AI-assisted proposal ready for review");
  }

  async function submitSpecificationConsensus() {
    setError("");
    try {
      if (!proposal || proposal.needsClarification) throw new Error("Resolve the clarification request before submitting.");
      if (!proposalAccepted) throw new Error("Review and accept the proposed circuit first.");
      if (!account) throw new Error("Connect a GenLayer wallet before requesting consensus.");
      const address = requireContract();
      setConsensus({ status: "Submitting to GenLayer validators…", hash: "" });
      const prefix = new TextEncoder().encode("ComputeMesh/TAP-02/Circuit/v1");
      const preimage = new Uint8Array(prefix.length + 4 + 4 + proposal.netlistHex.length / 2);
      preimage.set(prefix, 0);
      new DataView(preimage.buffer).setUint32(prefix.length, proposal.inputCount);
      new DataView(preimage.buffer).setUint32(prefix.length + 4, proposal.outputCount);
      preimage.set(Uint8Array.from(proposal.netlistHex.match(/../g)!.map((byte) => Number.parseInt(byte, 16))), prefix.length + 8);
      const digest = await crypto.subtle.digest("SHA-256", preimage);
      const commitment = Array.from(new Uint8Array(digest)).map((byte) => byte.toString(16).padStart(2, "0")).join("");
      setVerification((current) => ({ ...current, commitment }));
      const draft = JSON.stringify({ purpose: proposal.purpose, inputs: proposal.inputs, outputs: proposal.outputs, behaviour: proposal.behaviour, test_cases: proposal.testCases, assumptions: [], ambiguities: [], ambiguous: false });
      const write = await client.writeContract({ address, functionName: "approve_specification", args: [proposal.description, commitment, proposal.inputCount, proposal.outputCount, proposal.netlistHex, draft], value: 0n });
      setConsensus({ status: "Consensus transaction submitted", hash: String(write) });
    } catch (e) { setConsensus({ status: "Consensus failed", hash: "" }); setError(formatError(e)); }
  }

  return <main className="shell">
    <header className="topbar"><div className="brand"><span className="mark">CM</span><div><strong>ComputeMesh</strong><small>Deterministic circuit verification</small></div></div><div className="toolbar"><select value={network} onChange={(e) => setNetwork(e.target.value as NetworkName)}>{Object.keys(networks).map((name) => <option key={name}>{name}</option>)}</select><button className="wallet" onClick={connectWallet}>{account ? `${account.slice(0, 6)}…${account.slice(-4)}` : "Connect wallet"}</button></div></header>
    <section className="hero"><div><p className="eyebrow">TAP-02 execution layer</p><h1>Run circuits.<br /><em>Verify the result.</em></h1><p className="lede">ComputeMesh executes NAND, LATCH, and nested REF circuits locally, then asks GenLayer to independently verify the commitment and state transition.</p></div><div className="status-card"><span className="pulse" />{status}<small>{contractAddress ? `Contract ${contractAddress.slice(0, 10)}…` : "No contract address configured"}</small></div></section>
    <article className="panel builder-panel"><div className="panel-head"><div><span className="step">00</span><h2>AI Circuit Builder</h2></div><span className="tag">REVIEW REQUIRED</span></div><p className="hint">Describe a circuit in plain language. The starter AI assistant proposes only supported TAP-02 operations; ambiguous requests are stopped for clarification.</p><Field label="Describe your circuit" value={builderDescription} onChange={setBuilderDescription} placeholder="e.g. return 1 when two inputs are different" /><button className="primary" onClick={generateProposal}>Generate proposal <span>✦</span></button>{proposal && <div className="proposal"><div className="result">{proposal.needsClarification ? `Clarification needed: ${proposal.clarification}` : <><strong>Proposed circuit — review before acceptance</strong><p>{proposal.purpose}</p><p><code>{proposal.behaviour}</code></p><p>{proposal.explanation}</p><code className="netlist">{proposal.netlistHex}</code><div className="table proposal-table"><div>Input</div><div>Expected</div>{proposal.testCases.map((test) => <div className="table-row" key={test.input}><code>{test.input}</code><code>{test.output}</code></div>)}</div><label className="review"><input type="checkbox" checked={proposalAccepted} onChange={(e) => setProposalAccepted(e.target.checked)} /> I reviewed this AI-generated proposal</label><button className="primary" disabled={!proposalAccepted} onClick={submitSpecificationConsensus}>Submit specification for GenLayer consensus <span>→</span></button><div className="result">Consensus: {consensus.status}{consensus.hash && <><br /><code>{consensus.hash}</code></>}</div></>}</div></div>}</article>
    <section className="grid"><article className="panel"><div className="panel-head"><div><span className="step">01</span><h2>Register a circuit</h2></div><span className="tag">WALLET WRITE</span></div><p className="hint">Registration binds a processor and circuit ID to its SHA-256 commitment. Only the contract administrator may register.</p><Field label="Processor address" value={registration.processor} onChange={(v) => setRegistration({ ...registration, processor: v })} placeholder="40 hex characters" /><div className="two"><Field label="Circuit ID" value={registration.id} onChange={(v) => setRegistration({ ...registration, id: v })} placeholder="16 hex characters" /><Field label="Commitment" value={registration.commitment} onChange={(v) => setRegistration({ ...registration, commitment: v })} placeholder="64 hex characters" /></div><button className="primary" onClick={registerCircuit}>Register circuit <span>↗</span></button></article>
      <article className="panel"><div className="panel-head"><div><span className="step">02</span><h2>Verify execution</h2></div><span className="tag">CONTRACT READ</span></div><p className="hint">Submit the exact packed buffers and child records used by the execution evidence.</p><div className="two"><Field label="Input bits" value={verification.inputs} onChange={(v) => setVerification({ ...verification, inputs: v })} /><Field label="Output bits" value={verification.outputs} onChange={(v) => setVerification({ ...verification, outputs: v })} /></div><Field label="Netlist · hex" value={verification.netlist} onChange={(v) => setVerification({ ...verification, netlist: v })} /><Field label="Commitment · hex" value={verification.commitment} onChange={(v) => setVerification({ ...verification, commitment: v })} placeholder="SHA-256 commitment" /><div className="two"><Field label="Initial state · hex" value={verification.state} onChange={(v) => setVerification({ ...verification, state: v })} /><Field label="Inputs · packed hex" value={verification.inputBits} onChange={(v) => setVerification({ ...verification, inputBits: v })} /></div><div className="two"><Field label="Next state · hex" value={verification.nextState} onChange={(v) => setVerification({ ...verification, nextState: v })} /><Field label="Outputs · packed hex" value={verification.outputBits} onChange={(v) => setVerification({ ...verification, outputBits: v })} /></div><Field label="Child records · JSON array" value={verification.children} onChange={(v) => setVerification({ ...verification, children: v })} /><div className="two"><Field label="Root processor" value={verification.rootProcessor} onChange={(v) => setVerification({ ...verification, rootProcessor: v })} /><Field label="Root circuit ID" value={verification.rootId} onChange={(v) => setVerification({ ...verification, rootId: v })} /></div><button className="primary" onClick={verifyExecution}>Verify execution <span>→</span></button>{result !== null && <div className={`result ${result ? "pass" : "fail"}`}>{result ? "✓ Execution accepted" : "× Execution rejected"}</div>}</article></section>
    <article className="panel registered-panel"><div className="panel-head"><div><span className="step">03</span><h2>Check Registered Circuit</h2></div><span className="tag">READ ONLY</span></div><p className="hint">Look up the commitment registered in the deployed GenLayer contract. This does not send a transaction.</p><div className="two"><Field label="Processor address · 40 hex characters" value={registeredCircuit.processor} onChange={(v) => setRegisteredCircuit({ ...registeredCircuit, processor: v, commitment: null })} placeholder="without 0x" /><Field label="Circuit ID · 16 hex characters" value={registeredCircuit.circuitId} onChange={(v) => setRegisteredCircuit({ ...registeredCircuit, circuitId: v, commitment: null })} placeholder="zero-padded hex" /></div><button className="primary" onClick={checkRegisteredCircuit}>Check registration <span>⌕</span></button>{registeredCircuit.commitment !== null && <div className={`result ${registeredCircuit.commitment ? "pass" : "fail"}`}>{registeredCircuit.commitment ? <><span>Registered commitment</span><code className="commitment-value">{registeredCircuit.commitment}</code></> : "No registration found for this processor and circuit ID."}</div>}</article>
    <article className="panel xlayer-panel"><div className="panel-head"><div><span className="step">04</span><h2>Live TapeOut comparison</h2></div><span className="tag">X LAYER · 196</span></div><p className="hint">Read-only verification against the existing TapeOut processor on X Layer Mainnet. No X Layer private key is required.</p><div className="xmeta"><span>Processor <code>{xlayerProcessor}</code></span><span>Circuit <code>#{xlayerCircuitId}</code></span><span>RPC <code>{xlayerRpc}</code></span></div><button className="primary" onClick={loadXLayerCircuit}>Retrieve & compare on-chain <span>↗</span></button>{xlayer && <><p className="xmeta">Block <code>{xlayer.block}</code> · Metadata <code>{xlayer.metadata}</code></p><div className="table"><div>Input</div><div>ComputeMesh</div><div>TapeOut eval</div><div>Status</div>{xlayer.rows.map((row) => <div className="table-row" key={row.input}><code>{row.input}</code><code>{row.local}</code><code>{row.remote}</code><strong className={row.match ? "match" : "mismatch"}>{row.match ? "MATCH" : "MISMATCH"}</strong></div>)}</div></>}</article>
    {error && <div className="error">{error}</div>}
  </main>;
}

function Field({ label, value, onChange, placeholder }: { label: string; value: string; onChange: (value: string) => void; placeholder?: string }) { return <label className="field"><span>{label}</span><input value={value} onChange={(e) => onChange(e.target.value)} placeholder={placeholder} spellCheck={false} /></label>; }

export default App;
