#!/usr/bin/env python3
"""
OmniSteam Codebase Quality, Format & Include Integrity Checker
Pure Python 3 - Standard library only (no third-party pip dependencies required).

Usage:
  python tools/check_code.py          # Run comprehensive syntax, include & balance checks
  python tools/check_code.py --fix    # Auto-heal missing standard headers & run clang-format
  python tools/check_code.py --format # Run clang-format on all C++ files
"""

import os
import sys
import re
import argparse
import subprocess
import shutil
from pathlib import Path
from typing import List, Tuple, Dict

# Force UTF-8 on Windows stdout/stderr to prevent encoding errors
if sys.platform == 'win32':
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    if hasattr(sys.stderr, 'reconfigure'):
        sys.stderr.reconfigure(encoding='utf-8', errors='replace')

CHECK_RULES = [
    (re.compile(r'\bstd::(ifstream|ofstream|fstream)\b'), '<fstream>'),
    (re.compile(r'\bstd::(stringstream|istringstream|ostringstream)\b'), '<sstream>'),
    (re.compile(r'\bstd::(cout|cerr|cin)\b'), '<iostream>'),
    (re.compile(r'\bstd::vector\b'), '<vector>'),
    (re.compile(r'\bstd::(string|to_string)\b'), '<string>'),
    (re.compile(r'\bstd::string_view\b'), '<string_view>'),
    (re.compile(r'\bstd::(regex|smatch|sregex_iterator)\b'), '<regex>'),
    (re.compile(r'\bstd::(thread|jthread)\b'), '<thread>'),
    (re.compile(r'\bstd::(mutex|lock_guard|unique_lock)\b'), '<mutex>'),
    (re.compile(r'\bstd::atomic\b'), '<atomic>'),
    (re.compile(r'\bstd::(optional|nullopt)\b'), '<optional>'),
    (re.compile(r'\bstd::set\b'), '<set>'),
    (re.compile(r'\bstd::unordered_set\b'), '<unordered_set>'),
    (re.compile(r'\bstd::map\b'), '<map>'),
    (re.compile(r'\bstd::unordered_map\b'), '<unordered_map>'),
    (re.compile(r'\b(std::filesystem|fs::)\b'), '<filesystem>'),
    (re.compile(r'\b(std::memcpy|std::memset|std::memmove)\b'), '<cstring>'),
    (re.compile(r'\b(uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t|uintptr_t|intptr_t)\b'), '<cstdint>'),
    (re.compile(r'\bstd::chrono::'), '<chrono>'),
    (re.compile(r'\bstd::(sort|lower_bound|min|max)\b'), '<algorithm>')
]

IGNORED_DIRS = {
    'build', '.git', '.cache', 'dist', 'packages', 'node_modules',
    '.vs', 'out', 'bin', 'obj', 'third_party', 'vendor'
}

CPP_TOKEN_PATTERN = re.compile(
    r'(R"([a-zA-Z0-9_]*)\([\s\S]*?\)\2")|'   # 1,2: Raw string literal
    r'("(\\.|[^"\\])*")|'                    # 3,4: Normal string literal
    r"('(\\.|[^'\\])*')|"                    # 5,6: Character literal
    r'(/\*[\s\S]*?\*/)|'                      # 7: Block comment
    r'(//[^\n]*)'                             # 8: Line comment
)

def find_cpp_files(root_dir: str) -> List[Path]:
    """Find all C++ source and header files, skipping build/ and temporary directories."""
    cpp_files = []
    for root, dirs, files in os.walk(root_dir):
        dirs[:] = [d for d in dirs if d not in IGNORED_DIRS and not d.startswith('cmake-build-')]
        for f in files:
            if f.endswith(('.cpp', '.h', '.hpp', '.c')):
                cpp_files.append(Path(root) / f)
    return cpp_files

def strip_tokens(content: str) -> str:
    """Strip comments and normalize string literals to prevent false matches."""
    def replacer(match: re.Match) -> str:
        if match.group(1) or match.group(3) or match.group(5):
            return '""'
        return ' '
    return CPP_TOKEN_PATTERN.sub(replacer, content)

def check_delimiters(clean: str) -> List[str]:
    """Check bracket, brace, and parenthesis balance on cleaned code content."""
    errors = []
    counts = {
        '{': clean.count('{'), '}': clean.count('}'),
        '(': clean.count('('), ')': clean.count(')'),
        '[': clean.count('['), ']': clean.count(']')
    }

    if counts['{'] != counts['}']:
        errors.append(f"Brace mismatch {{}}: open={counts['{']}, close={counts['}']}")
    if counts['('] != counts[')']:
        errors.append(f"Parenthesis mismatch (): open={counts['(']}, close={counts[')']}")
    if counts['['] != counts[']']:
        errors.append(f"Bracket mismatch []: open={counts['[']}, close={counts[']']}")

    return errors

