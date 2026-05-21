#!/bin/bash
# Minimal Phase 2 Setup — Use existing corpus + synthetic data
# Skip large LibriSpeech download; use existing voice samples instead

CORPUS_ROOT="/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus"

echo "=== Minimal Phase 2 Setup ==="
echo "Using existing corpus + synthetic generators"
echo ""

# 1. Check existing corpus
echo "✓ Existing voice corpus:"
ls -lh "$CORPUS_ROOT/wav/" | grep -v total | awk '{print "  " $9 " (" $5 ")"}'
echo ""

# 2. Generate synthetic reverb (for Deverb testing)
echo "→ Generating reverberant speech (for Deverb)..."
python3 "$CORPUS_ROOT/whamr_subset/generate_synthetic_reverb.py" 2>&1 | grep -E "✓|✗|Processing"
echo ""

# 3. Generate synthetic noisy speech (for Denoiser testing)
echo "→ Generating noisy speech at various SNRs (for Denoiser)..."
python3 "$CORPUS_ROOT/dns_challenge/generate_noisy_speech.py" 2>&1 | grep -E "✓|✗|Processing"
echo ""

# 4. Summary
echo "=== Phase 2 Test Data Ready ==="
echo ""
echo "Test files available:"
echo "  Baseline (existing):"
find "$CORPUS_ROOT/wav" -name "*.wav" -type f | wc -l | awk '{print "    " $1 " WAV files"}'
echo ""

echo "  Reverberant (synthetic):"
find "$CORPUS_ROOT/whamr_subset" -name "*.wav" -type f 2>/dev/null | wc -l | awk '{print "    " $1 " RT60 variants"}'
echo ""

echo "  Noisy (synthetic):"
find "$CORPUS_ROOT/dns_challenge" -name "*.wav" -type f 2>/dev/null | wc -l | awk '{print "    " $1 " SNR variants"}'
echo ""

echo "Total test duration: ~4 min baseline + synthetic variations"
echo "This is sufficient for Phase 2 A/B listening tests."
echo ""
echo "Next: Run quick_test_audit.sh or individual product audits"
