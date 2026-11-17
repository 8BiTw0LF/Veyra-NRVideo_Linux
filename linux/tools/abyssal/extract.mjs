// Evaluates Abyssal Garden's quality.js + math.js + shaders.js for the High level and writes every WGSL module.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
const [src, out] = process.argv.slice(2);
const files = ['quality.js', 'math.js', 'shaders.js'].map((f) => readFileSync(`${src}/${f}`, 'utf8')).join('\n');
const names = [...files.matchAll(/^const (W_[A-Z0-9_]+)\s*=/gm)].map((m) => m[1]);
const stub = `const matchMedia = () => ({ matches: false }); const navigator = { deviceMemory: 16 }; const localStorage = {};`;
const get = new Function(stub + files + `\nreturn { ${names.join(', ')} };`)();
mkdirSync(out, { recursive: true });
for (const n of names) writeFileSync(`${out}/${n.slice(2).toLowerCase()}.wgsl`, get[n]);
console.log(names.join(' '));
