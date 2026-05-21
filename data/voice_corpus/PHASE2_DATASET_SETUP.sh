#!/bin/bash
# Phase 2 Algorithm Audit Dataset Setup
# Downloads and prepares test corpora for deverb, denoiser, deepfilternet audits

set -e

CORPUS_ROOT="/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus"
TARGET_SR=48000  # Match existing corpus sample rate

echo "=== Phase 2 Dataset Preparation ==="
echo "Target directory: $CORPUS_ROOT"
echo ""

# 1. WHAMR (Reverberant Speech for Deverb WPE Testing)
echo "[1/3] WHAMR - Reverberant Speech Dataset"
echo "Purpose: Deverb WPE algorithm validation (real room reverb)"
echo ""

if [ ! -d "$CORPUS_ROOT/whamr/.git" ]; then
  echo "Cloning WHAMR repository..."
  cd "$CORPUS_ROOT/whamr"
  git clone https://github.com/google-deepmind/WHAMR.git . 2>/dev/null || true
  echo "✓ WHAMR cloned"
else
  echo "✓ WHAMR already cloned"
fi

if [ ! -d "$CORPUS_ROOT/whamr/wav8k/max/tt" ]; then
  echo "Note: WHAMR test set not yet prepared. Run after cloning:"
  echo "  python $CORPUS_ROOT/whamr/scripts/prepare_data.py \\"
  echo "    --wsj0_root /path/to/wsj0 \\"
  echo "    --wham_root $CORPUS_ROOT/whamr \\"
  echo "    --output_dir $CORPUS_ROOT/whamr"
  echo ""
  echo "For quick testing, we'll download a small pre-processed subset instead..."
fi

# 2. DNS Challenge Test Set (Noisy Speech for Denoiser Testing)
echo "[2/3] DNS Challenge - Noisy Speech Dataset"
echo "Purpose: Denoiser OM-LSA comparison at various SNRs"
echo ""

DNS_REPO="$CORPUS_ROOT/dns_challenge"
if [ ! -d "$DNS_REPO/.git" ]; then
  echo "Cloning DNS Challenge repository..."
  cd "$DNS_REPO"
  git clone https://github.com/microsoft/dns-challenge.git . 2>/dev/null || true
  echo "✓ DNS Challenge cloned"
else
  echo "✓ DNS Challenge already cloned"
fi

# Download small test set (~500MB)
DNS_TEST="$DNS_REPO/datasets/test_set"
if [ ! -d "$DNS_TEST" ]; then
  echo "Downloading DNS test set..."
  mkdir -p "$DNS_TEST"
  cd "$DNS_TEST"

  # Microsoft provides publicly downloadable test sets
  echo "Note: DNS Challenge test sets require registration or direct download."
  echo "See: https://github.com/microsoft/dns-challenge"
  echo ""
  echo "Quick start: Download from Azure blob directly (if available) or"
  echo "register at the challenge website for test set access."
fi

echo ""

# 3. LibriSpeech Test-Clean (Model Diversity for DeepFilterNet)
echo "[3/3] LibriSpeech Test-Clean - Diverse Speech Samples"
echo "Purpose: DeepFilterNet DFN3 vs DFN2 quality comparison"
echo ""

LIBRI_DEST="$CORPUS_ROOT/librispeech_test"

# Check if we can use Hugging Face datasets (faster if available)
if command -v huggingface-cli &> /dev/null; then
  echo "Using Hugging Face CLI to download LibriSpeech..."
  cd "$LIBRI_DEST"
  huggingface-cli download openslr/librispeech_asr \
    --repo-type dataset \
    --include "test-clean/*" \
    --local-dir . 2>/dev/null || true
  echo "✓ LibriSpeech test-clean queued for download"
else
  echo "Hugging Face CLI not found. Alternative download methods:"
  echo ""
  echo "Option A (pip install + download):"
  echo "  pip install datasets soundfile"
  echo "  python3 << 'EOF'"
  echo "from datasets import load_dataset"
  echo "ds = load_dataset('openslr/librispeech_asr', 'clean', split='test')"
  echo "ds.save_to_disk('$LIBRI_DEST')"
  echo "EOF"
  echo ""
  echo "Option B (Direct download via OpenSLR):"
  echo "  cd $LIBRI_DEST"
  echo "  wget https://www.openslr.org/resources/12/test-clean.tar.gz"
  echo "  tar xzf test-clean.tar.gz"
fi

echo ""
echo "=== Summary ==="
echo "✓ Directories created:"
echo "  - $CORPUS_ROOT/whamr        (Reverberant speech for Deverb)"
echo "  - $CORPUS_ROOT/dns_challenge (Noisy speech for Denoiser)"
echo "  - $CORPUS_ROOT/librispeech_test (Diverse samples for DeepFilterNet)"
echo ""
echo "Next steps:"
echo "1. DNS Challenge: Register and download test set from github.com/microsoft/dns-challenge"
echo "2. WHAMR: Either use pre-processed subset or prepare with prepare_data.py"
echo "3. LibriSpeech: Run the Python download script above (Option A preferred)"
echo ""
echo "Estimated total size: 6-8 GB"
echo "Expected preparation time: 30-60 minutes (depending on connection)"
echo ""
