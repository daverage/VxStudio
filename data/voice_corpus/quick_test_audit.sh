#!/bin/bash
# Quick Phase 2 Audit Testing Script
# Run individual product audits using generated test data

CORPUS_ROOT="/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus"
BUILD_DIR="/Users/andrzejmarczewski/Documents/GitHub/VxStudio/build"

echo "VxStudio Phase 2 - Quick Audit Testing"
echo "========================================"
echo ""

# Check build exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "✗ Build directory not found: $BUILD_DIR"
    echo "  Run: cd /Users/andrzejmarczewski/Documents/GitHub/VxStudio && cmake --build build -j4"
    exit 1
fi

# 1. Deverb WPE Testing
echo "[1] Deverb WPE Algorithm Audit"
echo "==============================="
echo "Test file: $CORPUS_ROOT/wav/churchill_be_ye_men_of_valour.wav"
echo ""

if [ -f "$CORPUS_ROOT/wav/churchill_be_ye_men_of_valour.wav" ]; then
    if [ -f "$BUILD_DIR/VXDeverbMeasure" ]; then
        echo "→ Running Deverb on test sample..."
        OUTPUT="/tmp/deverb_test_output.wav"
        "$BUILD_DIR/VXDeverbMeasure" \
            "$CORPUS_ROOT/wav/churchill_be_ye_men_of_valour.wav" \
            "$OUTPUT" \
            voice 0.5 1.0

        if [ -f "$OUTPUT" ]; then
            echo "✓ Output: $OUTPUT"
            echo "  A/B listen: original vs processed"
            echo "  Note: Use your DAW to compare reverb reduction & speech clarity"
        fi
    else
        echo "⚠ VXDeverbMeasure not built yet"
    fi
else
    echo "✗ Test file not found: $CORPUS_ROOT/wav/churchill_be_ye_men_of_valour.wav"
fi

echo ""

# 2. Denoiser Testing (OM-LSA)
echo "[2] Denoiser OM-LSA Audit"
echo "=========================="
echo ""

if [ -f "$CORPUS_ROOT/dns_challenge/generate_noisy_speech.py" ]; then
    echo "→ Generate noisy test samples first:"
    echo "  python3 $CORPUS_ROOT/dns_challenge/generate_noisy_speech.py"
    echo ""
    echo "Then test Denoiser:"
    echo "  $BUILD_DIR/VXDenoiserMeasure <noisy.wav> <denoised.wav> voice 0.5 1.0"
else
    echo "⚠ Noisy speech generator not ready yet"
fi

echo ""

# 3. DeepFilterNet Testing
echo "[3] DeepFilterNet DFN3 vs DFN2 Audit"
echo "====================================="
echo ""

if [ -f "$BUILD_DIR/VXDeepFilterNetMeasure" ]; then
    echo "→ Process test samples through DeepFilterNet:"
    echo "  $BUILD_DIR/VXDeepFilterNetMeasure <input.wav> <output.wav> voice 1.0 1.0"
    echo ""
    echo "Check for:"
    echo "  - Frame length validation (should pass without errors)"
    echo "  - Speech safety gating (framework integration check)"
    echo "  - Clarity preservation (A/B listen)"
else
    echo "⚠ VXDeepFilterNetMeasure not built yet"
fi

echo ""

# 4. Leveler Testing (Offline Analysis)
echo "[4] Leveler Offline Analysis Audit"
echo "==================================="
echo ""

if [ -f "$BUILD_DIR/VXLevelerMeasure" ]; then
    echo "→ Test Leveler offline analysis:"
    echo "  $BUILD_DIR/VXLevelerMeasure <input.wav> <output.wav> voice 0.0 0.0"
    echo ""
    echo "Verify:"
    echo "  - Analysis completes without crashes"
    echo "  - Output level is within ±2 LUFS of expected target"
else
    echo "⚠ VXLevelerMeasure not built yet"
fi

echo ""

# 5. OptoComp/Finish Testing
echo "[5] OptoComp/Finish LA-2A Verification"
echo "======================================"
echo ""

echo "→ Inspect LA-2A time constants:"
echo "  grep -n 'ATTACK_TIME\|RELEASE_TIME' $BUILD_DIR/../Source/vxstudio/framework/VxStudioOptoCompressorLA2A.cpp"
echo ""
echo "Validate:"
echo "  - Attack: ~10ms"
echo "  - Release (50%): ~60ms"
echo "  - Release (full): 0.5-5s (two-stage)"
echo "  - Ratio: ~3:1"
echo ""
echo "✓ Time constants already verified in Phase 1"
echo "  No code changes needed."

echo ""
echo "========================================"
echo "Dataset Status"
echo "========================================"

echo ""
echo "Reverberant (Deverb):"
if [ -d "$CORPUS_ROOT/whamr_subset" ]; then
    count=$(ls "$CORPUS_ROOT/whamr_subset"/*.wav 2>/dev/null | wc -l)
    echo "  ✓ Synthetic reverb folder ready"
    echo "    Generated files: $count"
else
    echo "  → Pending: python3 $CORPUS_ROOT/whamr_subset/generate_synthetic_reverb.py"
fi

echo ""
echo "Noisy (Denoiser):"
if [ -d "$CORPUS_ROOT/dns_challenge" ] && [ -f "$CORPUS_ROOT/dns_challenge/generate_noisy_speech.py" ]; then
    echo "  ✓ Noisy speech generator ready"
    echo "    Run: python3 $CORPUS_ROOT/dns_challenge/generate_noisy_speech.py"
else
    echo "  → Pending download"
fi

echo ""
echo "Diverse Samples (DeepFilterNet):"
if [ -d "$CORPUS_ROOT/librispeech_test" ]; then
    count=$(find "$CORPUS_ROOT/librispeech_test" -name "*.flac" 2>/dev/null | wc -l)
    if [ "$count" -gt 0 ]; then
        echo "  ✓ LibriSpeech downloaded: $count samples"
    else
        echo "  → Still downloading LibriSpeech..."
    fi
else
    echo "  → Pending download"
fi

echo ""
echo "========================================"
echo "Next Steps"
echo "========================================"
echo ""
echo "1. Wait for dataset downloads to complete"
echo "2. Generate synthetic test data:"
echo "   python3 $CORPUS_ROOT/dns_challenge/generate_noisy_speech.py"
echo "   python3 $CORPUS_ROOT/whamr_subset/generate_synthetic_reverb.py"
echo "3. Run audit tests above"
echo "4. Document findings in per-product audit reports"
echo ""
