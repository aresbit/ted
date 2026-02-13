#!/usr/bin/env python3
import json
import sys

def main():
    jsonl_file = "/data/data/com.termux/files/home/.claude/projects/-data-data-com-termux-files-home-MateBot-ted/4c383291-b76a-49a7-9399-a78ccd8f2470.jsonl"

    with open(jsonl_file, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                data = json.loads(line)
                msg_type = data.get('type')
                if msg_type == 'assistant':
                    message = data.get('message', {})
                    content = message.get('content', [])
                    if isinstance(content, list):
                        for item in content:
                            if isinstance(item, dict) and item.get('type') == 'thinking':
                                thinking = item.get('thinking', '')
                                if thinking:
                                    print("=== THINKING ===")
                                    print(thinking[:2000])
                                    print()
            except json.JSONDecodeError:
                pass

if __name__ == '__main__':
    main()