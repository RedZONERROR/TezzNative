#!/usr/bin/env python3
"""
TezzNative Project Library Helper (tezz_lib.py)
TezzCorp Pvt Ltd. | Created for TezzNative v2.2

Manage libraries according to project requirements:
  python tools/tezz_lib.py add <name> [project_dir]
  python tools/tezz_lib.py sync [project_dir]
  python tools/tezz_lib.py list
  python tools/tezz_lib.py remove <name> [project_dir]
"""

import sys
import os
import re
import shutil

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SDK_LIB_DIR = os.path.join(REPO_ROOT, "lib")
REGISTRY_PATH = os.path.join(REPO_ROOT, "registry.tnx")
LOCK_PATH = os.path.join(REPO_ROOT, "tezz.lock")

LIBRARY_CATALOG = {
    "std": "Core standard library: memory, assertions, print, primitive utilities",
    "io": "File system, directory traversal, streams, file readers and writers",
    "net": "TCP sockets, DNS resolution, HTTP client/server, high-performance loopback",
    "tls": "TLS 1.2/1.3 encrypted secure sockets and policy-based handshake",
    "math": "Math functions, trigonometry, statistics, floating-point algorithms",
    "time": "High-resolution clocks, durations, timers, ISO timestamps",
    "task": "Multi-threaded async/await runtime, thread pools, channels, schedulers",
    "mind": "On-device AI inference engine: GGUF model execution, transformer decoder, token sampling",
    "llm": "Large language model runtime, prompt templates, conversational pipelines",
    "llm_core": "Low-level int8/fp16 quantized GEMM matmul kernels and SIMD tensor lanes",
    "nn": "Neural network layers: Dense, Conv2D, LayerNorm, activations, loss functions",
    "tensor": "Multi-dimensional tensor arithmetic, broadcasting, reshaping, matrix ops",
    "trainer": "Backpropagation, AdamW optimizer, SGD, loss tracking, training loops",
    "tokenizer": "BPE and WordPiece fast byte tokenizer for generative language models",
    "gpu": "GPU accelerator host interface, compute shader dispatch, buffer binding",
    "npu": "Neural Processing Unit accelerator bridge and hardware offload",
    "simd": "AVX2 / AVX-512 / NEON vector intrinsics and hardware acceleration",
    "intrin": "Direct CPU intrinsics: popcount, clz, ctz, bitwise vector lanes",
    "arena": "High-speed bump allocator for scoped zero-overhead memory pools",
    "vec": "Dynamic vector collection with automatic growth and element lifecycle",
    "str": "String manipulation, formatting, regex matching, substring slicing",
    "data": "Structured binary serialization, JSON parser/encoder, key-value stores",
    "tezzdb": "Embedded high-concurrency ACID key-document database engine",
    "tezzdbql": "Query language and indexed record lookup engine for TezzDB",
    "tezzapi": "Declarative REST API framework with route handlers and middleware",
    "tezzserve": "High-throughput static and dynamic HTTP web server with keepalive",
    "gui": "Cross-platform graphical user interface primitives and canvas renderer",
    "tnui": "Immediate-mode UI toolkit: buttons, labels, inputs, layout containers",
    "tzgui": "Native windowing, event queues, input devices, message loop dispatch",
    "tzimage": "Image processing, bitmap loading, resize, PNG/JPEG decode",
    "color": "Color spaces, RGB/RGBA conversions, hex parsing, palette generation",
    "event": "Event loop, signals, custom event dispatch, listener queues",
    "frame": "Animation frames, display synchronization, delta timing",
    "actor": "Actor model concurrency: message passing, actor mailboxes, supervisor trees",
    "tsm": "Tezz State Machine: finite state automaton, transitions, guard predicates",
    "tnauto": "Automation, process spawning, IPC pipes, environment configuration",
    "tts": "Text-to-speech synthesis engine: acoustic model and vocoder pipeline",
    "stt": "Speech-to-text recognition: acoustic feature extraction and CTC decoder",
    "cyber": "Cryptographic hashing (SHA-256, HMAC, CRC32), encryption primitives",
    "sys": "Platform system queries, process information, memory statistics",
    "os": "Operating system abstractions: environment variables, path separators, user profiles",
}

def hash8_source_file(path):
    with open(path, "rb") as f:
        data = f.read()
    h = 5381
    pending_cr = False
    for b in data:
        if pending_cr:
            if b == 10:
                h = (((h << 5) + h + 10) & 0x7FFFFFFF)
                pending_cr = False
                continue
            h = (((h << 5) + h + 13) & 0x7FFFFFFF)
            pending_cr = False
        if b == 13:
            pending_cr = True
        else:
            h = (((h << 5) + h + b) & 0x7FFFFFFF)
    if pending_cr:
        h = (((h << 5) + h + 13) & 0x7FFFFFFF)
    return f"{h:08X}"

