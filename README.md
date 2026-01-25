# EMPI Text Analyzer

Text analysis agent for neurodiversity assessment (ASD/ADHD support).

## What it does
- Analyzes text complexity and readability
- Computes 20+ readability metrics (Flesch-Kincaid, Gunning Fog, etc.)
- Provides complexity labels (simple/moderate/complex)
- Estimates accessibility levels for neurodiverse readers

## Quick Start
```bash
# Build
mkdir build && cd build
cmake .. && make

# Run tests
./test_text_analyzer

# Sample usage via Python
cd integrations
python3 text_analyzer.py "Your text here"
```

## Dependencies
- Python 3.8+ with: `pip install spacy textstat`
- C++17 compiler
- CMake 3.14+

## Files
- `src/agents/TextAnalyzer.cpp` - C++ EMPI agent
- `integrations/text_analyzer.py` - Python analysis engine
- `tests/test_text_analyzer.cpp` - Test suite

## License
MIT

## How to cite
```
@misc{empiagent2026,
  author = {V. Firsanova},
  title = {EMPI Agent Codebase},
  year = {2026},
  publisher = {GitHub},
  howpublished = {\url{https://github.com/vifirsanova/empi_agent}}
}
```

