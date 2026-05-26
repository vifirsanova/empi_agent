#!/usr/bin/env python3
import sys
import json
import tomli
from pathlib import Path
import openai

CONFIG_PATH = Path(__file__).parent.parent / "config" / "agent_config.toml"
with open(CONFIG_PATH, "rb") as f:
    config = tomli.load(f)["llm"]

client = openai.OpenAI(
    api_key=config["api_key"],
    base_url=config["api_base"],
    project=config.get("folder_id", "")
)

def get_model_name():
    """Yandex Cloud uses gpt:// prefix, others use model name directly."""
    folder = config.get("folder_id", "")
    model = config["model"]
    if folder and "yandex" in config.get("api_base", ""):
        return f"gpt://{folder}/{model}"
    return model

def answer(message):
    response = client.responses.create(
        model=get_model_name(),
        temperature=config["temperature"],
        input=message,
        max_output_tokens=config["max_tokens"]
    )
    return response.output_text

def main():
    input_data = json.loads(sys.stdin.read())
    prompt = input_data.get("prompt", "")
    
    result = {"text": answer(prompt)}
    sys.stdout.write(json.dumps(result))

if __name__ == "__main__":
    main()
