#include <JuceHeader.h>
#include "VocalSynthEngine.h"

VocalSynthEngine::VocalSynthEngine() = default;

// ============================================================================
//  AudioSource
// ============================================================================

void VocalSynthEngine::prepareToPlay(int, double sampleRate)
{
    currentSampleRate = sampleRate;
    shouldStop = false;
    metroSampleCounter = 0.0;
    metroBeatInMeasure = 0;
    clickPlayPos = -1;
    generateClickBuffers();
}

void VocalSynthEngine::releaseResources()
{
    juce::ScopedLock sl(queueLock);
    pendingVoices.clear();
    activeVoices.clear();
}

// ============================================================================
//  generateClickBuffers
// ============================================================================

void VocalSynthEngine::generateClickBuffers()
{
    const int clickLen = (int)(currentSampleRate * 0.03);
    clickHi.setSize(1, clickLen);
    clickLo.setSize(1, clickLen);

    for (int i = 0; i < clickLen; ++i)
    {
        float t = (float)i / (float)currentSampleRate;
        float env = std::exp(-t * 120.0f);
        clickHi.setSample(0, i, env * std::sin(2.0f * juce::MathConstants<float>::pi * 1500.0f * t) * 0.45f);
        clickLo.setSample(0, i, env * std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f * t) * 0.30f);
    }
}

// ============================================================================
//  Metronome control
// ============================================================================

void VocalSynthEngine::setMetronomeEnabled(bool enabled)
{
    metronomeEnabled.store(enabled);
    if (!enabled)
    {
        clickPlayPos = -1;
        metroSampleCounter = 0.0;
        metroBeatInMeasure = 0;
    }
}

void VocalSynthEngine::setTempo(double bpm)
{
    metroBPM.store(juce::jlimit(20.0, 522.0, bpm));
}

void VocalSynthEngine::setTimeSignature(int n, int d)
{
    metroTimeSigNum.store(juce::jlimit(1, 15, n));
    metroTimeSigDen.store(juce::jlimit(2, 16, d));
}

void VocalSynthEngine::resetMetronome(double playheadBeats)
{
    // Align the audio-thread metronome with the playhead.
    //
    // The next click fires at the next upcoming integer beat. If the playhead
    // is exactly on an integer beat (playheadBeats == 2.0), the next click
    // fires immediately on the very next audio sample. Otherwise we wait for
    // the fractional remainder before clicking.
    //
    // The beat-in-measure counter is set so that the upcoming click carries
    // the correct accent. For playheadBeats = 0.0 the next click is the
    // downbeat (beat 0 of measure); for playheadBeats = 2.7 the next click is
    // beat 3 of the measure.
    const double bpm = metroBPM.load();
    const int    num = metroTimeSigNum.load();
    const int    den = metroTimeSigDen.load();

    const double quarterSamples = (60.0 / bpm) * currentSampleRate;
    const double samplesPerBeat = quarterSamples * (4.0 / den);

    // Fractional distance past the last integer beat, in the range [0, 1).
    double frac = playheadBeats - std::floor(playheadBeats);

    if (frac < 1.0e-6)
    {
        // Exactly on a beat — click on the very next sample
        metroSampleCounter = samplesPerBeat;
    }
    else
    {
        // We've already covered `frac` beats of the current interval,
        // so seed the counter with that much progress already banked.
        metroSampleCounter = samplesPerBeat * frac;
    }

    // Work out which beat-in-measure the NEXT integer beat will be.
    // If playheadBeats = 0.0    → next integer beat = 0     → beat-in-measure 0
    // If playheadBeats = 2.7    → next integer beat = 3     → beat-in-measure 3 % num
    // If playheadBeats = 3.0    → next integer beat = 3     → beat-in-measure 3 % num
    int nextIntegerBeat = (frac < 1.0e-6)
        ? (int)std::llround(playheadBeats)
        : (int)std::floor(playheadBeats) + 1;

    // metroBeatInMeasure is incremented BEFORE each click is emitted in the
    // audio loop, so we seed it at (desired - 1) mod num.
    int desired = ((nextIntegerBeat % num) + num) % num;   // safe positive mod
    metroBeatInMeasure = (desired - 1 + num) % num;

    clickPlayPos = -1;                     // no click currently in flight
    metronomeTickFlag.store(false);        // clear any stale UI flag
}

// ============================================================================
//  getNextAudioBlock
// ============================================================================

void VocalSynthEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();

    if (shouldStop)
    {
        juce::ScopedLock sl(queueLock);
        activeVoices.clear();
        pendingVoices.clear();
        shouldStop = false;
        clickPlayPos = -1;
        metroSampleCounter = 0.0;
        metroBeatInMeasure = 0;
        return;
    }

    // Pause gate: emit silence (buffer was already cleared) and do NOT advance
    // voice rendering or the metronome counter. Voices keep their current
    // readPos/samplesPlayed so a subsequent unpause resumes exactly where we
    // left off. Pending voices queued during pause will still get absorbed
    // into activeVoices on the first unpaused block.
    if (audioPaused.load())
        return;

    {
        juce::ScopedLock sl(queueLock);
        if (!pendingVoices.empty())
        {
            for (auto& v : activeVoices)
            {
                if (v.active && !v.releasing)
                {
                    v.releasing = true;
                    v.releasePos = 0;
                    v.releaseLen = kReleaseSamples;
                }
            }
            for (auto& v : pendingVoices)
                activeVoices.push_back(std::move(v));
            pendingVoices.clear();
        }
    }

    const int numSamples = info.numSamples;
    float* outL = info.buffer->getWritePointer(0, info.startSample);
    float* outR = info.buffer->getNumChannels() > 1
        ? info.buffer->getWritePointer(1, info.startSample) : outL;

    if (!activeVoices.empty())
    {
        activeVoices.erase(
            std::remove_if(activeVoices.begin(), activeVoices.end(),
                [&](Voice& v) { return !renderVoice(v, outL, outR, numSamples); }),
            activeVoices.end());
    }

    if (metronomeEnabled.load())
        renderMetronome(outL, outR, numSamples);

    // ── Master gain (final stage) ───────────────────────────────────────────
    // Applied once per block after voices + metronome are fully mixed, so it
    // affects everything the engine emits. Skip the multiply when at unity to
    // avoid wasted cycles on the audio thread.
    const float g = masterGain.load();
    if (g != 1.0f)
    {
        juce::FloatVectorOperations::multiply(outL, g, numSamples);
        if (outR != outL)
            juce::FloatVectorOperations::multiply(outR, g, numSamples);
    }
}

// ============================================================================
//  renderMetronome
// ============================================================================

void VocalSynthEngine::renderMetronome(float* outL, float* outR, int numSamples)
{
    const double bpm = metroBPM.load();
    const int    num = metroTimeSigNum.load();
    const int    den = metroTimeSigDen.load();

    double quarterSamples = (60.0 / bpm) * currentSampleRate;
    double samplesPerBeat = quarterSamples * (4.0 / den);

    for (int i = 0; i < numSamples; ++i)
    {
        if (metroSampleCounter >= samplesPerBeat)
        {
            metroSampleCounter -= samplesPerBeat;
            metroBeatInMeasure = (metroBeatInMeasure + 1) % num;
            clickIsAccent = (metroBeatInMeasure == 0);
            clickPlayPos = 0;
            metroLastBeat.store(metroBeatInMeasure);
            metronomeTickFlag.store(true);
        }

        if (clickPlayPos >= 0)
        {
            auto& buf = clickIsAccent ? clickHi : clickLo;
            if (clickPlayPos < buf.getNumSamples())
            {
                float s = buf.getSample(0, clickPlayPos);
                outL[i] += s;
                outR[i] += s;
                ++clickPlayPos;
            }
            else
                clickPlayPos = -1;
        }

        metroSampleCounter += 1.0;
    }
}

// ============================================================================
//  Phoneme classification & duration weighting
// ============================================================================

VocalSynthEngine::PhonemeType VocalSynthEngine::classifyPhoneme(const std::string& ph)
{
    static const char* vowels[] = {
        "AA","AE","AH","AO","AW","AY","EH","ER","EY","IH","IY","OW","OY","UH","UW"
    };
    for (auto* v : vowels)
        if (ph == v) return PhonemeType::Vowel;

    static const char* sustained[] = {
        "M","N","NG","L","R","W","Y","F","V","S","Z","SH","ZH","TH","DH","HH"
    };
    for (auto* s : sustained)
        if (ph == s) return PhonemeType::SustainedConsonant;

    return PhonemeType::StopConsonant;
}

float VocalSynthEngine::phonemeDurationWeight(PhonemeType type)
{
    // Vowels naturally take more time in speech, stops are very brief
    switch (type)
    {
    case PhonemeType::Vowel:              return 2.5f;
    case PhonemeType::SustainedConsonant: return 1.5f;
    case PhonemeType::StopConsonant:      return 0.5f;
    }
    return 1.0f;
}

int VocalSynthEngine::phonemeMinSamples(PhonemeType type, double sampleRate)
{
    // Minimum duration so nothing sounds unnaturally clipped
    switch (type)
    {
    case PhonemeType::Vowel:              return (int)(0.080 * sampleRate);  // 80 ms
    case PhonemeType::SustainedConsonant: return (int)(0.050 * sampleRate);  // 50 ms
    case PhonemeType::StopConsonant:      return (int)(0.025 * sampleRate);  // 25 ms
    }
    return (int)(0.040 * sampleRate);
}

// ============================================================================
//  readSample — with loop or one-shot mode
// ============================================================================