def check_includes(clean: str, raw_content: str) -> List[str]:
    """Identify missing standard library headers based on cleaned code content."""
    missing = []
    for pattern, header in CHECK_RULES:
        if pattern.search(clean) and f'#include {header}' not in raw_content:
            missing.append(header)
    return missing

def heal_file_includes(file_path: Path, missing_headers: List[str]):
    """Insert missing standard includes safely in the file."""
    content = file_path.read_text(encoding='utf-8')
    headers_text = '\n'.join(f'#include {h}' for h in missing_headers)

    pragma_match = re.search(r'^[ \t]*#pragma\s+once[^\n]*\n', content, flags=re.MULTILINE)
    if pragma_match:
        idx = pragma_match.end()
        new_content = content[:idx] + '\n' + headers_text + '\n' + content[idx:]
    else:
        first_include = re.search(r'^[ \t]*#include\s+[<"][^>\n]+[>"][^\n]*\n', content, flags=re.MULTILINE)
        if first_include:
            idx = first_include.end()
            new_content = content[:idx] + headers_text + '\n' + content[idx:]
        else:
            new_content = headers_text + '\n\n' + content

    file_path.write_text(new_content, encoding='utf-8')

def run_clang_format(files: List[Path]) -> bool:
    """Run clang-format if available on system."""
    clang_format = shutil.which('clang-format')
    if not clang_format and os.path.exists(r'C:\Program Files\LLVM\bin\clang-format.exe'):
        clang_format = r'C:\Program Files\LLVM\bin\clang-format.exe'

    if not clang_format:
        print("[WARN] clang-format not found in PATH or standard location; skipping formatting.")
        return False

    print(f"[INFO] Formatting {len(files)} files with {clang_format}...")
    for f in files:
        subprocess.run([clang_format, '-i', str(f)], check=True)
    print("[PASS] Clang-Format completed successfully.")
    return True

def main():
    parser = argparse.ArgumentParser(description="OmniSteam Code Checker & Auto-Healer")
    parser.add_argument('--fix', action='store_true', help="Auto-heal missing headers and format code")
    parser.add_argument('--format', action='store_true', help="Format code with clang-format")
    parser.add_argument('--dir', default='.', help="Root directory to check")
    args = parser.parse_args()

    root_dir = os.path.abspath(args.dir)
    cpp_files = find_cpp_files(root_dir)

    print(f"=== OmniSteam Code Check (Files: {len(cpp_files)}) ===")

    total_syntax_issues = 0
    files_with_missing_headers: Dict[Path, List[str]] = {}

    for file_path in cpp_files:
        try:
            raw_content = file_path.read_text(encoding='utf-8')
        except Exception as e:
            print(f"[ERROR] Could not read {file_path}: {e}")
            total_syntax_issues += 1
            continue

        clean_content = strip_tokens(raw_content)

        delim_errors = check_delimiters(clean_content)
        for err in delim_errors:
            print(f"[FAIL] {file_path.relative_to(root_dir)}: {err}")
            total_syntax_issues += 1

        missing = check_includes(clean_content, raw_content)
        if missing:
            files_with_missing_headers[file_path] = missing
            print(f"[MISSING INCLUDES] {file_path.relative_to(root_dir)}: {', '.join(missing)}")

    missing_include_count = sum(len(m) for m in files_with_missing_headers.values())

    if args.fix and files_with_missing_headers:
        print(f"\n[INFO] Auto-healing missing includes across {len(files_with_missing_headers)} files...")
        for file_path, missing in files_with_missing_headers.items():
            heal_file_includes(file_path, missing)
        print("[PASS] All missing headers injected successfully.")
        missing_include_count = 0

    if args.format or args.fix:
        run_clang_format(cpp_files)

    total_issues = total_syntax_issues + missing_include_count

    print("\n" + "=" * 50)
    if total_issues == 0:
        print("🎉 [ALL CHECKS PASSED] 100% include integrity, syntax balance, and cleanliness!")
        sys.exit(0)
    else:
        print(f"❌ [CHECK FAILED] Found {total_issues} issues. Run `python tools/check_code.py --fix` to auto-heal.")
        sys.exit(1)

if __name__ == '__main__':
    main()
