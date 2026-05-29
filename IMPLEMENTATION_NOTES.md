# Speech Clarity & Tone Refine Implementation Notes

## Status: Phase 1 Foundation

### Challenge Identified
The split requires:
1. New `speech_clarity` and `tone_refine` products
2. New DSP for each artifact detector (DeEsser, DePolosive, DeBreath, DeMud, Harshness, Smooth)
3. New help content and version entries
4. New detection primitives

**This is a significant undertaking.** Rather than scaffolding everything at once with missing pieces, recommend a phased approach:

### Recommended Path Forward

#### Phase 1A: Understand Current Detection (This Week)
- Audit the existing Cleanup processor's detection (already implemented)
- Understand ClarityDsp band-processing approach
- Document how to extract/reuse detection logic

**Outcome:** Clear roadmap for adapting existing code

#### Phase 1B: Create Speech Clarity as Cleanup Variant (This Week)
- Duplicate Cleanup processor → Speech Clarity
- Keep existing detection, but expose 3 independent dials:
  - Sibilance strength (gates deEss)
  - Plosive strength (gates plosive reduction)
  - Breath strength (gates breath reduction)
- Add intensity-based LED feedback (reuse existing activity lights)
- Test with real audio

**Outcome:** Working product with split controls + LED feedback, reusing proven detection

#### Phase 1C: Refactor Detection After Validation (Later)
- Once Speech Clarity works and detection accuracy is validated
- Extract detection into standalone utilities (VxStudioArtifactDetectors.h)
- Create Tone Refine similarly
- Both will share detection infrastructure

**Outcome:** Clean architecture without the risk of breaking existing code

---

## Why This Order?

1. **Reuses existing code:** Cleanup's detection already works, proven in field
2. **Faster validation:** Test the control design with real users, real audio
3. **Lower risk:** No new DSP engines until we validate the concept
4. **Cleaner refactoring:** Extract utilities after we know what to extract
5. **Maintains momentum:** Ship working product faster

---

## Key Question for You

**Do you want to:**
- **Option A:** Ship Speech Clarity quickly as a Cleanup variant with split controls + LED (Phase 1B only), then refactor later
- **Option B:** Wait for full architecture with custom detection utilities (current approach, slower but cleaner from day 1)

Recommend **Option A** — get the UI/control design validated with real users, then optimize detection.

---

## Files Created So Far

- `/ARCHITECTURE_SPEECH_CLARITY_TONE_REFINE.md` — Full design doc (still valid)
- `VxStudioArtifactDetectors.h` — Detection utilities (needs infrastructure fixes, defer)
- `VxSpeechClarityProcessor.h/cpp` — Skeleton (incomplete without help/versions)

## Next Steps

1. **Decision:** Which approach (A or B)?
2. **If A:** Copy Cleanup → Speech Clarity, add split controls + LED
3. **If B:** Add missing help/versions, complete the detector header, continue current path