def hash8_text(text):
    data = text.encode("ascii")
    h = 5381
    for b in data:
        h = (((h << 5) + h + b) & 0x7FFFFFFF)
    return f"{h:08X}"

def find_sdk_lib(name):
    # Try local repo lib/
    p1 = os.path.join(SDK_LIB_DIR, f"{name}.tn")
    if os.path.exists(p1):
        return p1
    # Try TEZZ_SDK_ROOT
    sdk_root = os.environ.get("TEZZ_SDK_ROOT")
    if sdk_root:
        p2 = os.path.join(sdk_root, "lib", f"{name}.tn")
        if os.path.exists(p2):
            return p2
    return None

def update_tezz_mod(project_dir, name, version="0.1.0"):
    mod_path = os.path.join(project_dir, "tezz.mod")
    if not os.path.exists(mod_path):
        # Create minimal tezz.mod
        proj_name = os.path.basename(os.path.abspath(project_dir)) or "app"
        content = (
            f"name = {proj_name}\n"
            f"version = 0.1.0\n"
            f"module_root = lib\n"
            f"registry = https://tezznative.org/registry.tnx\n"
            f"registry_lib = https://tezznative.org/download/sdk/lib/\n"
            f"dep.{name} = {version}\n"
        )
        with open(mod_path, "w", encoding="ascii") as f:
            f.write(content)
        return

    with open(mod_path, "r", encoding="ascii") as f:
        lines = f.readlines()

    dep_key = f"dep.{name}"
    found = False
    new_lines = []
    for line in lines:
        if line.strip().startswith(f"{dep_key} =") or line.strip().startswith(f"{dep_key}="):
            new_lines.append(f"{dep_key} = {version}\n")
            found = True
        else:
            new_lines.append(line)

    if not found:
        # Append before optional dependencies or at end
        new_lines.append(f"{dep_key} = {version}\n")

    with open(mod_path, "w", encoding="ascii") as f:
        f.writelines(new_lines)

def update_tezz_lock(project_dir, name, version="0.1.0", checksum=None, url=None):
    lock_path = os.path.join(project_dir, "tezz.lock")
    if url is None:
        url = f"https://tezznative.org/download/sdk/lib/{name}.tn"
    if checksum is None:
        lib_file = os.path.join(project_dir, "lib", f"{name}.tn")
        if os.path.exists(lib_file):
            checksum = hash8_source_file(lib_file)
        else:
            checksum = "00000000"

    entries = {}
    if os.path.exists(lock_path):
        with open(lock_path, "r", encoding="ascii") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split()
                if len(parts) >= 3:
                    token = parts[0]
                    entries[token] = (parts[1], parts[2])

    token = f"{name}@{version}"
    entries[token] = (checksum, url)

    # Sort entries
    sorted_tokens = sorted(entries.keys())
    payload_lines = [f"{t} {entries[t][0]} {entries[t][1]}" for t in sorted_tokens]
    payload = "\n".join(payload_lines) + "\n"
    payload_hash = hash8_text(payload)

    header = f"# lock-meta v1 lines={len(payload_lines)} payload={payload_hash} key=none sig=none\n"
    with open(lock_path, "w", encoding="ascii") as f:
        f.write(header + payload)

def add_library(name, project_dir="."):
    project_dir = os.path.abspath(project_dir)
    lib_src = find_sdk_lib(name)
    if not lib_src:
        print(f"Error: Library '{name}' not found in TezzNative SDK.")
        print(f"Available libraries: {', '.join(sorted(LIBRARY_CATALOG.keys()))}")
        return False

    dest_lib_dir = os.path.join(project_dir, "lib")
    os.makedirs(dest_lib_dir, exist_ok=True)
    dest_path = os.path.join(dest_lib_dir, f"{name}.tn")
    shutil.copyfile(lib_src, dest_path)

    checksum = hash8_source_file(dest_path)
    update_tezz_mod(project_dir, name, "0.1.0")
    update_tezz_lock(project_dir, name, "0.1.0", checksum)

    desc = LIBRARY_CATALOG.get(name, "TezzNative module")
    print(f"Added library '{name}' (v0.1.0, checksum: {checksum}) -> lib/{name}.tn")
    print(f"  Description: {desc}")
    print(f"  Ready to import in code: import \"{name}\"")
    return True

def scan_project_imports(project_dir):
    """Scan all .tn files in project for import statements."""
    imports = set()
    import_pattern = re.compile(r'^\s*import\s+["\']([^"\']+)["\']', re.MULTILINE)
    for root, dirs, files in os.walk(project_dir):
        if "lib" in dirs:
            dirs.remove("lib")
        if ".tezz" in dirs:
            dirs.remove(".tezz")
        for f in files:
            if f.endswith(".tn"):
                fpath = os.path.join(root, f)
                try:
                    with open(fpath, "r", encoding="utf-8", errors="replace") as fh:
                        content = fh.read()
                    for m in import_pattern.findall(content):
                        mod_name = m.strip()
                        if mod_name.startswith("./"):
                            mod_name = mod_name[2:]
                        if mod_name.endswith(".tn"):
                            mod_name = mod_name[:-3]
                        imports.add(mod_name)
                except Exception:
                    pass
    return imports

