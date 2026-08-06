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
<p>Research-grade dereverberation using LRSV (Late-Reverberant Spectral Variance) with RT60 room-decay estimation and optional WPE (Weighted Prediction Error) enhancement for voice. Reduces reverberant tail and ambience while preserving direct sound clarity.</p>
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
<p>Directional microphone proximity model with real-time spectral analysis and adaptive filtering. Simulates the tonal character of moving a microphone closer to a source - adding weight, intimacy, and presence - without altering spatial location or introducing artifacts.</p>
<p>Closer also gently thins room tone as it rises, so the source pulls forward the way it would if the mic were genuinely closer - not just a bass boost. This adds a small amount of processing latency (about 16ms at 48kHz), compensated automatically by your host.</p>
<h2>How to use it</h2>
<ul>
<li>Raise Closer to add weight, intimacy, and a touch of dryness.</li>
<li>Use Air to stop the sound becoming overly thick or shut in.</li>
<li>Use Mud to balance bass depth versus low-mid boom, based on the source character.</li>
<li>Apply it after noise and room problems are already under control.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Thin distant voice: Closer 65%, Air 45%, Mud 50%.</li>
<li>Warm spoken-word polish: Closer 55%, Air 40%, Mud 55%.</li>
<li>Subtle intimacy lift: Closer 45%, Air 50%, Mud 48%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Phone or room mics that feel too far away.</li>
<li>Voice tracks that need warmth after cleanup.</li>
<li>Pre-tone-shaping enhancement before VXTone.</li>
</ul>)",
    "VXProximity"
};

inline constexpr HelpContent proximityClassic {
    "VXProximity Classic Help",
    R"(
<h1>VXProximity Classic</h1>
<p>A simplified two-control proximity simulator. <b>Closer</b> adds low-end body by emulating the proximity effect of a directional mic. <b>Air</b> adds upper-presence shimmer. For the three-control version with Mud compensation, use VX Proximity.</p>
<h2>How to use it</h2>
<ul>
<li>Raise Closer to add weight and intimacy to a distant or thin-sounding voice.</li>
<li>Use Air to prevent the sound becoming too thick or congested.</li>
<li>Apply it after noise and room problems are already under control.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Thin distant voice: Closer 65%, Air 45%.</li>
<li>Subtle warmth lift: Closer 40%, Air 35%.</li>
<li>Spoken-word polish: Closer 55%, Air 40%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Phone or room mics that feel too far away.</li>
<li>Voice tracks that need warmth after cleanup.</li>
<li>Situations where the three-dial version is more than needed.</li>
</ul>)",
    "proximity-classic"
};

inline constexpr HelpContent speech_clarity {
    "VXSpeechClarity Help",
    R"(
<h1>VXSpeechClarity</h1>
<p>Targeted speech-artifact cleanup for sibilance, plosives, and breath noise. It is the focused corrective stage for speech mechanics rather than broad tonal shaping.</p>
<h2>How to use it</h2>
<ul>
<li>Raise Sibilance to soften harsh /s/ and /z/ bursts without dulling the whole take.</li>
<li>Raise Plosive to reduce low-frequency consonant thumps from close mics.</li>
<li>Raise Breath to pull back obvious inhalations and wind-like noise between phrases.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Light de-essing only: Sibilance 35%, Plosive 0%, Breath 0%.</li>
<li>Close-mic spoken voice cleanup: Sibilance 30%, Plosive 40%, Breath 25%.</li>
<li>Podcast artifact control: Sibilance 45%, Plosive 35%, Breath 30%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Speech tracks with sharp consonants and close-mic pops.</li>
<li>Dialogue cleanup before tone shaping and final compression.</li>
<li>Mechanical voice cleanup when VXTone or VXFinish would be too broad.</li>
</ul>)",
    "VXSpeechClarity"
};

inline constexpr HelpContent tone {
    "VXTone Help",
    R"(
<h1>VXTone</h1>
<p>Bass, midrange, and treble shaping with mode-aware shelf and peak placement. It is the fast tonal balance stage after corrective cleanup.</p>
<h2>How to use it</h2>
<ul>
<li>Start from the centre position and make small moves.</li>
<li>Use Bass for weight and warmth, Treble for brightness and openness.</li>
<li>Use Mid to shape the presence region: in Vocal mode at 2 kHz for speech intelligibility; in General mode at 1 kHz for warmth.</li>
<li>Prefer subtle shaping after cleanup and proximity, not before.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Need a little warmth: Bass 58%, Treble 50%, Mid 50%.</li>
<li>Dull voice lift: Bass 50%, Treble 60%, Mid 55%.</li>
<li>Balanced polish: Bass 55%, Treble 56%, Mid 50%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Final tonal balance after cleanup.</li>
<li>Correcting a track that feels thin, dull, or lacks presence.</li>
<li>Subtle pre-finish shaping before VXFinish or VXOptoComp.</li>
</ul>)",
    "VXTone"
};

