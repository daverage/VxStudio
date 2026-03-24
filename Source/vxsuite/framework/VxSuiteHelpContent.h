#pragma once

#include <string_view>

namespace vxsuite::help {

struct HelpContent {
    std::string_view title;
    std::string_view html;
    std::string_view readmeSection;
};

inline constexpr HelpContent deepFilterNet {
    "VXDeepFilterNet Help",
    R"(
<h1>VXDeepFilterNet</h1>
<p>ML-powered voice isolation for heavy or complex background noise. It is the strongest noise-removal tool in the suite when classic denoisers cannot separate the voice cleanly enough.</p>
<h2>How to use it</h2>
<ul>
<li>Start with Clean around 55 to 70% and raise it until the noise falls back clearly.</li>
<li>Use Guard to restore natural speech detail if the result starts to sound over-processed.</li>
<li>Choose the model that behaves best on the material. DeepFilterNet 3 is usually the first choice.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Street or traffic noise: Clean 75%, Guard 65%.</li>
<li>Busy cafe or moving background: Clean 65%, Guard 75%.</li>
<li>Need a gentler isolation pass before other cleanup: Clean 50%, Guard 80%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Phone or camera speech recorded in public spaces.</li>
<li>Dialogue with mixed non-stationary interference.</li>
<li>First stage before deverb, cleanup, and finishing processors.</li>
</ul>)",
    "VXDeepFilterNet"
};

inline constexpr HelpContent denoiser {
    "VXDenoiser Help",
    R"(
<h1>VXDenoiser</h1>
<p>Broadband spectral denoiser for steady noise such as hiss, fans, HVAC, and room tone. It is designed to clean the bed without turning into a voice-isolation tool.</p>
<h2>How to use it</h2>
<ul>
<li>Raise Clean until the steady noise floor drops to a useful level.</li>
<li>If the voice loses harmonics or consonants, increase Guard.</li>
<li>Use it early in the chain, before deverb and finishing.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Light hiss: Clean 40%, Guard 75%.</li>
<li>Fan or HVAC: Clean 60%, Guard 70%.</li>
<li>Safety-first spoken voice: Clean 50%, Guard 85%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Podcast or narration with constant background air noise.</li>
<li>Camera audio with a steady room bed.</li>
<li>Follow-up cleanup after a stronger ML pass leaves residual steady noise.</li>
</ul>)",
    "VXDenoiser"
};

inline constexpr HelpContent subtract {
    "VXSubtract Help",
    R"(
<h1>VXSubtract</h1>
<p>Profile-guided subtractive denoiser for noises with a learnable fingerprint. It goes further than a blind denoiser when you can capture representative noise safely.</p>
<h2>How to use it</h2>
<ul>
<li>Enable Learn and play the noise by itself for about one to two seconds.</li>
<li>Turn Learn off to lock the profile.</li>
<li>Raise Subtract for more removal and raise Protect if the source becomes hollow or over-scooped.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Machine or room noise with a clean profile: Subtract 65%, Protect 80%.</li>
<li>More aggressive learned subtraction: Subtract 80%, Protect 70%.</li>
<li>Delicate speech preservation: Subtract 55%, Protect 88%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Air conditioner, projector, or other repeatable tonal/broadband beds.</li>
<li>Noise-only intro or pause available for learning.</li>
<li>Pre-clean stage before deverb and tonal shaping.</li>
</ul>)",
    "VXSubtract"
};

inline constexpr HelpContent deverb {
    "VXDeverb Help",
    R"(
<h1>VXDeverb</h1>
<p>Room-tail and reverb reduction for speech and general program material. It reduces smeared ambience while keeping direct sound usable.</p>
<h2>How to use it</h2>
<ul>
<li>Increase Reduce until the room tail pulls back without making the source papery.</li>
<li>Use Blend to restore low-body weight if the dereverb pass gets too lean.</li>
<li>Place it before proximity, tone shaping, and final dynamics.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Small reflective room: Reduce 50%, Blend 40%.</li>
<li>Distant voice in a live room: Reduce 70%, Blend 35%.</li>
<li>General ambience tidy-up: Reduce 35%, Blend 50%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Phone or camera speech recorded far from the source.</li>
<li>Dialogue in an untreated room.</li>
<li>Recovering clarity before cleanup and finishing.</li>
</ul>)",
    "VXDeverb"
};