float VocalSynthEngine::readSample(const juce::AudioBuffer<float>* buf, int& readPos,
    bool loop,
    const std::vector<float>* xfade, int xfadeFade)
{
    if (buf == nullptr) return 0.0f;
    const int len = buf->getNumSamples();
    if (len == 0) return 0.0f;

    if (loop)
    {
        // ── Seamless loop (sustain slots) ──────────────────────────────────
        // Uses a precomputed xfade buffer that blends buf[len-fade..len-1]
        // into buf[0..fade-1]. Loop structure:
        //   First pass  : buf[0 .. len-fade-1]  (plays attack once)
        //                 xfade[0 .. fade-1]    (transition)
        //   Each cycle  : buf[fade .. len-fade-1] (steady-state, no attack repeat)
        //                 xfade[0 .. fade-1]    (transition)
        // Every boundary is between adjacent samples → no click.
        if (xfade != nullptr && xfadeFade > 0 && (int)xfade->size() >= xfadeFade
            && len > 2 * xfadeFade)
        {
            const int fade = xfadeFade;
            const int loopBodyLen = len - 2 * fade;   // pure steady-state per cycle
            const int firstPass = len - fade;        // buf samples before first xfade
            const int cycleLen = loopBodyLen + fade; // = len - fade per loop cycle

            float sample;
            if (readPos < firstPass)
            {
                sample = buf->getSample(0, readPos);
            }
            else if (readPos < len)
            {
                // First xfade region
                sample = (*xfade)[readPos - firstPass];
            }
            else
            {
                // Subsequent loop cycles
                int pos = (readPos - len) % cycleLen;
                if (pos < loopBodyLen)
                    sample = buf->getSample(0, fade + pos);
                else
                    sample = (*xfade)[pos - loopBodyLen];
            }

            ++readPos;
            return sample;
        }

        // ── Legacy loop crossfade (non-sustain phonemes, short slots) ──────
        // Imperfect but adequate for the brief consonant loops (< 1 beat).
        const int loopFade = std::min(256, len / 4);
        int idx = readPos % len;
        float sample = buf->getSample(0, idx);

        int distToEnd = len - idx;
        if (distToEnd < loopFade && loopFade > 0)
        {
            float t = (float)distToEnd / (float)loopFade;
            int   wrapIdx = loopFade - distToEnd;
            float startSample = buf->getSample(0, wrapIdx % len);
            sample = sample * t + startSample * (1.0f - t);
        }

        ++readPos;
        return sample;
    }
    else
    {
        // One-shot mode: play to end then fade to zero; no looping
        if (readPos >= len)
            return 0.0f;

        float sample = buf->getSample(0, readPos);

        // Gentle fade-out over last 512 samples
        const int fadeLen = std::min(512, len / 4);
        int distToEnd = len - readPos;
        if (distToEnd < fadeLen && fadeLen > 0)
            sample *= (float)distToEnd / (float)fadeLen;

        ++readPos;
        return sample;
    }
}

// ============================================================================
//  renderVoice — crossfade, envelope, vibrato, smoothing
// ============================================================================

bool VocalSynthEngine::renderVoice(Voice& v, float* outL, float* outR, int numSamples)
{
    if (!v.active || v.currentPhoneme >= (int)v.phonemes.size())
        return false;

    const float pi2 = 2.0f * juce::MathConstants<float>::pi;

    for (int i = 0; i < numSamples; ++i)
    {
        // ── Advance to next phoneme if current slot exhausted ───────────────
        while (v.currentPhoneme < (int)v.phonemes.size())
        {
            auto& cur = v.phonemes[v.currentPhoneme];
            if (cur.samplesPlayed < cur.slotLength) break;

            // Hand off to crossfade
            v.prevBuffer = cur.buffer;
            v.prevReadPos = cur.readPos;
            v.crossfadePos = 0;
            v.crossfadeLen = kCrossfadeSamples;
            ++v.currentPhoneme;
        }

        if (v.currentPhoneme >= (int)v.phonemes.size())
            return false;

        auto& slot = v.phonemes[v.currentPhoneme];

        // ── Vibrato: modulate read speed for vowels ─────────────────────────
        // Vibrato works by occasionally double-reading or skipping a sample,
        // effectively micro-shifting the pitch up/down
        bool extraRead = false;
        bool skipRead = false;
        if (slot.type == PhonemeType::Vowel && v.vibratoDepth > 0.0f)
        {
            v.vibratoPhase += v.vibratoRate / (float)currentSampleRate;
            if (v.vibratoPhase >= 1.0f) v.vibratoPhase -= 1.0f;

            // Vibrato produces a fractional speed offset
            // depth of 0.15 semitones ≈ ±0.87% speed change
            float speedMod = v.vibratoDepth * std::sin(pi2 * v.vibratoPhase);
            // Convert semitone offset to a probability of extra/skipped sample
            float speedRatio = std::pow(2.0f, speedMod / 12.0f) - 1.0f;

            // Accumulate fractional offset — when it exceeds 1, do extra read
            v.vibratoAccum += speedRatio;
            if (v.vibratoAccum >= 1.0f) { extraRead = true;  v.vibratoAccum -= 1.0f; }
            if (v.vibratoAccum <= -1.0f) { skipRead = true;  v.vibratoAccum += 1.0f; }
        }

        // ── Read current phoneme sample ─────────────────────────────────────
        bool loopThis = !slot.isLast;  // last phoneme: one-shot; others: loop
        const std::vector<float>* xfadeBuf = slot.loopXfade.empty() ? nullptr : &slot.loopXfade;
        float sampleIn = readSample(slot.buffer, slot.readPos, loopThis, xfadeBuf, slot.loopFadeSamples);

        // Apply vibrato speed modulation
        if (extraRead) readSample(slot.buffer, slot.readPos, loopThis, xfadeBuf, slot.loopFadeSamples);  // advance extra
        if (skipRead && slot.readPos > 0) --slot.readPos;                // step back

        // ── Per-phoneme amplitude envelope ──────────────────────────────────
        float envGain = 1.0f;
        if (slot.samplesPlayed < slot.attackLen && slot.attackLen > 0)
        {
            // Soft attack (sine quarter-wave ramp 0→1)
            float t = (float)slot.samplesPlayed / (float)slot.attackLen;
            envGain = std::sin(t * juce::MathConstants<float>::halfPi);
        }
        else if (slot.samplesPlayed >= slot.releaseStart && slot.releaseLen > 0)
        {
            // Soft release (cosine quarter-wave 1→0)
            float t = (float)(slot.samplesPlayed - slot.releaseStart) / (float)slot.releaseLen;
            t = std::min(t, 1.0f);
            envGain = std::cos(t * juce::MathConstants<float>::halfPi);
        }
        sampleIn *= envGain;

        // ── Equal-power crossfade with outgoing phoneme ─────────────────────
        float voiceSample = 0.0f;
        if (v.prevBuffer != nullptr && v.crossfadePos < v.crossfadeLen)
        {
            float sampleOut = readSample(v.prevBuffer, v.prevReadPos, true);
            float t = (float)v.crossfadePos / (float)v.crossfadeLen;
            float fadeOut = std::cos(t * juce::MathConstants<float>::halfPi);
            float fadeIn = std::sin(t * juce::MathConstants<float>::halfPi);
            voiceSample = sampleOut * fadeOut + sampleIn * fadeIn;
            ++v.crossfadePos;
        }
        else
        {
            voiceSample = sampleIn;
        }

        // ── One-pole smoothing filter (reduces concatenation harshness) ─────
        v.filterState += v.filterCoeff * (voiceSample - v.filterState);
        voiceSample = v.filterState;

        // ── Release envelope (when superseded by a new voice) ───────────────
        if (v.releasing)
        {
            if (v.releasePos >= v.releaseLen)
                return false;
            float relT = (float)v.releasePos / (float)v.releaseLen;
            voiceSample *= std::cos(relT * juce::MathConstants<float>::halfPi);
            ++v.releasePos;
        }

        outL[i] += voiceSample;
        outR[i] += voiceSample;
        ++slot.samplesPlayed;
    }

    return true;
}

