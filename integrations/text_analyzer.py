#!/usr/bin/env python3
"""
Text Analyzer for neurodiversity assessment
Analyzes text complexity for users with ASD and ADHD

Input: JSON via stdin with {"text": "content"} or raw text
Output: JSON with readability metrics and linguistic features
"""

import json
import sys
import logging
import textstat
import spacy
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
    """Devnull for stderr suppression during imports"""
    def write(self, x): pass
    def flush(self): pass

# Temporarily suppress stderr during imports
original_stderr = sys.stderr
sys.stderr = NullWriter()

try:
    import spacy
except ImportError:
    spacy = None

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
    """Analyzes text complexity and neurodiversity-relevant metrics"""
    
    def __init__(self, config_path: Optional[str] = None):
        """Initialize with optional TOML config"""
        self.config = self._load_config(config_path)
        self.nlp = None
        self._initialize_models()
    
    def _load_config(self, config_path: Optional[str]) -> Dict[str, Any]:
        """Load configuration from TOML file or use defaults"""
        default_config = {
            'system': {
                'max_text_length': 100000,
                'default_language': 'en'
            },
            'languages': {
                'en': 'en_core_web_sm',
                'ru': 'ru_core_news_sm'
            }
        }
        
        if config_path and Path(config_path).exists():
            try:
                with open(config_path, 'rb') as f:
                    user_config = tomli.load(f)
                # Merge with defaults
                for section, values in user_config.items():
                    if section in default_config:
                        default_config[section].update(values)
                    else:
                        default_config[section] = values
            except Exception:
                pass  # Silent fail
        
        return default_config
    
    def _initialize_models(self) -> None:
        """Initialize spaCy model if available (silently)"""
        if not spacy:
            self.nlp = None
            return
        
        language = self.config['system']['default_language']
        model_name = self.config['languages'].get(language, 'en_core_web_sm')
        
        try:
            with warnings.catch_warnings():
                warnings.simplefilter("ignore")
                # Load with minimal components for speed
                self.nlp = spacy.load(
                    model_name, 
                    disable=["parser", "ner", "lemmatizer", "attribute_ruler"]
                )
        except Exception:
            self.nlp = None
    
    def analyze(self, text: str) -> Dict[str, Any]:
        """Main analysis entry point - returns all metrics"""
        if not text or not text.strip():
            return {"error": "Empty text provided"}
        
        # Enforce length limit
        max_length = self.config['system']['max_text_length']
        if len(text) > max_length:
            text = text[:max_length]
        
        try:
            start_time = time.time()
            
            # Process with spaCy if available
            spacy_doc = self.nlp(text) if self.nlp else None
            
            # Collect all metrics
            result = {}
            
            if textstat:
                result.update(self._readability_metrics(text))
            
            result.update(self._basic_stats(text))
            result.update(self._structural_metrics(text))
            result.update(self._lexical_metrics(text))
            
            if spacy_doc:
                result.update(self._autism_metrics(spacy_doc))
            
            # Add metadata
            result["metadata"] = {
                "processing_time_seconds": time.time() - start_time,
                "text_length_characters": len(text),
                "text_length_words": len(text.split()),
                "language": self.config['system']['default_language'],
                "spacy_available": spacy_doc is not None
            }
            
            # Clean up empty values
            result = {k: v for k, v in result.items() if v is not None and v != {}}
            
            return result
            
        except Exception as e:
            return {"error": f"Analysis failed: {str(e)}"}
    
    def _readability_metrics(self, text: str) -> Dict[str, Any]:
        """All textstat readability scores"""
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
        """Character, word, sentence counts"""
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
        
        # Fallback calculations
        words = text.split()
        sentences = [s.strip() for s in re.split(r'[.!?]+', text) if s.strip()]
        return {
            "character_count": len(text),
            "word_count": len(words),
            "sentence_count": len(sentences),
        }
    
    def _structural_metrics(self, text: str) -> Dict[str, Any]:
        """Paragraphs, headings, lists"""
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
        """Vocabulary diversity metrics"""
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
            
            # Approximate lexical diversity for longer texts
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
    
    def _autism_metrics(self, spacy_doc) -> Dict[str, Any]:
        """Pronoun/anaphora density for ASD-relevant analysis"""
        try:
            # Count pronouns and determiners
            pronouns = sum(1 for token in spacy_doc if token.pos_ == "PRON")
            determiners = sum(1 for token in spacy_doc if token.pos_ == "DET")
            
            # Content tokens (exclude punctuation, spaces, symbols)
            content_tokens = [
                token for token in spacy_doc 
                if token.pos_ not in ("PUNCT", "SPACE", "SYM")
            ]
            token_count = len(content_tokens)
            
            if token_count == 0:
                return {}
            
            # Count anaphora (pronouns with morphological features)
            anaphora_count = 0
            for token in spacy_doc:
                if token.pos_ == "PRON":
                    morph = token.morph.to_dict()
                    if any(key in morph for key in ['Person', 'Number', 'Case']):
                        anaphora_count += 1
                elif token.pos_ == "DET" and token.dep_ != "det":
                    morph = token.morph.to_dict()
                    if morph.get('PronType') == 'Dem':
                        anaphora_count += 1
            
            return {
                "pronoun_density": pronouns / token_count,
                "determiner_density": determiners / token_count,
                "anaphora_density": anaphora_count / token_count,
                "content_token_count": token_count,
                "sentence_count": len(list(spacy_doc.sents)),
            }
        except Exception:
            return {}

def main():
    """Read input, analyze, output JSON, exit with code 0"""
    
    # Parse input (JSON or raw text)
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
