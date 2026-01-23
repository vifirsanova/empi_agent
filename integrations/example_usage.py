#!/usr/bin/env python3
"""
Generate example output from text file
"""

import json
import sys
from text_analyzer import TextAnalyzer

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <text_file>")
        sys.exit(1)
    
    try:
        with open(sys.argv[1], 'r', encoding='utf-8') as f:
            text = f.read().strip()
    except Exception as e:
        print(f"Error reading file: {e}")
        sys.exit(1)
    
    analyzer = TextAnalyzer()
    result = analyzer.analyze(text)
    
    # Save complete example
    example_data = {
        "input_text": text,
        "analysis_result": result
    }
    
    output_file = "example_output.json"
    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(example_data, f, indent=2, ensure_ascii=False)
    
    print(f"Example saved to '{output_file}'")
    print(f"Text length: {len(text)} chars, {len(text.split())} words")

if __name__ == "__main__":
    main()