// ============================================================================
//  stop
// ============================================================================

void VocalSynthEngine::stop() { shouldStop = true; }

void VocalSynthEngine::setPaused(bool paused)
{
    audioPaused.store(paused);
}

// ============================================================================
//  queueLyric — BPM drives speech rate
// ============================================================================

void VocalSynthEngine::queueLyric(const juce::String& lyric, int gridPitch,
    double bpm, double durationBeats)
{
    juce::StringArray words;
    words.addTokens(lyric.trim(), " ", "");

    std::vector<std::string> allPhonemes;
    for (const auto& word : words)
    {
        auto ph = lookupWord(word);
        allPhonemes.insert(allPhonemes.end(), ph.begin(), ph.end());
    }
    if (allPhonemes.empty()) return;

    const int  midiPitch = gridPitchToMidi(gridPitch);
    const auto noteFolder = nearestNoteFolder(midiPitch, availableNoteFolders);

    // One beat's worth of time, derived from BPM
    const double secondsPerBeat = 60.0 / juce::jlimit(30.0, 400.0, bpm);

    // Clamp durationBeats defensively (DB stores it as int >= 1, but be safe)
    const double safeDuration = juce::jlimit(0.25, 64.0, durationBeats);

    // buildVoice now takes the target MIDI directly so it can pitch-shift
    // each phoneme buffer to match the requested note when the source folder
    // doesn't already line up exactly.
    Voice v = buildVoice(allPhonemes, noteFolder, midiPitch, secondsPerBeat, safeDuration);
    v.active = true;

    // Randomise vibrato parameters for humanisation
    std::uniform_real_distribution<float> rateDist(4.5f, 6.5f);   // Hz (natural vibrato range)
    std::uniform_real_distribution<float> depthDist(0.08f, 0.20f); // semitones (subtle)
    v.vibratoRate = rateDist(rng);
    v.vibratoDepth = depthDist(rng);
    v.vibratoPhase = 0.0f;

    // One-pole smoothing: cutoff ~8 kHz to soften harsh transitions
    // coeff = 1 - e^(-2π * fc / fs), closer to 1 = less filtering
    double cutoffHz = 8000.0;
    v.filterCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi
        * (float)(cutoffHz / currentSampleRate));
    v.filterState = 0.0f;

    juce::ScopedLock sl(queueLock);
    pendingVoices.push_back(std::move(v));
}

// ============================================================================
//  pitchShiftBuffer — Lagrange-interpolated resample
// ============================================================================
//
//  Used by buildVoice to fill in piano-roll rows that have no dedicated voice
//  bank folder. Given the source phoneme buffer recorded at some folder MIDI
//  and a target pitch N semitones away, this produces a fresh buffer that
//  plays the same phoneme at the target pitch.
//
//  speedRatio (input samples consumed per output sample) = 2^(shift / 12).
//    shift = +12  → ratio 2.0   → output is half the length, pitch up an octave
//    shift = -12  → ratio 0.5   → output is double length, pitch down an octave
//  Output length = round(srcLen / speedRatio) so the entire input is consumed.
//
//  Called from queueLyric → buildVoice on the message thread, NOT the audio
//  thread. Allocation is fine here. The resulting shared_ptr is stored in
//  PhonemeSlot::ownedBuffer to keep the buffer alive for the lifetime of the
//  voice.

