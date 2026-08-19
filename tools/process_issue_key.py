#!/usr/bin/env python3
"""
OmniSteam Issue Depot Key Processor & Validator
Verifies AppID with Steam Store API, validates key entropy,
prevents unauthorized key overwrites, and merges into depotkeys.bin.
"""

import sys
import os
import re
import json
import urllib.request
import urllib.error
from depot_key_tool import unpack_bin_to_dict, pack_keys_to_bin, validate_hex_key

def fetch_steam_app_metadata(app_id: int) -> dict:
    """Query Steam Store API to verify AppID existence and retrieve title."""
    url = f"https://store.steampowered.com/api/appdetails?appids={app_id}&l=english"
    req = urllib.request.Request(url, headers={'User-Agent': 'OmniSteam-KeyValidator/1.0'})
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            data = json.loads(response.read().decode('utf-8'))
            app_data = data.get(str(app_id), {})
            if app_data.get('success'):
                data_obj = app_data.get('data', {})
                return {
                    'valid': True,
                    'name': data_obj.get('name', 'Unknown Game'),
                    'type': data_obj.get('type', 'game')
                }
    except Exception as e:
        print(f"Warning: Steam API query failed: {e}", file=sys.stderr)
    return {'valid': False, 'name': '', 'type': ''}

def parse_issue_body(body_text: str) -> dict:
    """Extract fields from GitHub Issue form markdown."""
    result = {}
    
    # Extract AppID
    app_match = re.search(r'###\s+Steam AppID\s+([\d]+)', body_text)
    if app_match:
        result['app_id'] = int(app_match.group(1).strip())
        
    # Extract DepotID
    depot_match = re.search(r'###\s+Depot ID\s+([\d]+)', body_text)
    if depot_match:
        result['depot_id'] = int(depot_match.group(1).strip())
        
    # Extract Key
    key_match = re.search(r'###\s+64-character Decryption Key[^\n]*\s+([0-9a-fA-F]{64})', body_text)
    if key_match:
        result['depot_key'] = key_match.group(1).strip().lower()
        
    # Extract Depot Type
    type_match = re.search(r'###\s+Depot Type\s+([^\n]+)', body_text)
    if type_match:
        result['depot_type'] = type_match.group(1).strip()
        
    return result

def main():
    if len(sys.argv) < 3:
        print("Usage: process_issue_key.py <issue_body_file> <depotkeys_bin_path>")
        sys.exit(1)
        
    issue_file = sys.argv[1]
    bin_path = sys.argv[2]
    
    with open(issue_file, 'r', encoding='utf-8') as f:
        body = f.read()
        
    parsed = parse_issue_body(body)
    app_id = parsed.get('app_id')
    depot_id = parsed.get('depot_id')
    key_hex = parsed.get('depot_key')
    depot_type = parsed.get('depot_type', 'Game Content')
    
    if not app_id or not depot_id or not key_hex:
        print(f"ERROR: Missing required fields: app_id={app_id}, depot_id={depot_id}, key={bool(key_hex)}")
        print("RESULT:INVALID_FORM")
        sys.exit(1)
        
    # 1. Validate Key format & entropy
    raw_key = validate_hex_key(key_hex)
    if not raw_key:
        print("ERROR: Key is invalid, dummy (000/fff), or has insufficient entropy.")
        print("RESULT:INVALID_KEY")
        sys.exit(1)
        
    # 2. Query Steam API for App metadata
    print(f"Verifying AppID {app_id} with Steam Store API...")
    meta = fetch_steam_app_metadata(app_id)
    game_title = meta['name'] if meta['valid'] else f"App {app_id}"
    print(f"App Title: {game_title} (Valid: {meta['valid']})")
    
    # 3. Check existing depotkeys.bin
    existing_keys = unpack_bin_to_dict(bin_path) if os.path.exists(bin_path) else {}
    
    if depot_id in existing_keys:
        current_key = existing_keys[depot_id].lower()
        if current_key == key_hex:
            print(f"Depot {depot_id} is already in database with identical key.")
            print("RESULT:DUPLICATE")
            sys.exit(0)
        else:
            print(f"CONFLICT: Depot {depot_id} already exists with different key: current={current_key}, submitted={key_hex}")
            print("RESULT:CONFLICT_REQUIRES_MANUAL_REVIEW")
            sys.exit(2)
            
    # 4. Insert and repack
    existing_keys[depot_id] = key_hex
    count = pack_keys_to_bin(existing_keys, bin_path)
    print(f"SUCCESS: Merged Depot {depot_id} ({depot_type}) for {game_title}. Total entries: {count}")
    print(f"GAME_TITLE:{game_title}")
    print(f"DEPOT_ID:{depot_id}")
    print(f"APP_ID:{app_id}")
    print("RESULT:MERGED")

if __name__ == '__main__':
    main()
