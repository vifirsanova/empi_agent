# EMPI Agent Framework

Multi-agent framework for adaptive educational content. Analyzes text + user needs → generates accessible HTML.

<img width="2555" height="1519" alt="image" src="https://github.com/user-attachments/assets/152b3a2e-1bca-42dc-aa82-465b21c69c14" />

## Quick Start

```bash
git clone https://github.com/vifirsanova/empi_agent
cd empi_agent

# Install dependencies
pip install -r requirements.txt
python -m spacy download en_core_web_sm

# Build
mkdir build && cd build
cmake ..
make -j4

# Run CLI
./orchestrate_agents -i ../data/sample.txt

# Run GUI (requires Qt6)
cmake .. -DBUILD_GUI=ON && make
./empi_gui
```

## Configuration

`config/agent_config.toml`:

```toml
[llm]
api_base = "https://api.openai.com/v1"
api_key = ""                    # Empty → fallback to local/mock
model = "gpt-4o-mini"
max_tokens = 8000
temperature = 0.7
local_model_path = "models/Phi-3-mini-4k-instruct-q4.gguf"
```

**Fallback chain:** Cloud API → Local model → Mock HTML (always works).

## CLI Usage

```bash
./orchestrate_agents -i input.txt [-o output.html] [-p "prompt"] [-c config.toml]
```

## Architecture

### φ-ψ Handler Pattern

`UniversalAgent` base class implements EMPI protocol with function pairs:

```mermaid
flowchart LR
    Input[EMPI Message] --> PHI[φ-function\ndata extraction]
    PHI --> State[(Agent State)]
    PHI --> Extracted[Extracted Data]
    Extracted --> PSI[ψ-function\ndata processing]
    PSI --> Output[EMPI Message]
    State --> PSI
```

### Message Structure

```json
{
  "header": {
    "protocol": "EMPI/1.0",
    "message_id": "msg_1234567890_text_analyzer",
    "timestamp": "1234567890",
    "agent_id": "text_analyzer",
    "task_type": "text_metrics"
  },
  "payload": {
    "metadata": {"source": "text_analyzer"},
    "data": {}
  }
}
```

### Orchestration

TextAnalyzer and FeedbackAgent run in parallel, then InterfaceGenerator executes:

```mermaid
flowchart TD
    Start([Start]) --> Input[Educational text]
    Start --> Dialog[User dialogue]
    
    subgraph Parallel[Parallel execution]
        Input --> TA[TextAnalyzer]
        Dialog --> FA[FeedbackAgent]
        
        TA --> TM[Compute text metrics]
        FA --> UP[Generate user profile]
    end
    
    TM --> IG[InterfaceGenerator]
    UP --> IG
    
    IG --> HTML[Generate adaptive HTML]
    HTML --> Done([Done])
```

## Agents

### TextAnalyzer
Computes 20+ readability metrics via Python (`textstat` + `spaCy`).

**Sample output:**
```json
{
  "status": "success",
  "metrics": {
    "flesch_kincaid_grade": 8.2,
    "flesch_reading_ease": 65.3,
    "gunning_fog": 9.1,
    "smog_index": 7.8,
    "dale_chall_score": 7.2,
    "difficult_words": 3
  },
  "complexity_label": "moderate",
  "accessibility_level": "medium"
}
```

### FeedbackAgent
Analyzes dialog history via LLM to extract user needs.

**Sample output:**
```json
{
  "status": "success",
  "analysis": {
    "sentiment": "neutral",
    "topics": ["ADHD", "focus"],
    "satisfaction_score": 0.5,
    "complaints": ["long paragraphs difficult to focus on"],
    "feedback_summary": "User has ADHD and struggles with long paragraphs."
  }
}
```

### InterfaceGenerator
Generates HTML using local LLM or cloud API.

## GUI Features
- Drag & drop file upload (PDF, txt, plain input)
- Preset adaptation prompts (ADHD-friendly, Dyslexia-friendly, For children, Beginner)
- Split view: chat + live preview
- Language toggle (EN/RU)
- Download/export HTML

## Testing Suite

### Test Structure
```
tests/
├── test_llm_client.cpp
├── test_interface_generator.cpp
├── test_pipeline.cpp
├── test_accessibility.js
└── user_profiles.json
```

### Unit & Integration Tests

| Test | Purpose |
|------|---------|
| `test_llm_client.cpp` | Validates LLM availability and JSON parsing |
| `test_interface_generator.cpp` | Tests HTML generation with mock metrics |
| `test_pipeline.cpp` | End-to-end orchestration of all agents |

### Accessibility Validation (`test_accessibility.js`)

WCAG 2.1 compliance checker:

| Criterion | Description | Level |
|-----------|-------------|-------|
| 1.1.1 | Images have alt text | A |
| 1.3.1 | Heading hierarchy not skipped | A |
| 1.4.3 | Sufficient text contrast | AA |
| 2.4.4 | Links have meaningful text | A |
| 2.4.7 | Visible focus indicators | AA |
| 4.1.1 | No duplicate IDs | A |

```bash
npm install jsdom html-validator
node test_accessibility.js --json
```

### User Profiles (`user_profiles.json`)

100 synthetic profiles covering:

**Single conditions:** Dyslexia, ADHD, ASD (mild/moderate/marked)

**Comorbid:** ASD+ADHD, Dyslexia+ADHD, ASD+Dyslexia, Triple condition

**Additional:** Light sensitivity, auditory sensitivity, scotopic sensitivity

Each profile includes:
- `natural_language_prompt` — user's own words
- `cognitive_profile` — condition, severity, deficits
- `accessibility_settings` — actionable CSS/layout rules

### Running Tests

```bash
cd build
cmake .. -DBUILD_TESTS=ON
make

# Individual tests
./test_llm_client
./test_interface_generator
./test_pipeline

# All tests
ctest --output-on-failure
```

## Test Data

Real accessibility documentation from W3C WAI, WAI tutorials, and WebAIM — used for testing text adaptation across diverse content types.

## Requirements

- C++17, CMake 3.14+
- Python 3.8+ with `spacy`, `textstat`, `openai`, `tomli`
- Node.js with `jsdom`, `html-validator` (optional)
- Qt6 (optional, for GUI)

## License

MIT