inline constexpr HelpContent proximity {
    "VXProximity Help",
    R"(
<h1>VXProximity</h1>
<p>Close-mic tone shaping that adds a fuller, nearer vocal perspective after cleanup. It is a tone-and-space shaper, not a corrective denoiser.</p>
<h2>How to use it</h2>
<ul>
<li>Raise Closer to add weight and intimacy.</li>
<li>Use Air to stop the sound becoming overly thick or shut in.</li>
<li>Apply it after noise and room problems are already under control.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Thin distant voice: Closer 65%, Air 45%.</li>
<li>Warm spoken-word polish: Closer 55%, Air 40%.</li>
<li>Subtle intimacy lift: Closer 45%, Air 50%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Phone or room mics that feel too far away.</li>
<li>Voice tracks that need warmth after cleanup.</li>
<li>Pre-tone-shaping enhancement before VXTone.</li>
</ul>)",
    "VXProximity"
};

inline constexpr HelpContent cleanup {
    "VXCleanup Help",
    R"(
<h1>VXCleanup</h1>
<p>Corrective voice cleanup for mud, harshness, breaths, plosives, sibilance, and general tonal trouble. It is subtractive repair before enhancement.</p>
<h2>How to use it</h2>
<ul>
<li>Raise Cleanup until the distracting problems start to fall away.</li>
<li>Increase Body if the result becomes too thin.</li>
<li>Use Focus to steer the correction toward low-mid cleanup or more presence/air-region control.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Muddy spoken voice: Cleanup 55%, Body 55%, Focus 45%.</li>
<li>Harsh, breathy voice: Cleanup 60%, Body 50%, Focus 70%.</li>
<li>Light corrective tidy-up: Cleanup 35%, Body 55%, Focus 55%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Dialogue that needs cleanup before any enhancement.</li>
<li>Speech with boxiness, spit, or low-end bumps.</li>
<li>Preparation stage before proximity, tone, or final compression.</li>
</ul>)",
    "VXCleanup"
};

inline constexpr HelpContent tone {
    "VXTone Help",
    R"(
<h1>VXTone</h1>
<p>Simple bass and treble shaping with mode-aware shelf placement. It is the fast tonal balance stage after corrective cleanup.</p>
<h2>How to use it</h2>
<ul>
<li>Start from the centre position and make small moves.</li>
<li>Use Bass for weight and warmth, Treble for brightness and openness.</li>
<li>Prefer subtle shaping after cleanup and proximity, not before.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Need a little warmth: Bass 58%, Treble 50%.</li>
<li>Dull voice lift: Bass 50%, Treble 60%.</li>
<li>Balanced polish: Bass 55%, Treble 56%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Final tonal balance after cleanup.</li>
<li>Correcting a track that feels thin or dull.</li>
<li>Subtle pre-finish shaping before VXFinish or VXOptoComp.</li>
</ul>)",
    "VXTone"
};

inline constexpr HelpContent finish {
    "VXFinish Help",
    R"(
<h1>VXFinish</h1>
<p>Final polish and level control after cleanup and tone work. It combines finish compression, bounded body recovery, makeup, and limiting for a more produced result.</p>
<h2>How to use it</h2>
<ul>
<li>Raise Finish to increase compression, polish, and level control.</li>
<li>Use Body to recover useful weight after cleanup.</li>
<li>Gain is unity-centered: left is 50%, centre is 100%, right is 150%.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Light vocal polish: Finish 35%, Body 55%, Gain 100%.</li>
<li>Produced spoken voice: Finish 60%, Body 58%, Gain 110%.</li>
<li>Conservative final control after heavy cleanup: Finish 45%, Body 52%, Gain 100%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Last stage on cleaned speech.</li>
<li>Recovery and polish after corrective processing.</li>
<li>Fast final level shaping when you want more than a plain compressor.</li>
</ul>)",
    "VXFinish"
};

