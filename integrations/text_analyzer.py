#!/usr/bin/env python3
"""
Text Analyzer for readability assessment
Uses textstat only - no heavy ML dependencies
"""

import json
import sys
import logging
import textstat
import re
import time
import tomli
import os
import warnings
from typing import Dict, Any, Optional
from pathlib import Path

warnings.filterwarnings("ignore")
if not sys.warnoptions:
    warnings.simplefilter("ignore")
os.environ["PYTHONWARNINGS"] = "ignore"

class NullWriter:
    """Devnull for stderr suppression"""
    def write(self, x): pass
    def flush(self): pass

original_stderr = sys.stderr
sys.stderr = NullWriter()

try:
    import textstat
except ImportError:
    textstat = None

try:
    import tomli
except ImportError:
    tomli = None

sys.stderr = original_stderr

logging.basicConfig(
    level=logging.CRITICAL,
    handlers=[logging.NullHandler()]
)
logger = logging.getLogger(__name__)
logger.propagate = False

class TextAnalyzer:
    """Analyzes text complexity using readability metrics only"""
    
    def __init__(self, config_path: Optional[str] = None):
        self.config = self._load_config(config_path)
    
    def _load_config(self, config_path: Optional[str]) -> Dict[str, Any]:
        default_config = {
            'system': {
                'max_text_length': 100000,
                'default_language': 'en'
            }
        }
        
        if config_path and Path(config_path).exists():
            try:
                with open(config_path, 'rb') as f:
                    user_config = tomli.load(f)
                for section, values in user_config.items():
                    if section in default_config:
                        default_config[section].update(values)
                    else:
                        default_config[section] = values
            except Exception:
                pass
        
        return default_config
    
    def analyze(self, text: str) -> Dict[str, Any]:
        if not text or not text.strip():
            return {"error": "Empty text provided"}
        
        max_length = self.config['system']['max_text_length']
        if len(text) > max_length:
            text = text[:max_length]
        
        try:
            start_time = time.time()
            result = {}
            
            if textstat:
                result.update(self._readability_metrics(text))
            
            result.update(self._basic_stats(text))
            result.update(self._structural_metrics(text))
            result.update(self._lexical_metrics(text))
            
            result["metadata"] = {
                "processing_time_seconds": time.time() - start_time,
                "text_length_characters": len(text),
                "text_length_words": len(text.split()),
                "language": self.config['system']['default_language']
            }
            
            result = {k: v for k, v in result.items() if v is not None and v != {}}
            return result
            
        except Exception as e:
            return {"error": f"Analysis failed: {str(e)}"}
    
    def _readability_metrics(self, text: str) -> Dict[str, Any]:
        try:
            return {
                "flesch_kincaid_grade": textstat.flesch_kincaid_grade(text),
                "flesch_reading_ease": textstat.flesch_reading_ease(text),
                "gunning_fog_index": textstat.gunning_fog(text),
                "smog_index": textstat.smog_index(text),
                "automated_readability_index": textstat.automated_readability_index(text),
                "coleman_liau_index": textstat.coleman_liau_index(text),
                "dale_chall_score": textstat.dale_chall_readability_score(text),
                "linsear_write_score": textstat.linsear_write_formula(text),
                "difficult_word_count": textstat.difficult_words(text),
                "text_standard": textstat.text_standard(text),
            }
        except Exception:
            return {}
    
    def _basic_stats(self, text: str) -> Dict[str, Any]:
        try:
            if textstat:
                return {
                    "character_count": textstat.char_count(text),
                    "letter_count": textstat.letter_count(text),
                    "syllable_count": textstat.syllable_count(text),
                    "word_count": textstat.lexicon_count(text),
                    "sentence_count": textstat.sentence_count(text),
                    "polysyllable_count": textstat.polysyllabcount(text),
                }
        except Exception:
            pass
        
        words = text.split()
        sentences = [s.strip() for s in re.split(r'[.!?]+', text) if s.strip()]
        return {
            "character_count": len(text),
            "word_count": len(words),
            "sentence_count": len(sentences),
        }
    
    def _structural_metrics(self, text: str) -> Dict[str, Any]:
        try:
            paragraphs = [p for p in text.split('\n\n') if p.strip()]
            sentences = [s.strip() for s in re.split(r'[.!?]+', text) if s.strip()]
            
            if not sentences:
                return {}
            
            heading_pattern = re.compile(r'^#{1,3}\s+', re.MULTILINE)
            list_pattern = re.compile(r'^[\s]*[-*•]\s|^\d+[\.\)]\s', re.MULTILINE)
            
            avg_paragraph_words = len(text.split()) / len(paragraphs) if paragraphs else 0
            
            return {
                "paragraph_count": len(paragraphs),
                "paragraph_sentence_ratio": len(paragraphs) / len(sentences) if sentences else 0,
                "has_headings": bool(heading_pattern.search(text)),
                "has_lists": bool(list_pattern.search(text)),
                "list_item_count": len(list_pattern.findall(text)),
                "average_paragraph_length_words": avg_paragraph_words,
            }
        except Exception:
            return {}
    
    def _lexical_metrics(self, text: str) -> Dict[str, Any]:
        try:
            words = [w.lower() for w in text.split() if w.strip()]
            if not words:
                return {}
            
            unique_words = len(set(words))
            total_words = len(words)
            
            result = {
                "type_token_ratio": unique_words / total_words,
                "unique_word_count": unique_words,
                "unique_word_ratio": unique_words / total_words,
            }
            
            if total_words >= 50:
                segments = []
                segment_size = 10
                for i in range(0, total_words, segment_size):
                    segment = words[i:i + segment_size]
                    if segment:
                        segments.append(len(set(segment)) / len(segment))
                
                if segments:
                    result["lexical_diversity_score"] = sum(segments) / len(segments)
            
            return result
        except Exception:
            return {}

def main():
    try:
        input_json = json.loads(sys.stdin.read())
        text = input_json.get("text", "")
    except:
        if not sys.stdin.isatty():
            text = sys.stdin.read().strip()
        elif len(sys.argv) > 1:
            text = sys.argv[1]
        else:
            text = ""
    
    original_stderr = sys.stderr
    sys.stderr = NullWriter()
    
    result = {"error": "Unknown error in execution"}
    
    try:
        if text:
            analyzer = TextAnalyzer()
            result = analyzer.analyze(text)
        else:
            result = {"error": "No text provided"}
    except Exception as e:
        result = {"error": f"Unexpected error: {str(e)}"}
    finally:
        try:
            sys.stdout.write(json.dumps(result, ensure_ascii=False, separators=(',', ':')))
        except:
            sys.stdout.write('{"error": "JSON serialization failed"}')
    
    sys.stderr = original_stderr
    sys.exit(0)

if __name__ == "__main__":
    main()
