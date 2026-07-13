# Project Rules — Claude Code
# Global rules are in ~/.claude/CLAUDE.md — do not duplicate here.
# Keep lean: every token here costs quota on every prompt.

## Stack
# Primary language: C++ / Cmake
# Build command:    cmake --build build --parallel

## Conventions
# Add project-specific coding conventions here.
# e.g. "Use snake_case for all identifiers"
# e.g. "All public functions must have a docstring"

## Shared DSP — embedded copies rule
# VxRepair embeds DSP from other products by compiling their source files directly.
# Embedded DSP sources (as of now):
#   products/denoiser/dsp/        → used by VXDenoiser + VXRepair
#   products/deverb/dsp/          → used by VXDeverb + VXRepair
#   products/speech_clarity/dsp/  → used by VXSpeechClarity + VXRepair
#
# RULE: Any time you modify a DSP file in denoiser/dsp/, deverb/dsp/, or
# speech_clarity/dsp/, you MUST also rebuild and re-test VXRepair to confirm
# the change does not break the embedded usage. Check VxRepairProcessor.cpp
# for how that DSP is called and update the call-site if the interface changed.
