#!/usr/bin/env python3
"""VeyAmux for Linux: replacement for AMD's Windows-only FidelityFX_SC.exe (GLSL/Vulkan path only).

Reproduces what FidelityFX_SC does for `-compiler=glslang ... -reflection` so the unmodified FidelityFX SDK 1.1.4
Vulkan backend and blob accessors can be built on Linux:
  * expands permutation options  -DNAME={a,b,...}  into every combination (same key/bit layout, including the
    "options not mentioned in the shader come last and are deduplicated" rule),
  * compiles each permutation with glslangValidator, names it by the MD5 of the SPIR-V (dedupe),
  * reflects descriptors with SPIRV-Reflect (ffx_reflect helper) and writes
      <name>_<md5>.h            (reflection arrays + SPIR-V bytes)
      <name>_permutations.h     (option enums, PermutationKey union, PermutationInfo, IndirectionTable, info table)
Header layout follows tools/ffx_shader_compiler/src/{ffx_sc.cpp,glsl_compiler.cpp} (MIT, AMD).

usage: ffx_sc_linux.py --reflect <ffx_reflect> --glslang <glslangValidator> --out <dir> --name <shaderName>
                        [-I<dir> ...] [-DNAME=value ...] [-DNAME={a,b} ...] [glslang args ...] <shader.glsl>
"""
import hashlib
import itertools
import os
import re
import subprocess
import sys
import tempfile


def parse(argv):
    o = {"reflect": None, "glslang": None, "out": None, "name": None, "includes": [], "fixed": [], "perm": [],
         "glslang_args": [], "input": None}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--reflect", "--glslang", "--out", "--name"):
            o[a[2:]] = argv[i + 1]; i += 2; continue
        if a.startswith("-I"):
            o["includes"].append(a[2:] if len(a) > 2 else argv[i + 1]); i += 1 if len(a) > 2 else 2; continue
        if a.startswith("-D"):
            body = a[2:]
            m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)=\{(.*)\}$", body)
            if m:
                o["perm"].append((m.group(1), [v.strip() for v in m.group(2).split(",")]))
            else:
                o["fixed"].append(body)
            i += 1; continue
        if i == len(argv) - 1 and not a.startswith("-"):
            o["input"] = a; i += 1; continue
        o["glslang_args"].append(a); i += 1
    for k in ("reflect", "glslang", "out", "name", "input"):
        if not o[k]:
            sys.exit(f"missing --{k}")
    return o


def find_include(name, here, includes):
    for d in [here] + includes:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return os.path.abspath(p)
    return None


def options_mentioned(shader, includes, names):
    """FidelityFX_SC's early filter: which permutation defines appear in the shader or anything it includes."""
    found, seen, todo = set(), set(), [os.path.abspath(shader)]
    inc_re = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
    while todo and len(found) < len(names):
        f = todo.pop()
        if f in seen:
            continue
        seen.add(f)
        try:
            text = open(f, encoding="utf-8", errors="replace").read().split("\n")
        except OSError:
            continue
        for line in text:
            m = inc_re.match(line)
            if m:
                p = find_include(m.group(1), os.path.dirname(f), includes)
                if p:
                    todo.append(p)
            for n in names:
                if n not in found and n in line:
                    found.add(n)
    return found


def bits_for(count):
    return max(1, (count - 1).bit_length())