def sync_project(project_dir="."):
    project_dir = os.path.abspath(project_dir)
    mod_path = os.path.join(project_dir, "tezz.mod")
    needed = set()

    # 1. Read dependencies from tezz.mod
    if os.path.exists(mod_path):
        with open(mod_path, "r", encoding="ascii") as f:
            for line in f:
                line = line.strip()
                if line.startswith("dep."):
                    dep = line.split("=")[0].strip()[4:]
                    needed.add(dep)

    # 2. Scan project code for imports
    code_imports = scan_project_imports(project_dir)
    for imp in code_imports:
        if imp in LIBRARY_CATALOG or find_sdk_lib(imp):
            needed.add(imp)

    if not needed:
        print("No libraries declared in tezz.mod or imported in project code.")
        return

    added = 0
    dest_lib_dir = os.path.join(project_dir, "lib")
    os.makedirs(dest_lib_dir, exist_ok=True)

    for name in sorted(needed):
        dest_file = os.path.join(dest_lib_dir, f"{name}.tn")
        if not os.path.exists(dest_file):
            if add_library(name, project_dir):
                added += 1
        else:
            # Ensure locked in tezz.mod and tezz.lock
            checksum = hash8_source_file(dest_file)
            update_tezz_mod(project_dir, name, "0.1.0")
            update_tezz_lock(project_dir, name, "0.1.0", checksum)

    print(f"Sync complete. Project has {len(needed)} libraries ready in lib/ ({added} newly added).")

def list_libraries():
    print("=" * 72)
    print(" TezzNative Standard & Production Library Catalog")
    print("=" * 72)
    for name, desc in sorted(LIBRARY_CATALOG.items()):
        print(f"  {name:<15} {desc}")
    print("=" * 72)
    print("Add any library to your project: python tools/tezz_lib.py add <name>")
    print("Auto-sync all project imports:   python tools/tezz_lib.py sync")

def remove_library(name, project_dir="."):
    project_dir = os.path.abspath(project_dir)
    lib_file = os.path.join(project_dir, "lib", f"{name}.tn")
    if os.path.exists(lib_file):
        os.remove(lib_file)
        print(f"Removed lib/{name}.tn")

    mod_path = os.path.join(project_dir, "tezz.mod")
    if os.path.exists(mod_path):
        with open(mod_path, "r", encoding="ascii") as f:
            lines = [l for l in f if not l.strip().startswith(f"dep.{name}")]
        with open(mod_path, "w", encoding="ascii") as f:
            f.writelines(lines)
        print(f"Removed dep.{name} from tezz.mod")

    lock_path = os.path.join(project_dir, "tezz.lock")
    if os.path.exists(lock_path):
        with open(lock_path, "r", encoding="ascii") as f:
            lines = [l.strip() for l in f if l.strip() and not l.strip().startswith("#")]
        lines = [l for l in lines if not l.startswith(f"{name}@")]
        payload = "\n".join(lines) + ("\n" if lines else "")
        payload_hash = hash8_text(payload) if lines else "00000000"
        header = f"# lock-meta v1 lines={len(lines)} payload={payload_hash} key=none sig=none\n"
        with open(lock_path, "w", encoding="ascii") as f:
            f.write(header + payload)
        print(f"Updated tezz.lock")

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    cmd = sys.argv[1].lower()
    if cmd in ("add", "install", "get"):
        if len(sys.argv) < 3:
            print("Usage: python tools/tezz_lib.py add <library_name> [project_dir]")
            sys.exit(1)
        name = sys.argv[2]
        pdir = sys.argv[3] if len(sys.argv) > 3 else "."
        ok = add_library(name, pdir)
        sys.exit(0 if ok else 1)
    elif cmd in ("sync", "fetch"):
        pdir = sys.argv[2] if len(sys.argv) > 2 else "."
        sync_project(pdir)
    elif cmd in ("list", "ls", "catalog"):
        list_libraries()
    elif cmd in ("remove", "rm"):
        if len(sys.argv) < 3:
            print("Usage: python tools/tezz_lib.py remove <library_name> [project_dir]")
            sys.exit(1)
        name = sys.argv[2]
        pdir = sys.argv[3] if len(sys.argv) > 3 else "."
        remove_library(name, pdir)
    else:
        print(f"Unknown command: {cmd}")
        print(__doc__)
        sys.exit(1)

if __name__ == "__main__":
    main()