inline constexpr HelpContent tone_refine {
    "VXToneRefine Help",
    R"(
<h1>VXToneRefine</h1>
<p>Automatic tonal correction for mud, harshness, and general roughness. It is for targeted subtractive refinement when a track needs cleanup in specific problem regions without manual EQ moves.</p>
<h2>How to use it</h2>
<ul>
<li>Raise Mud to reduce boxy low-mid buildup.</li>
<li>Raise Harshness to soften brittle presence-region peaks.</li>
<li>Raise Smooth to apply transparent broad tonal calming after the main problems are under control.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Boxy room voice: Mud 45%, Harshness 20%, Smooth 10%.</li>
<li>Brittle spoken voice: Mud 15%, Harshness 45%, Smooth 20%.</li>
<li>General refinement pass: Mud 25%, Harshness 30%, Smooth 25%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Speech that feels muddy, brittle, or rough after basic cleanup.</li>
<li>Corrective refinement before finishing compression.</li>
<li>Faster guided tonal repair when manual EQ is too slow or fussy.</li>
</ul>)",
    "VXToneRefine"
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
<p>Professional LA2A-style opto levelling and limiting with a visible Pro switch, standalone behaviour control, stereo-link options, and slower, smoother program-dependent gain reduction than VXFinish. It is for natural dynamic control with opto character when you want engineer-facing control instead of guided finishing.</p>
<h2>How to use it</h2>
<ul>
<li>Raise Peak Red. to drive more opto gain reduction.</li>
<li>Use Body for light post-compressor weight shaping.</li>
<li>Gain is unity-centered: left is 50%, centre is 100%, right is 150%.</li>
<li>Leave Pro off for the simple guided view: Auto behaviour and linked stereo tracking stay engaged.</li>
<li>Turn Pro on to reveal Behavior and Stereo Link.</li>
<li>Behavior on Auto follows the program role; use Compress for classic levelling and Limit for firmer containment.</li>
<li>Keep Stereo Link high for classic linked stereo tracking, or lower it for dual-mono style response.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Simple vocal levelling: Peak Red. 35%, Body 52%, Gain 100%, Pro Off.</li>
<li>Firm vocal compression: Peak Red. 55%, Body 54%, Gain 108%, Pro On, Behavior Compress, Stereo Link 100%.</li>
<li>Limiter-style bus control: Peak Red. 65%, Body 50%, Gain 100%, Pro On, Behavior Limit, Stereo Link 100%.</li>
<li>Asymmetric stereo material: Peak Red. 45%, Body 50%, Gain 100%, Pro On, Behavior Compress, Stereo Link 0-25%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Natural spoken-word levelling.</li>
<li>Opto-style smoothing after cleanup and tone shaping.</li>
<li>General dynamic control when VXFinish feels too guided or produced.</li>
</ul>)",
    "VXOptoComp"
};

inline constexpr HelpContent leveler {
    "VXLeveler Help",
    R"(
<h1>VXLeveler</h1>
<p>Adaptive level control with two behaviours: Vocal Rider rides a voice toward a held reference level - freezing the fader in pauses instead of pumping room tone - and Mix Leveler smooths whole-programme level. Both share the same controls; it is meant to feel like automatic fader support, not static compression. The level trace shows the ride in action: the bright line is the fader (dimmed while parked), the dashed line is the level it is steering toward.</p>
<h2>How to use it</h2>
<ul>
<li>Choose Vocal Rider when speech intelligibility is the priority.</li>
<li>Choose Mix Leveler when you want gentler overall programme control.</li>
<li>Use Level for how far the processor should even things out and Control for how assertively it reacts.</li>
<li>Target rides the material hotter or quieter than its learned reference; Depth sets how far the ride reaches into pauses and quiet dips.</li>
<li>Route your music bus into the plugin's sidechain input and the Vocal Rider holds your vocal a set amount above the music - Target then sets how hot it sits. Remove the routing and it returns to riding the vocal against its own level.</li>
<li>In Offline analysis mode, press Analyze while the track plays to learn its level map; the rider then steers toward the analyzed reference.</li>
</ul>
<h2>Example settings</h2>
<ul>
<li>Vocal Rider for uneven dialogue: Level 65%, Control 60%, Target and Depth centred.</li>
<li>Vocal against a backing track: route the music into the sidechain, Level 70%, Target +2 dB to sit the vocal slightly hot.</li>
<li>Mix Leveler for broad programme smoothing: Level 50%, Control 45%.</li>
<li>Whole-song consistency: Mix Leveler, Analysis Offline, press Analyze while the song plays, then Level 60%.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Speech riding in mixed or inconsistent recordings.</li>
<li>Holding a vocal at a set level above a changing arrangement (sidechain).</li>
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

inline constexpr HelpContent repair {
    "VX Repair Help",
    R"(
<h1>VX Repair</h1>
<p>All-in-one voice repair assistant. Analyses your audio and automatically suggests which tools to enable and at what strength. It combines noise reduction, speech clarity cleanup, and dereverberation in a single guided workflow.</p>
<h2>How to use it</h2>
<ul>
<li>Click Analyse and play a representative section  -  at least five seconds with real programme material, not silence.</li>
<li>Repair detects the problems and sets the tools. Accept the suggestions, adjust to taste, or turn individual tools off.</li>
<li>Drag any strength knob to enable that tool automatically if it is currently off.</li>
<li>Use Listen on any tool row to hear only what that processor is removing.</li>
<li>Click Reset Analysis to start again from scratch.</li>
</ul>
<h2>Two-phase analysis</h2>
<p>If significant noise is detected in Phase 1, Repair runs a second five-second pass with noise reduction active. This lets the reverb and speech-clarity detectors work on a cleaner signal so their scores are not inflated by broadband noise.</p>
<h2>Noise mode  -  DSP vs DeepFilter</h2>
<p>The Noise tool has two modes selectable from the DeepFilter button in the Noise row.</p>
<ul>
<li><strong>DSP (default)</strong>  -  broadband spectral denoiser. Fast, low latency, works well for hiss, fans, HVAC, and steady room noise. Good for most podcast and narration work.</li>
<li><strong>DeepFilter</strong>  -  ML-powered voice isolation. Stronger on complex or non-stationary backgrounds such as crowd noise, traffic, or cafes. Requires the DeepFilter model to be installed via VX Deep Filter Net. Falls back silently to the DSP denoiser if the model is not available. When DeepFilter is selected it is also used as the reference denoiser during Phase 2 analysis.</li>
</ul>
<h2>Tool order</h2>
<p>The repair chain runs Noise first, then Speech Clarity, then Reverb. This order matters: cleaning noise before de-essing avoids false sibilance triggers, and de-essing before deverb stops the reverb tail from masking breath sounds.</p>
<h2>Practical scenarios</h2>
<ul>
<li>Podcast or narration with background hiss and slight room reverb: let Repair analyse and apply all three tools.</li>
<li>Phone or camera speech with strong interference: enable DeepFilter mode for heavier noise removal, then let Repair adjust reverb and clarity around it.</li>
<li>Voice already mostly clean: Repair will leave inactive tools off  -  use only what is needed.</li>
</ul>)",
    "VXRepair"
};