def main():
    o = parse(sys.argv[1:])
    os.makedirs(o["out"], exist_ok=True)
    name = o["name"]
    shader = os.path.abspath(o["input"])

    # Stable partition: options mentioned in the shader first (as FidelityFX_SC does).
    mentioned = options_mentioned(shader, o["includes"], [p[0] for p in o["perm"]])
    opts = [p for p in o["perm"] if p[0] in mentioned] + [p for p in o["perm"] if p[0] not in mentioned]
    numeric = {n: all(re.fullmatch(r"-?\d+", v) for v in vals) for n, vals in opts}
    nbits = {n: bits_for(len(vals)) for n, vals in opts}

    # Enumerate combinations; keys use the same bit layout as the generated union.
    key_to_index, hash_to_index, uniques = {}, {}, []
    tmp = tempfile.mkdtemp(prefix=f"ffxsc_{name}_")
    combos = list(itertools.product(*[range(len(v)) for _, v in opts])) if opts else [()]
    for combo in combos:
        key, bit, defines, dup_of = 0, 0, [], None
        for (oname, vals), vi in zip(opts, combo):
            if oname not in mentioned and vi != 0 and dup_of is None:
                dup_of = True
            val = vals[vi]
            if not val.startswith("-"):
                defines.append(f"-D{oname}={val}" if numeric[oname] else f"-D{val}")
            key |= vi << bit
            bit += nbits[oname]
        if dup_of:
            # Identical to the permutation with every unmentioned option at index 0.
            base_key, b = 0, 0
            for (oname, vals), vi in zip(opts, combo):
                base_key |= (vi if oname in mentioned else 0) << b
                b += nbits[oname]
            key_to_index[key] = ("alias", base_key)
            continue
        spv = os.path.join(tmp, f"{key}.spv")
        cmd = [o["glslang"]] + o["glslang_args"] + [f"-D{d}" for d in o["fixed"]] + defines + \
              [f"-I{i}" for i in o["includes"]] + ["-o", spv, shader]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(f"{shader}[{key}] glslang failed:\n{r.stdout}\n{r.stderr}\n")
            sys.exit(1)
        blob = open(spv, "rb").read()
        digest = hashlib.md5(blob).hexdigest()
        if digest not in hash_to_index:
            hash_to_index[digest] = len(uniques)
            rr = subprocess.run([o["reflect"], spv], capture_output=True, text=True, check=True)
            refl = [ln.split("\t") for ln in rr.stdout.splitlines() if ln.strip()]
            uniques.append((digest, blob, refl, defines))
        key_to_index[key] = hash_to_index[digest]
    for k, v in list(key_to_index.items()):
        if isinstance(v, tuple):
            key_to_index[k] = key_to_index[v[1]]

    kinds = ["CBV", "TextureSRV", "TextureUAV", "BufferSRV", "BufferUAV", "Sampler", "RTAccelerationStructure"]
    for digest, blob, refl, _ in uniques:
        pname = f"{name}_{digest}"
        with open(os.path.join(o["out"], pname + ".h"), "w") as fp:
            fp.write(f"// {pname}.h.\n// Auto generated by VeyAmux ffx_sc_linux.py (FidelityFX-SC compatible).\n\n")
            for k in kinds:
                rs = [r for r in refl if r[0] == k]
                if not rs:
                    continue
                names = " ".join('"%s",' % r[1] for r in rs)
                binds = " ".join("%s," % r[2] for r in rs)
                counts = " ".join("%s," % r[3] for r in rs)
                sets_ = " ".join("%s," % r[4] for r in rs)
                fp.write("static const char* g_%s_%sResourceNames[] = { %s };\n" % (pname, k, names))
                fp.write("static const uint32_t g_%s_%sResourceBindings[] = { %s };\n" % (pname, k, binds))
                fp.write("static const uint32_t g_%s_%sResourceCounts[] = { %s };\n" % (pname, k, counts))
                fp.write("static const uint32_t g_%s_%sResourceSets[] = { %s };\n\n" % (pname, k, sets_))
            fp.write(f"static const uint32_t g_{pname}_size = {len(blob)};\n\n")
            fp.write(f"static const unsigned char g_{pname}_data[] = {{\n")
            for i in range(0, len(blob), 16):
                fp.write(",".join(f"0x{b:02x}" for b in blob[i:i + 16]) + ("," if i + 16 < len(blob) else "") + "\n")
            fp.write("};\n\n")

    with open(os.path.join(o["out"], f"{name}_permutations.h"), "w") as fp:
        for digest, *_ in uniques:
            fp.write(f'#include "{name}_{digest}.h"\n')
        fp.write("\n")
        for oname, vals in opts:
            if not numeric[oname]:
                up = oname.upper()
                fp.write(f"typedef enum {oname} {{\n")
                for j, v in enumerate(vals):
                    sep = "" if j == len(vals) - 1 else ","
                    fp.write(f"    OPT_{up}_{v.upper()} = {j}{sep}\n")
                fp.write(f"}} {oname};\n\n")
        fp.write(f"typedef union {name}_PermutationKey {{\n    struct {{\n")
        for oname, _ in opts:
            fp.write(f"        uint32_t {oname} : {nbits[oname]};\n")
        fp.write(f"    }};\n    uint32_t index;\n}} {name}_PermutationKey;\n\n")
        fp.write(f"typedef struct {name}_PermutationInfo {{\n    const uint32_t       blobSize;\n    const unsigned char* blobData;\n\n")
        members = [("ConstantBuffers", "constantBuffer"), ("SRVTextures", "srvTexture"), ("UAVTextures", "uavTexture"),
                   ("SRVBuffers", "srvBuffer"), ("UAVBuffers", "uavBuffer"), ("Samplers", "sampler"),
                   ("RTAccelerationStructures", "rtAccelerationStructure")]
        for plural, single in members:
            fp.write(f"\n    const uint32_t  num{plural};\n    const char**    {single}Names;\n"
                     f"    const uint32_t* {single}Bindings;\n    const uint32_t* {single}Counts;\n    const uint32_t* {single}Spaces;\n")
        fp.write(f"}} {name}_PermutationInfo;\n\n")
        total = 1 << sum(nbits[n] for n, _ in opts)
        fp.write(f"static const uint32_t g_{name}_IndirectionTable[] = {{\n")
        for i in range(total):
            fp.write(f"    {key_to_index.get(i, 0)},\n")
        fp.write("};\n\n")
        fp.write(f"static const {name}_PermutationInfo g_{name}_PermutationInfo[] = {{\n")
        for digest, blob, refl, _ in uniques:
            pname = f"{name}_{digest}"
            fp.write(f"    {{ g_{pname}_size, g_{pname}_data, ")
            for k in kinds:
                n = sum(1 for r in refl if r[0] == k)
                if n == 0:
                    fp.write("0, 0, 0, 0, 0, ")
                else:
                    fp.write(f"{n}, g_{pname}_{k}ResourceNames, g_{pname}_{k}ResourceBindings, g_{pname}_{k}ResourceCounts, g_{pname}_{k}ResourceSets, ")
            fp.write("},\n")
        fp.write("};\n\n")
    print(f"{name}: {len(combos)} permutations, {len(uniques)} unique")


if __name__ == "__main__":
    main()
