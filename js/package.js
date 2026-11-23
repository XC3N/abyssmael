// Reading a Felucca/M-VAVE .fwsc package in the browser: the same as fm1_install.py's product_of / logical_image.
export const BLOCKS = 20, BLK = 0x30, KEEP = 0x2F;

// the package identity ("FM-1_5800"): one marker byte after each of the first 20 blocks
export function productOf(raw) {
  if (raw.length < BLOCKS * BLK) throw Object.assign(new Error("not an FM-1 package (too short)"), { code: "badpkg" });
  let s = "";
  for (let i = 0; i < BLOCKS; i++) {
    const m = raw[i * BLK + KEEP];
    if (m !== 0x7D) s += String.fromCharCode((m - i - 1) & 0xFF);
  }
  return s;
}

// the image the device reads during an update: the package without the 20 marker bytes
export function logicalImage(raw) {
  const out = new Uint8Array(raw.length - BLOCKS);
  let o = 0;
  for (let i = 0; i < BLOCKS; i++) { out.set(raw.subarray(i * BLK, i * BLK + KEEP), o); o += KEEP; }
  out.set(raw.subarray(BLOCKS * BLK), o);
  return out;
}

export async function sha256Hex(buf) {
  const d = await crypto.subtle.digest("SHA-256", buf);
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// M-VAVE's own firmware (not a Felucca one): same test as fm1_install.py is_stock
export const isStock = (product) => /^FM-1_0\d\d$/.test(product) && product !== "FM-1_099";

// Open a package: checks its identity, and (given `expect`) the manifest's size, SHA-256 and identity.
// -> { raw, image, product, sha256 }. Throws errors with code "badpkg" or "hash".
export async function openPackage(buf, expect = {}) {
  const raw = new Uint8Array(buf);
  const sha256 = await sha256Hex(raw);
  if (expect.sha256 && sha256 !== expect.sha256.toLowerCase())
    throw Object.assign(new Error(`SHA-256 mismatch: the file is ${sha256}, the list says ${expect.sha256}`), { code: "hash" });
  const product = productOf(raw);
  if (!/^[^_]+_\d+$/.test(product)) throw Object.assign(new Error(`not an FM-1 package (identity "${product}")`), { code: "badpkg" });
  if (expect.product && product !== expect.product)
    throw Object.assign(new Error(`the package is ${product}, the list says ${expect.product}`), { code: "badpkg" });
  return { raw, image: logicalImage(raw), product, sha256 };
}