std::shared_ptr<juce::AudioBuffer<float>> VocalSynthEngine::pitchShiftBuffer(
    const juce::AudioBuffer<float>& src, double semitoneShift)
{
    const int srcLen = src.getNumSamples();
    const int srcCh = std::max(1, src.getNumChannels());

    if (srcLen <= 0)
        return std::make_shared<juce::AudioBuffer<float>>(srcCh, 0);

    const double speedRatio = std::pow(2.0, semitoneShift / 12.0);
    const int    dstLen = std::max(1, (int)std::llround((double)srcLen / speedRatio));

    auto dst = std::make_shared<juce::AudioBuffer<float>>(srcCh, dstLen);
    dst->clear();

    juce::LagrangeInterpolator interp;
    for (int ch = 0; ch < srcCh; ++ch)
    {
        interp.reset();
        interp.process(speedRatio,
            src.getReadPointer(ch),
            dst->getWritePointer(ch),
            dstLen);
    }
    return dst;
}

// ============================================================================
//  buildVoice — diphone-first lookup with single-phoneme fallback
// ============================================================================

VocalSynthEngine::BufferLookup VocalSynthEngine::findBuffer(
    const std::string& key, const std::string& preferredFolder) const
{
    BufferLookup hit;

    // Try preferred folder first — this is whichever folder is closest in
    // semitones to the requested target pitch.
    {
        auto it = voiceBank.find(key + "_" + preferredFolder);
        if (it != voiceBank.end())
        {
            hit.buffer = &it->second;
            hit.srcFolderMidi = noteNameToMidi(preferredFolder);
            return hit;
        }
    }

    // Fallback: scan every other available pitch folder. We may not always
    // have the exact phoneme/diphone in the preferred folder (e.g. only A3
    // recorded "HH-AH"), so we try all loaded folders before giving up.
    for (const auto& nf : availableNoteFolders)
    {
        if (nf.second == preferredFolder) continue;     // already tried above
        auto fbIt = voiceBank.find(key + "_" + nf.second);
        if (fbIt != voiceBank.end())
        {
            hit.buffer = &fbIt->second;
            hit.srcFolderMidi = nf.first;
            return hit;
        }
    }

    return hit;   // {nullptr, -1}
}

