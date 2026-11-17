// Runs the original world generator (quality.js, math.js, world.js and the generation part of main.js) on the
// density grid the C++ port computed, and writes the same files for comparison.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
const [src, dumpDir, out] = process.argv.slice(2);
mkdirSync(out, { recursive: true });
const main = readFileSync(`${src}/main.js`, 'utf8');
const cut = (a, b) => { const i = main.indexOf(a), j = main.indexOf(b, i); if (i < 0 || j < 0) throw new Error('cut ' + a); return main.slice(i, j); };
const files = ['quality.js', 'math.js', 'world.js'].map((f) => readFileSync(`${src}/${f}`, 'utf8')).join('\n');
const body = `
const matchMedia = () => ({ matches: false }); const navigator = { deviceMemory: 16 }; const localStorage = {};
${files}

let D = new Float32Array(readFileSync(dumpDir + '/D.bin').buffer.slice(0));
{
  const S = NX * NY, R = new Uint8Array(D.length), O = [1, -1, NX, -NX, S, -S], b = new Uint8Array(NX), L = [];
  for (let i = 0; i < D.length;) { b.fill(1); for (let y = 0; y < NY; y++) for (let x = 0; x < NX; x++, i++) if (D[i] <= 0) b[x] = 0; else if (b[x]) R[i] = 1; else L.push(i); }
  for (let k = 1; k; L.reverse()) { k = 0; for (const i of L) if (!R[i] && O.some((o) => R[i + o])) R[i] = k = 1; }
  for (const i of L) if (!R[i]) D[i] = -D[i];
}
${cut('const CO = 52', "await say('Carving")}
const T = buildTerrain(D);
${cut('const POSES = [', "await say('Growing")}
const Wd = makeWorld(D, T, flyPts, POSES);
for (const p of POSES) p[1] += Wd.gy(p[0], p[2]);
${cut('const meshes = [', 'let nV = 0')}
const put = (n, a) => writeFileSync(out + '/' + n, Buffer.from(a.buffer || a, a.byteOffset || 0, a.byteLength));
put('terrain_vb.bin', new Uint8Array(T.vb)); put('terrain_ib.bin', T.ib);
put('big.bin', new Uint8Array(Wd.big.buf)); put('bigF.bin', new Uint8Array(Wd.bigF.buf)); put('small.bin', new Uint8Array(Wd.small.buf));
Wd.chunks.forEach((c, i) => put('chunk' + i + '.bin', c.data)); Wd.props.forEach((c, i) => put('prop' + i + '.bin', c.data));
put('solids.bin', Wd.solids.data); put('glints.bin', Wd.glints); put('bubbles.bin', Wd.bubbles); put('fish.bin', Wd.fish);
put('volD.bin', Wd.volD); put('gm.bin', Wd.gm); put('CG.bin', CG); put('CI.bin', CI);
put('poses.bin', new Float64Array(POSES.flat()));
meshes.forEach((m, i) => { put('mesh' + i + '_vb.bin', new Uint8Array(m.vb)); put('mesh' + i + '_ib.bin', m.ib); });
`;
const fn = new Function('readFileSync', 'writeFileSync', 'dumpDir', 'out', 'hypot2', body.replace(/\bawait say\([^)]*\);/g, ''));
fn(readFileSync, writeFileSync, dumpDir, out);
console.log('done');
