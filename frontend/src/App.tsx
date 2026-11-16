import { useEffect, useMemo, useState } from "react";
import { createClient } from "genlayer-js";
import { localnet, studionet, testnetAsimov, testnetBradbury } from "genlayer-js/chains";

type NetworkName = "localnet" | "studionet" | "testnetAsimov" | "testnetBradbury";
type Chain = typeof localnet | typeof studionet | typeof testnetAsimov | typeof testnetBradbury;

const networks: Record<NetworkName, Chain> = {
  localnet, studionet, testnetAsimov, testnetBradbury,
};

const contractAddress = import.meta.env.VITE_CONTRACT_ADDRESS as string | undefined;
const initialNetwork = (import.meta.env.VITE_GENLAYER_NETWORK || "localnet") as NetworkName;
const xlayerRpc = (import.meta.env.VITE_XLAYER_RPC_URL || "https://rpc.xlayer.tech") as string;
const xlayerChainId = Number(import.meta.env.VITE_XLAYER_CHAIN_ID || "196");
const xlayerProcessor = (import.meta.env.VITE_XLAYER_PROCESSOR || "0x839bdD6fA7A66416A609A735E11De5411B98574E") as string;
const xlayerCircuitId = Number(import.meta.env.VITE_XLAYER_CIRCUIT_ID || "1");
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
  const [xlayer, setXlayer] = useState<{ block: string; metadata: string; netlist: string; rows: Array<{ input: string; local: string; remote: string; match: boolean }> } | null>(null);

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
    } catch (e) { setStatus("Ready"); setError(e instanceof Error ? e.message : String(e)); }
  }

  async function registerCircuit() {
    setError(""); setStatus("Preparing registration…");
    try {
      if (!account) throw new Error("Connect a wallet before registering a circuit.");
      const address = requireContract();
      const write = { address, functionName: "register_circuit", args: [registration.processor, registration.id, registration.commitment] };
      const hash = await client.writeContract({ ...write, value: 0n });
      setStatus(`Registration submitted: ${hash}`);
    } catch (e) { setStatus("Registration failed"); setError(e instanceof Error ? e.message : String(e)); }
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
    } catch (e) { setStatus("Verification failed"); setError(e instanceof Error ? e.message : String(e)); }
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
    } catch (e) { setStatus("X Layer read failed"); setError(e instanceof Error ? e.message : String(e)); }
  }

  return <main className="shell">
    <header className="topbar"><div className="brand"><span className="mark">CM</span><div><strong>ComputeMesh</strong><small>Deterministic circuit verification</small></div></div><div className="toolbar"><select value={network} onChange={(e) => setNetwork(e.target.value as NetworkName)}>{Object.keys(networks).map((name) => <option key={name}>{name}</option>)}</select><button className="wallet" onClick={connectWallet}>{account ? `${account.slice(0, 6)}…${account.slice(-4)}` : "Connect wallet"}</button></div></header>
    <section className="hero"><div><p className="eyebrow">TAP-02 execution layer</p><h1>Run circuits.<br /><em>Verify the result.</em></h1><p className="lede">ComputeMesh executes NAND, LATCH, and nested REF circuits locally, then asks GenLayer to independently verify the commitment and state transition.</p></div><div className="status-card"><span className="pulse" />{status}<small>{contractAddress ? `Contract ${contractAddress.slice(0, 10)}…` : "No contract address configured"}</small></div></section>
    <section className="grid"><article className="panel"><div className="panel-head"><div><span className="step">01</span><h2>Register a circuit</h2></div><span className="tag">WALLET WRITE</span></div><p className="hint">Registration binds a processor and circuit ID to its SHA-256 commitment. Only the contract administrator may register.</p><Field label="Processor address" value={registration.processor} onChange={(v) => setRegistration({ ...registration, processor: v })} placeholder="40 hex characters" /><div className="two"><Field label="Circuit ID" value={registration.id} onChange={(v) => setRegistration({ ...registration, id: v })} placeholder="16 hex characters" /><Field label="Commitment" value={registration.commitment} onChange={(v) => setRegistration({ ...registration, commitment: v })} placeholder="64 hex characters" /></div><button className="primary" onClick={registerCircuit}>Register circuit <span>↗</span></button></article>
      <article className="panel"><div className="panel-head"><div><span className="step">02</span><h2>Verify execution</h2></div><span className="tag">CONTRACT READ</span></div><p className="hint">Submit the exact packed buffers and child records used by the execution evidence.</p><div className="two"><Field label="Input bits" value={verification.inputs} onChange={(v) => setVerification({ ...verification, inputs: v })} /><Field label="Output bits" value={verification.outputs} onChange={(v) => setVerification({ ...verification, outputs: v })} /></div><Field label="Netlist · hex" value={verification.netlist} onChange={(v) => setVerification({ ...verification, netlist: v })} /><Field label="Commitment · hex" value={verification.commitment} onChange={(v) => setVerification({ ...verification, commitment: v })} placeholder="SHA-256 commitment" /><div className="two"><Field label="Initial state · hex" value={verification.state} onChange={(v) => setVerification({ ...verification, state: v })} /><Field label="Inputs · packed hex" value={verification.inputBits} onChange={(v) => setVerification({ ...verification, inputBits: v })} /></div><div className="two"><Field label="Next state · hex" value={verification.nextState} onChange={(v) => setVerification({ ...verification, nextState: v })} /><Field label="Outputs · packed hex" value={verification.outputBits} onChange={(v) => setVerification({ ...verification, outputBits: v })} /></div><Field label="Child records · JSON array" value={verification.children} onChange={(v) => setVerification({ ...verification, children: v })} /><div className="two"><Field label="Root processor" value={verification.rootProcessor} onChange={(v) => setVerification({ ...verification, rootProcessor: v })} /><Field label="Root circuit ID" value={verification.rootId} onChange={(v) => setVerification({ ...verification, rootId: v })} /></div><button className="primary" onClick={verifyExecution}>Verify execution <span>→</span></button>{result !== null && <div className={`result ${result ? "pass" : "fail"}`}>{result ? "✓ Execution accepted" : "× Execution rejected"}</div>}</article></section>
    <article className="panel xlayer-panel"><div className="panel-head"><div><span className="step">03</span><h2>Live TapeOut comparison</h2></div><span className="tag">X LAYER · 196</span></div><p className="hint">Read-only verification against the existing TapeOut processor on X Layer Mainnet. No X Layer private key is required.</p><div className="xmeta"><span>Processor <code>{xlayerProcessor}</code></span><span>Circuit <code>#{xlayerCircuitId}</code></span><span>RPC <code>{xlayerRpc}</code></span></div><button className="primary" onClick={loadXLayerCircuit}>Retrieve & compare on-chain <span>↗</span></button>{xlayer && <><p className="xmeta">Block <code>{xlayer.block}</code> · Metadata <code>{xlayer.metadata}</code></p><div className="table"><div>Input</div><div>ComputeMesh</div><div>TapeOut eval</div><div>Status</div>{xlayer.rows.map((row) => <div className="table-row" key={row.input}><code>{row.input}</code><code>{row.local}</code><code>{row.remote}</code><strong className={row.match ? "match" : "mismatch"}>{row.match ? "MATCH" : "MISMATCH"}</strong></div>)}</div></>}</article>
    {error && <div className="error">{error}</div>}
    <footer><span>COMPUTEMESH / GENLAYER</span><span>Private keys stay in your wallet · No XLayer dependency</span></footer>
  </main>;
}

function Field({ label, value, onChange, placeholder }: { label: string; value: string; onChange: (value: string) => void; placeholder?: string }) { return <label className="field"><span>{label}</span><input value={value} onChange={(e) => onChange(e.target.value)} placeholder={placeholder} spellCheck={false} /></label>; }

export default App;
