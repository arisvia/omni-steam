#!/usr/bin/env python3
"""
OmniSteam Depot Key Binary Packer & Validator
Binary Format (OMKY v1):
  [Header - 12 bytes]
    - Magic: b'OMKY' (0x4F4D4B59, 4 bytes)
    - Version: uint32 (4 bytes, little-endian = 1)
    - EntryCount: uint32 (4 bytes, little-endian)
  [Entries - EntryCount * 36 bytes, sorted ascending by depot_id]
    - depot_id: uint32 (4 bytes, little-endian)
    - key: bytes (32 bytes raw AES-256 binary key)
"""

import sys
import os
import json
import struct
import re
from typing import Dict, Tuple, List, Optional

MAGIC = b'OMKY'
VERSION = 1
ENTRY_SIZE = 36  # 4 bytes uint32 + 32 bytes key

def validate_hex_key(key_hex: str) -> Optional[bytes]:
    """Validate 64-char hex key and return 32 raw bytes, or None if invalid."""
    clean = key_hex.strip().lower()
    if len(clean) != 64 or not re.fullmatch(r'[0-9a-f]{64}', clean):
        return None
    # Reject dummy / low-entropy keys
    if clean == '0' * 64 or clean == 'f' * 64:
        return None
    unique_chars = len(set(clean))
    if unique_chars < 8:  # Insufficient entropy
        return None
    try:
        return bytes.fromhex(clean)
    except ValueError:
        return None

def pack_keys_to_bin(keys_dict: Dict[int, str], output_path: str) -> int:
    """Pack a dictionary of {depot_id: hex_key} into sorted depotkeys.bin."""
    valid_entries: List[Tuple[int, bytes]] = []
    for depot_id, key_hex in keys_dict.items():
        raw_key = validate_hex_key(key_hex)
        if raw_key:
            valid_entries.append((int(depot_id), raw_key))
    
    # Sort strictly ascending by depot_id for O(log N) binary search
    valid_entries.sort(key=lambda x: x[0])
    
    # Deduplicate (keep latest)
    deduped = {}
    for did, key in valid_entries:
        deduped[did] = key
    
    sorted_entries = sorted(deduped.items(), key=lambda x: x[0])
    entry_count = len(sorted_entries)
    
    with open(output_path, 'wb') as f:
        # Write Header (12 bytes)
        f.write(MAGIC)
        f.write(struct.pack('<II', VERSION, entry_count))
        # Write Entries (36 bytes each)
        for depot_id, raw_key in sorted_entries:
            f.write(struct.pack('<I', depot_id))
            f.write(raw_key)
            
    return entry_count

def unpack_bin_to_dict(bin_path: str) -> Dict[int, str]:
    """Unpack depotkeys.bin into {depot_id: hex_key} dictionary."""
    if not os.path.exists(bin_path):
        return {}
    
    with open(bin_path, 'rb') as f:
        data = f.read()
        
    if len(data) < 12:
        raise ValueError("Invalid depotkeys.bin: file too small")
        
    magic = data[:4]
    if magic != MAGIC:
        raise ValueError(f"Invalid magic header: {magic} (expected {MAGIC})")
        
    version, count = struct.unpack('<II', data[4:12])
    if version != 1:
        raise ValueError(f"Unsupported version: {version}")
        
    expected_size = 12 + count * ENTRY_SIZE
    if len(data) < expected_size:
        raise ValueError(f"Truncated binary file: expected {expected_size} bytes, got {len(data)}")
        
    result = {}
    offset = 12
    for _ in range(count):
        depot_id, = struct.unpack('<I', data[offset:offset+4])
        raw_key = data[offset+4:offset+36]
        result[depot_id] = raw_key.hex()
        offset += ENTRY_SIZE
        
    return result

def main():
    if len(sys.argv) < 2:
        print("Usage: depot_key_tool.py [pack_json <json_file> <output_bin>] | [dump <bin_file>] | [merge <bin_file> <depot_id> <hex_key>]")
        sys.exit(1)
        
    cmd = sys.argv[1]
    if cmd == "pack_json":
        json_file = sys.argv[2]
        output_bin = sys.argv[3] if len(sys.argv) > 3 else "depotkeys.bin"
        with open(json_file, 'r', encoding='utf-8') as f:
            data = json.load(f)
        count = pack_keys_to_bin(data, output_bin)
        print(f"Successfully packed {count} depot keys into {output_bin} ({os.path.getsize(output_bin)} bytes)")
    elif cmd == "dump":
        bin_file = sys.argv[2] if len(sys.argv) > 2 else "depotkeys.bin"
        keys = unpack_bin_to_dict(bin_file)
        print(f"Total keys in {bin_file}: {len(keys)}")
        for did in sorted(keys.keys())[:10]:
            print(f"  Depot {did}: {keys[did]}")
        if len(keys) > 10:
            print(f"  ... and {len(keys) - 10} more entries.")
    else:
        print(f"Unknown command: {cmd}")

if __name__ == '__main__':
    main()