VocalSynthEngine::Voice VocalSynthEngine::buildVoice(
    const std::vector<std::string>& phonemes,
    const std::string& noteFolder,
    int    targetMidi,
    double secondsPerBeat,
    double durationBeats)
{
    if (phonemes.empty()) return Voice{};

    // Build the sequence of slots using diphone-first lookup:
    // For [HH, EH, L, OW]:
    //   slot 0: try "xx-HH" (onset), fallback "HH" (solo)
    //   slot 1: try "HH-EH" (diphone), fallback "EH" (solo)
    //   slot 2: try "EH-L" (diphone), fallback "L" (solo)
    //   slot 3: try "L-OW" (diphone), fallback "OW" (solo)
    //   slot 4: try "OW-xx" (release), optional

    struct SlotPlan {
        const juce::AudioBuffer<float>* buffer = nullptr;
        int   sourceFolderMidi = -1;     // MIDI of the folder this buffer was loaded from
        PhonemeType type = PhonemeType::Vowel;
        float weight = 1.0f;
        bool isDiphone = false;
        bool isLast = false;
    };
    std::vector<SlotPlan> plans;

    int diphoneHits = 0;
    int soloHits = 0;

    for (size_t i = 0; i < phonemes.size(); ++i)
    {
        const auto& cur = phonemes[i];
        std::string prev = (i == 0) ? "xx" : phonemes[i - 1];

        SlotPlan plan;
        plan.type = classifyPhoneme(cur);
        plan.weight = phonemeDurationWeight(plan.type);

        // Try diphone: PREV-CUR
        std::string diphoneKey = prev + "-" + cur;
        auto bl = findBuffer(diphoneKey, noteFolder);
        if (bl.buffer)
        {
            plan.buffer = bl.buffer;
            plan.sourceFolderMidi = bl.srcFolderMidi;
            plan.isDiphone = true;
            ++diphoneHits;
        }
        else
        {
            // Fallback: solo phoneme CUR
            bl = findBuffer(cur, noteFolder);
            if (bl.buffer)
            {
                plan.buffer = bl.buffer;
                plan.sourceFolderMidi = bl.srcFolderMidi;
                ++soloHits;
            }
        }

        if (plan.buffer)
            plans.push_back(plan);
    }

    // Track whether we actually appended a release diphone (LAST-xx).
    // It matters for picking the sustain slot: we never stretch the release
    // diphone itself — we stretch the vowel/consonant right before it.
    const size_t phonemeSlotCount = plans.size();

    if (!phonemes.empty())
    {
        std::string releaseKey = phonemes.back() + "-xx";
        auto bl = findBuffer(releaseKey, noteFolder);
        if (bl.buffer)
        {
            SlotPlan rel;
            rel.buffer = bl.buffer;
            rel.sourceFolderMidi = bl.srcFolderMidi;
            rel.type = classifyPhoneme(phonemes.back());
            rel.weight = 0.5f;  // releases are short
            rel.isDiphone = true;
            plans.push_back(rel);
        }
    }

    if (plans.empty()) return Voice{};

    const bool hasReleaseDiphone = plans.size() > phonemeSlotCount;

    DBG("buildVoice: " + juce::String((int)phonemes.size()) + " phonemes -> "
        + juce::String(diphoneHits) + " diphones, "
        + juce::String(soloHits) + " solos, "
        + juce::String(durationBeats, 2) + " beats, target MIDI "
        + juce::String(targetMidi));

    // ---- Sustain target (singer-style note hold) -----------------------------
    // For durationBeats > 1 we stretch ONE slot to fill the extra time:
    //   1st preference: the LAST vowel (before any release diphone)
    //   2nd preference: the LAST sustained consonant
    //   3rd preference: the last non-release slot
    const int searchEnd = hasReleaseDiphone ? (int)plans.size() - 1 : (int)plans.size();

    int sustainIdx = -1;
    for (int i = searchEnd - 1; i >= 0; --i)
        if (plans[i].type == PhonemeType::Vowel) { sustainIdx = i; break; }

    if (sustainIdx < 0)
        for (int i = searchEnd - 1; i >= 0; --i)
            if (plans[i].type == PhonemeType::SustainedConsonant) { sustainIdx = i; break; }

    if (sustainIdx < 0)
        sustainIdx = searchEnd - 1;  // last resort: whatever the tail phoneme is

    // ---- Sample-budget math --------------------------------------------------
    // Normal speech for the phonemes fits in 1 beat. Extra time beyond 1 beat
    // goes entirely into the sustain slot so consonants keep their natural
    // speech rate.
    float totalWeight = 0.0f;
    for (auto& p : plans) totalWeight += p.weight;

    const int oneBeatSamples = (int)(secondsPerBeat * currentSampleRate);
    const double extraBeats = std::max(0.0, durationBeats - 1.0);
    const int extraSustainSamples = (int)(extraBeats * secondsPerBeat * currentSampleRate);

    std::uniform_real_distribution<float> jitterDist(0.92f, 1.08f);

    Voice v;
    for (size_t pi = 0; pi < plans.size(); ++pi)
    {
        auto& p = plans[pi];

        // ── Pitch shift ─────────────────────────────────────────────────────
        // If the buffer's source folder MIDI doesn't equal targetMidi, resample
        // to bridge the gap. This is the entire reason this engine can fill in
        // notes outside the recorded pitch folders (C5/F#4/A3 in Aaron) — every
        // other piano-roll row gets here with shift != 0 and we synthesise the
        // right pitch via Lagrange resampling. The shared_ptr keeps the new
        // buffer alive for the lifetime of the slot.
        std::shared_ptr<juce::AudioBuffer<float>> ownedShifted;
        if (p.buffer != nullptr && p.sourceFolderMidi >= 0)
        {
            const int shift = targetMidi - p.sourceFolderMidi;
            if (shift != 0 && p.buffer->getNumSamples() > 0)
            {
                ownedShifted = pitchShiftBuffer(*p.buffer, (double)shift);
                if (ownedShifted && ownedShifted->getNumSamples() > 0)
                    p.buffer = ownedShifted.get();   // rest of buildVoice uses the shifted copy
                else
                    ownedShifted.reset();             // fall back to original on failure
            }
        }

        int slotLen = (int)((p.weight / totalWeight) * oneBeatSamples);
        slotLen = (int)(slotLen * jitterDist(rng));

        int minLen = phonemeMinSamples(p.type, currentSampleRate);
        slotLen = std::max(slotLen, minLen);

        const bool isLast = (pi == plans.size() - 1);
        const bool isSustain = ((int)pi == sustainIdx) && extraSustainSamples > 0;

        PhonemeSlot slot;
        slot.buffer = p.buffer;
        slot.ownedBuffer = ownedShifted;   // null if no shift; non-null keeps the resampled buf alive
        slot.type = p.type;

        if (isSustain)
        {
            // Singer-style hold: loop this slot for the extended duration.
            // Force loop mode by clearing isLast — renderVoice keys off that.
            slot.slotLength = slotLen + extraSustainSamples;
            slot.isLast = false;

            // Precompute seamless loop crossfade buffer so the sustain slot
            // loops without audible clicks at the wrap boundary.
            // xfade[i] = lerp(buf[len-fade+i], buf[i], i/fade)
            // This blends the tail of the buffer into the head; the resulting
            // loop structure plays the attack once, then cycles through the
            // steady-state region + xfade with no per-cycle amplitude jump.
            //
            // NOTE: this builds against p.buffer, which may already be the
            // pitch-shifted copy — so the xfade indices line up correctly with
            // whatever buffer renderVoice will actually be reading.
            if (p.buffer != nullptr)
            {
                const int blen = p.buffer->getNumSamples();
                const int fade = std::min(256, blen / 4);
                if (fade > 0 && blen > 2 * fade)
                {
                    slot.loopFadeSamples = fade;
                    slot.loopXfade.resize(fade);
                    const float fadeF = static_cast<float>(fade);
                    for (int xi = 0; xi < fade; ++xi)
                    {
                        float t = static_cast<float>(xi) / fadeF;
                        float endS = p.buffer->getSample(0, blen - fade + xi);
                        float startS = p.buffer->getSample(0, xi);
                        slot.loopXfade[xi] = endS * (1.0f - t) + startS * t;
                    }
                }
            }
        }
        else if (isLast && p.type != PhonemeType::StopConsonant)
        {
            // Original behavior: tail slot plays its full buffer once (one-shot)
            slot.slotLength = p.buffer->getNumSamples();
            slot.isLast = true;
        }
        else
        {
            slot.slotLength = slotLen;
            slot.isLast = isLast;
        }

        // Envelope: diphones need less attack since transitions are baked in
        if (p.isDiphone)
        {
            slot.attackLen = (int)(0.002 * currentSampleRate);  // 2ms
            slot.releaseLen = (int)(0.008 * currentSampleRate);  // 8ms
        }
        else
        {
            switch (p.type)
            {
            case PhonemeType::Vowel:
                slot.attackLen = (int)(0.005 * currentSampleRate);
                slot.releaseLen = (int)(0.015 * currentSampleRate);
                break;
            case PhonemeType::SustainedConsonant:
                slot.attackLen = (int)(0.003 * currentSampleRate);
                slot.releaseLen = (int)(0.010 * currentSampleRate);
                break;
            case PhonemeType::StopConsonant:
                slot.attackLen = 0;
                slot.releaseLen = (int)(0.005 * currentSampleRate);
                break;
            }
        }

        // If this sustain slot is the actual tail of the voice (no release
        // diphone follows), give it a longer, musical fade-out so the held
        // note doesn't cut abruptly when slotLength is reached.
        const bool isTailOfVoice = isSustain && (pi == plans.size() - 1);
        if (isTailOfVoice)
            slot.releaseLen = (int)(0.080 * currentSampleRate);   // 80 ms

        slot.releaseLen = std::min(slot.releaseLen, slot.slotLength / 3);
        slot.attackLen = std::min(slot.attackLen, slot.slotLength / 3);
        slot.releaseStart = slot.slotLength - slot.releaseLen;

        if (slot.isLast)
        {
            // One-shot slot: no envelope release — readSample does its own fade.
            slot.releaseLen = 0;
            slot.releaseStart = slot.slotLength;
        }

        v.phonemes.push_back(std::move(slot));
    }

    return v;
}

