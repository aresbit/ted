#!/usr/bin/env python3
import json
import sys

def extract_content(content):
    """Extract text from message content which can be string or array of objects"""
    if isinstance(content, str):
        return content
    elif isinstance(content, list):
        texts = []
        for item in content:
            if isinstance(item, dict) and item.get('type') == 'text':
                texts.append(item.get('text', ''))
            elif isinstance(item, dict) and item.get('type') == 'thinking':
                texts.append(f"[Thinking: {item.get('thinking', '')[:100]}...]")
            elif isinstance(item, dict) and item.get('type') == 'tool_use':
                texts.append(f"[Tool call: {item.get('name')} {item.get('input', {})}]")
        return '\n'.join(texts)
    else:
        return str(content)

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 extract_session.py <jsonl_file>")
        sys.exit(1)

    jsonl_file = sys.argv[1]

    with open(jsonl_file, 'r', encoding='utf-8') as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                data = json.loads(line)
                msg_type = data.get('type')
                if msg_type == 'user' or msg_type == 'assistant':
                    role = 'User' if msg_type == 'user' else 'Assistant'
                    message = data.get('message', {})
                    content = message.get('content', '')
                    text = extract_content(content)
                    # Skip empty messages
                    if text.strip():
                        print(f"=== {role} ===")
                        print(text[:5000])  # Limit output
                        print()
                elif msg_type == 'file-history-snapshot':
                    # Skip metadata
                    pass
            except json.JSONDecodeError as e:
                print(f"Error parsing line {line_num}: {e}", file=sys.stderr)

if __name__ == '__main__':
    main()