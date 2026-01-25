#!/usr/bin/env python3
"""
Text Analyzer for neurodiversity assessment - FIXED VERSION
Analyzes text complexity for people with ASD and ADHD
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

# ====== CRITICAL: Suppress ALL warnings before anything else ======
warnings.filterwarnings("ignore")
if not sys.warnoptions:
    warnings.simplefilter("ignore")
    os.environ["PYTHONWARNINGS"] = "ignore"

# Redirect stderr to suppress any remaining output
class NullWriter:
    def write(self, x): pass
    def flush(self): pass

# Temporarily replace stderr during imports
original_stderr = sys.stderr
sys.stderr = NullWriter()

# Now import with suppressed warnings
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

# Restore stderr
sys.stderr = original_stderr

# Setup SILENT logging
logging.basicConfig(
    level=logging.CRITICAL,  # Only show critical errors
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[logging.NullHandler()]  # Don't output anything
)
logger = logging.getLogger(__name__)
logger.propagate = False  # Don't propagate to root logger


class TextAnalyzer:
    """Text analyzer with configurable metrics - SILENT version"""
    
    def __init__(self, config_path: Optional[str] = None):
        """
        Initialize analyzer with configuration
        
        Args:
            config_path: Path to TOML configuration file
        """
        self.config = self._load_config(config_path)
        self.nlp = None
        
        # Don't log initialization
        self._initialize_models()
    
    def _load_config(self, config_path: Optional[str]) -> Dict[str, Any]:
        """Load configuration from TOML file - SILENT"""
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
        """Initialize NLP models - COMPLETELY SILENT"""
        language = self.config['system']['default_language']
        
        # Initialize spaCy model if available
        if spacy:
            model_name = self.config['languages'].get(language, 'en_core_web_sm')
            try:
                # Ultra-silent load
                import warnings
                with warnings.catch_warnings():
                    warnings.simplefilter("ignore")
                    # Load with minimal components
                    self.nlp = spacy.load(
                        model_name, 
                        disable=["parser", "ner", "lemmatizer", "attribute_ruler"]
                    )
            except Exception:
                self.nlp = None
        else:
            self.nlp = None
    
    def analyze(self, text: str) -> Dict[str, Any]:
        """
        Analyze text and return all computed metrics
        
        Args:
            text: Input text for analysis
            
        Returns:
            Dictionary with all computed metrics
        """
        if not text or not text.strip():
            return {"error": "Empty text provided"}
        
        # Check text length
        max_length = self.config['system']['max_text_length']
        if len(text) > max_length:
            text = text[:max_length]
        
        try:
            start_time = time.time()
            
            # Create spaCy document if model is loaded
            spacy_doc = self.nlp(text) if self.nlp else None
            
            # Compute all metrics
            result = {}
            
            # 1. Readability metrics using textstat (always works)
            if textstat:
                result.update(self._compute_readability_metrics(text))
            
            # 2. Basic text statistics
            result.update(self._compute_basic_stats(text))
            
            # 3. Structural metrics
            result.update(self._compute_structural_metrics(text))
            
            # 4. Lexical metrics
            result.update(self._compute_lexical_metrics(text))
            
            # 5. Autism support metrics (requires spaCy)
            if spacy_doc:
                result.update(self._compute_autism_metrics(spacy_doc))
            
            # Add metadata
            result["metadata"] = {
                "processing_time_seconds": time.time() - start_time,
                "text_length_characters": len(text),
                "text_length_words": len(text.split()),
                "language": self.config['system']['default_language'],
                "spacy_available": spacy_doc is not None
            }
            
            # Remove any empty/None values
            result = {k: v for k, v in result.items() if v is not None and v != {}}
            
            return result
            
        except Exception as e:
            return {"error": f"Analysis failed: {str(e)}"}
    
    def _compute_readability_metrics(self, text: str) -> Dict[str, Any]:
        """Compute readability metrics using textstat"""
        try:
            if not textstat:
                return {}
            return {
                # Core readability metrics
                "flesch_kincaid_grade": textstat.flesch_kincaid_grade(text),
                "flesch_reading_ease": textstat.flesch_reading_ease(text),
                "gunning_fog_index": textstat.gunning_fog(text),
                "smog_index": textstat.smog_index(text),
                "automated_readability_index": textstat.automated_readability_index(text),
                "coleman_liau_index": textstat.coleman_liau_index(text),
                "dale_chall_score": textstat.dale_chall_readability_score(text),
                "linsear_write_score": textstat.linsear_write_formula(text),
                
                # Additional readability metrics
                "difficult_word_count": textstat.difficult_words(text),
                "text_standard": textstat.text_standard(text),
            }
        except Exception:
            return {}
    
    def _compute_basic_stats(self, text: str) -> Dict[str, Any]:
        """Compute basic text statistics"""
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
            else:
                # Fallback to simple calculations
                words = text.split()
                sentences = [s.strip() for s in re.split(r'[.!?]+', text) if s.strip()]
                return {
                    "character_count": len(text),
                    "word_count": len(words),
                    "sentence_count": len(sentences),
                }
        except Exception:
            # Fallback to simple calculations
            words = text.split()
            sentences = [s.strip() for s in re.split(r'[.!?]+', text) if s.strip()]
            return {
                "character_count": len(text),
                "word_count": len(words),
                "sentence_count": len(sentences),
            }
    
    def _compute_structural_metrics(self, text: str) -> Dict[str, Any]:
        """Compute structural complexity metrics"""
        try:
            paragraphs = [p for p in text.split('\n\n') if p.strip()]
            sentences = [s.strip() for s in re.split(r'[.!?]+', text) if s.strip()]
            
            if not sentences:
                return {}
            
            # Detect structural elements
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
    
    def _compute_lexical_metrics(self, text: str) -> Dict[str, Any]:
        """Compute lexical diversity metrics"""
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
            
            # Approximate lexical diversity metrics for long texts
            if total_words >= 50:  # Minimum for meaningful MTLD-like calculation
                # Simple approximation of lexical diversity
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
    
    def _compute_autism_metrics(self, spacy_doc) -> Dict[str, Any]:
        """Compute autism-relevant linguistic metrics"""
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


# ====== MAIN ENTRY POINT - GUARANTEED CLEAN EXIT ======
def main():
    import sys
    import json
    
    # Read JSON from stdin
    try:
        input_json = json.loads(sys.stdin.read())
        text = input_json.get("text", "")
    except:
        # Fallback: treat input as raw text
        if not sys.stdin.isatty():
            text = sys.stdin.read().strip()
        elif len(sys.argv) > 1:
            text = sys.argv[1]
        else:
            text = ""
    
    # Suppress all output to stderr
    original_stderr = sys.stderr
    sys.stderr = NullWriter()
    
    result = {"error": "Unknown error in execution"}
    
    try:
        if text:
            # Initialize and analyze
            analyzer = TextAnalyzer()
            result = analyzer.analyze(text)
        else:
            result = {"error": "No text provided"}
        
    except Exception as e:
        # Catch and include in JSON response
        result = {"error": f"Unexpected error: {str(e)}"}
    
    finally:
        # ALWAYS output JSON, even on failure
        try:
            sys.stdout.write(json.dumps(result, ensure_ascii=False, separators=(',', ':')))
        except:
            # Even if JSON serialization fails
            sys.stdout.write('{"error": "JSON serialization failed"}')
    
    # Restore stderr
    sys.stderr = original_stderr
    
    # CRITICAL: Exit with code 0 NO MATTER WHAT
    sys.exit(0)

if __name__ == "__main__":
    main()