inline constexpr HelpContent rebalance {
    "VXRebalance Help",
    R"(
<h1>VXRebalance</h1>
<p>Confidence-driven source-family rebalance for full mixes. It estimates source ownership across time-frequency regions and lets you push or pull vocals, drums, bass, guitar, and residual content without stems.</p>
<h2>How to use it</h2>
<ul>
<li>Choose the Recording Type that best matches the source: Studio, Live, or Phone / Rough.</li>
<li>Start with small moves on the source lane you want to rebalance.</li>
<li>Use Strength to scale the overall impact of all five moves together.</li>
<li>Treat it as perceptual source rebalance, not perfect stem extraction.</li>
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

inline constexpr HelpContent tune {
    "VXTune Help",
    R"(
<h1>VXTune</h1>
<p>Intelligent vocal pitch correction that fixes pitch errors while leaving the performance - vibrato, bends, phrasing - untouched. It corrects the intended note only when it is confident the deviation is an error, and preserves expression by design.</p>
<h2>How to use it</h2>
<ul>
<li>Insert on a monophonic vocal. The status line shows the detected note, its offset in cents, confidence, and whether the engine is intervening.</li>
<li>Amount sets how much detected pitch error is removed.</li>
<li>Natural sets how readily movement counts as error rather than expression: left preserves everything human, right is tighter.</li>
<li>When unsure, the engine does nothing - a missed correction sounds like the singer; a wrong correction sounds like a malfunction.</li>
</ul>
<h2>Practical scenarios</h2>
<ul>
<li>Transparent intonation cleanup on lead or backing vocals.</li>
<li>Checking vocal intonation while tracking or comping.</li>
</ul>
<h2>Sidechain (optional)</h2>
<p>Route an instrumental into the sidechain input to help Key/Scale detection - VXTune learns the song's key faster and more reliably from full chord content than from the vocal alone. This is detection-only: the sidechain audio is never mixed into VXTune's output.</p>
<ul>
<li>Some hosts build a multichannel track (e.g. 4 channels: 2 for the vocal, 2 received from another track for the sidechain). If your track's master/parent send is set wider than stereo to match, those extra channels can reach your mix downstream of the plugin. Set the track's master/parent send to stereo (2 channels) unless you specifically intend to route more.</li>
<li>VXTune adds real processing latency for the pitch-shift itself (shown as the plugin's reported latency). If your host does not fully delay-compensate a sidechain feed the way it does a plain signal chain, a track feeding the sidechain can drift out of time-alignment with the corrected vocal once both are audible together. If you hear phasing/timing artifacts only when both the vocal and the sidechain source are playing (and not when either is soloed), check your host's delay compensation for that specific routing, or the receive/send point (post-fader is usually safer than pre-fader/pre-FX).</li>
</ul>)",
    "VXTune"
};

} // namespace vxsuite::help