// ============================================================================
//  lookupPhonemes — public wrapper for the educational inspector
// ============================================================================

juce::StringArray VocalSynthEngine::lookupPhonemes(const juce::String& word) const
{
    juce::StringArray out;
    auto phonemes = lookupWord(word);
    for (const auto& p : phonemes)
        out.add(juce::String(p));
    return out;
}

// ============================================================================
//  loadDictionary
// ============================================================================

bool VocalSynthEngine::loadDictionary(const juce::File& cmuDictFile)
{
    dictionary.clear();
    if (!cmuDictFile.existsAsFile()) return false;

    juce::StringArray lines;
    cmuDictFile.readLines(lines);

    int loaded = 0;
    for (const auto& line : lines)
    {
        if (line.startsWith(";;;") || line.trim().isEmpty()) continue;
        int sp = line.indexOfChar(' ');
        if (sp < 0) continue;
        juce::String word = line.substring(0, sp).trim();
        if (word.contains("(")) continue;

        juce::StringArray tokens;
        tokens.addTokens(line.substring(sp + 1).trim(), " ", "");
        std::vector<std::string> ph;
        for (const auto& t : tokens) ph.push_back(stripStress(t.toStdString()));
        dictionary[word.toLowerCase().toStdString()] = std::move(ph);
        ++loaded;
    }
    DBG("VocalSynthEngine: loaded " + juce::String(loaded) + " dictionary entries");
    return loaded > 0;
}

// ============================================================================
//  loadVoiceBank
// ============================================================================

bool VocalSynthEngine::loadVoiceBank(const juce::File& voiceBankFolder)
{
    voiceBank.clear();
    availableNoteFolders.clear();
    if (!voiceBankFolder.isDirectory()) return false;

    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();

    int loaded = 0;
    for (juce::DirectoryEntry noteEntry : juce::RangedDirectoryIterator(voiceBankFolder, false, "*", juce::File::findDirectories))
    {
        std::string folderName = noteEntry.getFile().getFileName().toStdString();
        int midi = noteNameToMidi(folderName);
        if (midi >= 0)
            availableNoteFolders.push_back({ midi, folderName });

        for (juce::DirectoryEntry wavEntry : juce::RangedDirectoryIterator(noteEntry.getFile(), false, "*.wav", juce::File::findFiles))
        {
            juce::File f = wavEntry.getFile();
            auto reader = std::unique_ptr<juce::AudioFormatReader>(fmt.createReaderFor(f));
            if (!reader) continue;
            juce::AudioBuffer<float> buf(1, (int)reader->lengthInSamples);
            reader->read(&buf, 0, (int)reader->lengthInSamples, 0, true, false);
            voiceBank[f.getFileNameWithoutExtension().toStdString()] = std::move(buf);
            ++loaded;
        }
    }

    // Sort by MIDI pitch for nearest-search
    std::sort(availableNoteFolders.begin(), availableNoteFolders.end());

    if (availableNoteFolders.empty())
    {
        // Fallback to legacy hardcoded folders
        availableNoteFolders = { {57,"A3"}, {60,"C4"}, {65,"F4"} };
    }

    DBG("VocalSynthEngine: loaded " + juce::String(loaded) + " samples from "
        + juce::String((int)availableNoteFolders.size()) + " pitch folders");
    return loaded > 0;
}