inline constexpr HelpContent optoComp {
    "VXOptoComp Help",
    R"(
<h1>VXOptoComp</h1>
<p>LA2A-style opto levelling and limiting with slower, smoother program-dependent gain reduction than VXFinish. It is for natural dynamic control with opto character.</p>
<h2>How to use it</h2>
<ul>
<li>Raise Peak Red. to drive more opto gain reduction.</li>
<li>Use Body for light post-compressor weight shaping.</li>
<li>Gain is unity-centered: left is 50%, centre is 100%, right is 150%.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Gentle levelling: Peak Red. 35%, Body 52%, Gain 100%.</li>
<li>Firm voice levelling: Peak Red. 55%, Body 54%, Gain 108%.</li>
<li>Limiter-style general control: Peak Red. 65%, Body 50%, Gain 100%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Natural spoken-word levelling.</li>
<li>Opto-style smoothing after cleanup and tone shaping.</li>
<li>General dynamic control when VXFinish feels too produced.</li>
</ul>)",
    "VXOptoComp"
};

inline constexpr HelpContent leveler {
    "VXLeveler Help",
    R"(
<h1>VXLeveler</h1>
<p>Adaptive level control with two distinct behaviours: Vocal Rider for speech-focused riding and Mix Leveler for broader programme smoothing. It is meant to feel more like automatic fader support than static compression.</p>
<h2>How to use it</h2>
<ul>
<li>Choose Vocal Rider when speech intelligibility is the priority.</li>
<li>Choose Mix Leveler when you want gentler overall programme control.</li>
<li>Use Level for how far the processor should even things out and Control for how assertively it reacts.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Vocal Rider for uneven dialogue: Level 65%, Control 60%.</li>
<li>Mix Leveler for broad programme smoothing: Level 50%, Control 45%.</li>
<li>Heavier rider action: Level 75%, Control 70%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Speech riding in mixed or inconsistent recordings.</li>
<li>Programme smoothing before final finish/limiting.</li>
<li>Long-form content where sections vary in level too much.</li>
</ul>)",
    "VXLeveler"
};

inline constexpr HelpContent analyser {
    "VXStudioAnalyser Help",
    R"(
<h1>VXStudioAnalyser</h1>
<p>Chain-aware dry-vs-wet spectrum analyser for VX Suite. Insert it last to inspect either the whole chain or one specific VX stage at a time.</p>
<h2>How to use it</h2>
<ul>
<li>Put the analyser at the end of the VX chain.</li>
<li>Select Full Chain to compare chain input against final output.</li>
<li>Click a stage in the left rail to inspect only that processor's dry-vs-wet spectrum.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>General readability: Avg Time 500 ms, Smoothing 1/3 OCT.</li>
<li>Fast transient inspection: Avg Time 125 ms, Smoothing 1/12 OCT.</li>
<li>Broad tonal overview: Avg Time 1000 ms, Smoothing 1 OCT.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Checking what one plugin in the chain is really changing.</li>
<li>Comparing whole-chain tone before and after processing.</li>
<li>Debugging over-bright, over-thin, or over-damped processing decisions.</li>
</ul>)",
    "VXStudioAnalyser"
};

inline constexpr HelpContent rebalance {
    "VXRebalance Help",
    R"(
<h1>VXRebalance</h1>
<p>Heuristic source-family rebalance for full mixes. It lets you gently lift or tuck vocals, drums, bass, guitar, and residual content without running a heavyweight stem-separation model.</p>
<h2>How to use it</h2>
<ul>
<li>Start with small moves on the source lane you want to rebalance.</li>
<li>Use Strength to scale the overall impact of all five moves together.</li>
<li>Treat it like broad corrective balance, not surgical stem extraction.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Bring vocals forward slightly: Vocals 60%, Strength 70%.</li>
<li>Tuck a boomy rhythm section: Bass 42%, Drums 45%, Strength 75%.</li>
<li>Open a busy rehearsal mix: Vocals 58%, Guitar 47%, Other 46%, Strength 65%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Quick rebalance of a rough stereo mix.</li>
<li>Making speech or lead lines feel more present without remixing stems.</li>
<li>Light source-family shaping before final tone and dynamics.</li>
</ul>)",
    "VXRebalance"
};

} // namespace vxsuite::help
