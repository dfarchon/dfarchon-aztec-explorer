/**
 * Golden vectors via existing dfpunk-aztec node_modules (no new installs).
 * Usage: node tools/gen_poseidon_vectors.mjs
 */
import { createRequire } from 'module';
import { pathToFileURL } from 'url';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const dfpunkRoot = '/home/stx/develop/df/dfpunk-aztec';
const require = createRequire(path.join(dfpunkRoot, 'package.json'));

// Resolve @aztec-labs/foundation poseidon through pnpm layout
async function loadPoseidon() {
  const foundationPoseidon = path.join(
    dfpunkRoot,
    'node_modules/@aztec-labs/foundation/dest/crypto/poseidon/index.js'
  );
  const { poseidon2Hash } = await import(pathToFileURL(foundationPoseidon).href);
  const frPath = path.join(
    dfpunkRoot,
    'node_modules/@aztec-labs/foundation/dest/curves/bn254/field.js'
  );
  const { Fr } = await import(pathToFileURL(frPath).href);
  return { poseidon2Hash, Fr };
}

const MODULUS = BigInt(
  '21888242871839275222246405745257275088548364400416034343698204186575808495617'
);

function toFr(n) {
  const v = BigInt(n);
  return v < 0n ? v + MODULUS : v;
}

const { poseidon2Hash, Fr } = await loadPoseidon();

async function h(vals) {
  const fields = vals.map((v) => new Fr(typeof v === 'bigint' ? v : BigInt(v)));
  const out = await poseidon2Hash(fields);
  return BigInt(out.toString());
}

const cases = [];

// Alignment vectors
cases.push({
  name: 'hash(1,2)',
  inputs: [1n, 2n],
  got: await h([1n, 2n]),
  expected: BigInt('0x038682aa1cb5ae4e0a3f13da432a95c77c5c111f6f030faf9cad641ce1ed7383'),
});
cases.push({
  name: 'hash(1,2,3,4)',
  inputs: [1n, 2n, 3n, 4n],
  got: await h([1n, 2n, 3n, 4n]),
  expected: BigInt('0x130bf204a32cac1f0ace56c78b731aa3809f06df2731ebcf6b3464a15788b1b9'),
});
cases.push({
  name: 'hash(0,0)',
  inputs: [0n, 0n],
  got: await h([0n, 0n]),
  expected: BigInt('0x0b63a53787021a4a962a452c2921b3663aff1ffd8d5510540f8e659e782956f1'),
});

// Planet hashes matching standalone
const planetCases = [
  [420, -7, -3],
  [420, -225, 198],
  [420, 0, 0],
  [420, -256, 0],
];
for (const [key, x, y] of planetCases) {
  const got = await h([BigInt(key), toFr(x), toFr(y)]);
  cases.push({
    name: `planet(${key},${x},${y})`,
    inputs: [BigInt(key), toFr(x), toFr(y)],
    got,
    expected: null,
  });
}

let failed = 0;
for (const c of cases) {
  if (c.expected != null) {
    const ok = c.got === c.expected;
    console.log(`${ok ? 'OK' : 'FAIL'} ${c.name}`);
    console.log(`  got  0x${c.got.toString(16)}`);
    if (!ok) {
      console.log(`  exp  0x${c.expected.toString(16)}`);
      failed++;
    }
  } else {
    console.log(`VEC ${c.name}`);
    console.log(`  hex 0x${c.got.toString(16)}`);
    console.log(`  dec ${c.got.toString(10)}`);
  }
}

// Cross-check first standalone planet decimal hash
const standaloneFirst = BigInt(
  '80546497555981120645940630931509083029059134353157235835168648385027722'
);
const planetNeg225 = await h([420n, toFr(-225), toFr(198)]);
const match = planetNeg225 === standaloneFirst;
console.log(
  `${match ? 'OK' : 'FAIL'} standalone first planet (-225,198) matches bb.js`
);
if (!match) {
  console.log(`  gpu/cpu ${standaloneFirst.toString(16)}`);
  console.log(`  bb.js   ${planetNeg225.toString(16)}`);
  failed++;
}

process.exit(failed === 0 ? 0 : 1);