// ============================================================================
//  reloadVoiceBank  --  thread-safe hot-swap for character select
// ============================================================================
//
//  Thread-safety story:
//  - The audio thread dereferences PhonemeSlot::buffer pointers that live
//    inside the voiceBank map. If we clear voiceBank while the audio thread
//    is reading, we UB.
//  - setPaused(true) makes getNextAudioBlock return early (no reads). We then
//    sleep briefly to guarantee any in-flight audio block has finished.
//  - Under queueLock we wipe activeVoices + pendingVoices so no stale Voice
//    references will survive into the new bank.
//  - loadVoiceBank() itself clears voiceBank and repopulates.
//  - setPaused(false) resumes the audio thread with a fresh voiceBank.
//
//  Pitch-shifted slots own their resampled buffer via shared_ptr, so even if
//  we somehow missed clearing them they would not point into the cleared
//  voiceBank map — but we clear them anyway for cleanliness.

bool VocalSynthEngine::reloadVoiceBank(const juce::File& voiceBankFolder)
{
    const bool wasPaused = audioPaused.load();
    audioPaused.store(true);

    // Give the audio thread up to ~50ms to exit any in-flight block. Typical
    // audio block at 44.1kHz with 512 samples is ~12ms, so this is plenty.
    juce::Thread::sleep(50);

    {
        juce::ScopedLock sl(queueLock);
        activeVoices.clear();
        pendingVoices.clear();
    }

    const bool ok = loadVoiceBank(voiceBankFolder);

    // Restore prior pause state (almost always false at call time, but
    // if the user had paused playback before swapping we honour that).
    audioPaused.store(wasPaused);
    return ok;
}

// ============================================================================
//  Helpers
// ============================================================================

std::vector<std::string> VocalSynthEngine::lookupWord(const juce::String& word) const
{
    juce::String clean;
    for (juce::juce_wchar c : word)
        if (juce::CharacterFunctions::isLetter(c)) clean += c;

    auto it = dictionary.find(clean.toLowerCase().toStdString());
    if (it != dictionary.end()) return it->second;

    std::vector<std::string> result;
    for (juce::juce_wchar c : clean.toLowerCase())
    {
        auto lit = dictionary.find(std::string(1, (char)c));
        if (lit != dictionary.end())
            for (const auto& p : lit->second) result.push_back(p);
    }
    return result;
}

std::string VocalSynthEngine::stripStress(const std::string& phoneme) const
{
    std::string out;
    for (char c : phoneme)
        if (!std::isdigit((unsigned char)c)) out += c;
    return out;
}

int VocalSynthEngine::gridPitchToMidi(int gridPitch)
{
    // Maps piano-roll row index (0 = A6, 14 = C2) to MIDI pitch.
    // Matches the kRowMidi table in PianoRollComponent exactly.
    static constexpr int kRowMidi[15] = {
        93, 90, 84, 81, 78, 72, 69, 66, 60, 57, 54, 48, 45, 42, 36
    };
    if (gridPitch < 0 || gridPitch >= 15)
        return 60;   // safe fallback: C4
    return kRowMidi[gridPitch];
}

std::string VocalSynthEngine::nearestNoteFolder(int midiPitch,
    const std::vector<std::pair<int, std::string>>& available)
{
    if (available.empty()) return "C4";
    const auto* best = &available[0];
    int bestD = std::abs(midiPitch - available[0].first);
    for (const auto& nf : available)
    {
        int d = std::abs(midiPitch - nf.first);
        if (d < bestD) { bestD = d; best = &nf; }
    }
    return best->second;
}

int VocalSynthEngine::noteNameToMidi(const std::string& name)
{
    // Parse names like "C4", "F#4", "A3", "Bb3"
    if (name.size() < 2) return -1;
    static const int noteBase[] = { 9,11,0,2,4,5,7 }; // A,B,C,D,E,F,G
    char letter = (char)std::toupper((unsigned char)name[0]);
    if (letter < 'A' || letter > 'G') return -1;
    int base = noteBase[letter - 'A'];
    size_t i = 1;
    if (i < name.size() && name[i] == '#') { base += 1; ++i; }
    else if (i < name.size() && name[i] == 'b') { base -= 1; ++i; }
    if (i >= name.size() || !std::isdigit((unsigned char)name[i])) return -1;
    int octave = name[i] - '0';
    return (octave + 1) * 12 + base;  // MIDI: C4 = 60
}