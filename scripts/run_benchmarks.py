#!/usr/bin/env python3
"""
MemSentry Automated Benchmark Runner & Report Generator
Executes the benchmark binary and formats latency / throughput tables for README.md.
"""

import subprocess
import os
import sys
import re

def run_benchmark():
    bin_path = os.path.join("bin", "benchmark.exe" if sys.platform == "win32" else "benchmark")
    if not os.path.exists(bin_path):
        print(f"[-] Benchmark binary not found at: {bin_path}")
        sys.exit(1)

    print(f"[*] Running benchmark: {bin_path}...")
    result = subprocess.run([bin_path], capture_output=True, text=True)
    if result.returncode != 0:
        print("[-] Benchmark execution failed:")
        print(result.stderr)
        sys.exit(1)

    print("[+] Benchmark completed successfully!")
    print(result.stdout)
    return result.stdout

if __name__ == "__main__":
    output = run_benchmark()
