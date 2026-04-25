#pragma once
#include <JuceHeader.h>
#include <random>

// ---------------------------------------------------------------------------
//  VocalSynthEngine
//
//  Concatenative vocal synthesiser + integrated metronome.
//
//  Audio quality features:
//  • BPM-proportional phoneme timing — speech rate tracks tempo
//  • Duration weighting — vowels get more time, stops less, fricatives fair
//  • Equal-power cosine crossfade between phonemes
//  • Per-phoneme amplitude envelope (attack / sustain / release)
//  • Vibrato on vowel phonemes for humanisation
//  • Slight timing jitter (±8%) per phoneme for natural feel
//  • Last phoneme plays full WAV once (no loop), fades at end
//  • One-pole smoothing filter to reduce concatenation harshness
//  • Release envelope when a voice is superseded
//  • Time-signature-aware metronome on the audio thread
// ---------------------------------------------------------------------------

class VocalSynthEngine : public juce::AudioSource
{
public:
    VocalSynthEngine();
    ~VocalSynthEngine() override = default;

    // ── Resource loading ────────────────────────────────────────────────────
    bool loadDictionary(const juce::File& cmuDictFile);
    bool loadVoiceBank(const juce::File& voiceBankFolder);

    // Thread-safe hot-swap: pauses the audio thread, clears active + pending
    // voices under queueLock, then reloads from the new folder and unpauses.
    // Safe to call from any non-audio thread while audio is running.
    // Used by the DAW character-select flow to switch between Aaron / UTAU / ...
    bool reloadVoiceBank(const juce::File& voiceBankFolder);

    // ── Dictionary lookup (for educational / inspection features) ───────────
    // Returns the ARPAbet phonemes for a word, stress digits stripped.
    // Falls back to per-character lookup if the word isn't in the dictionary.
    // Empty array if not resolvable.
    juce::StringArray lookupPhonemes(const juce::String& word) const;

    // ── Playback control ────────────────────────────────────────────────────
    // durationBeats: how long the note should hold, in beats (default 1.0).
    // For durationBeats > 1.0, consonants keep their normal speech rate and
    // the final vowel is stretched to fill the remainder (singer-style hold).
    void queueLyric(const juce::String& lyric, int gridPitch,
        double bpm = 120.0, double durationBeats = 1.0);
    void stop();

    // Pause: freezes all active voices in place and silences output. Unlike
    // stop(), pause preserves voice state (readPos, samplesPlayed) so that
    // resuming via setPaused(false) continues exactly where it left off.
    // Also freezes metronome counter so no tick backlog builds up.
    void setPaused(bool paused);
    bool isPaused() const { return audioPaused.load(); }

    // ── Metronome control (call from message thread) ────────────────────────
    void setMetronomeEnabled(bool enabled);
    void setTempo(double bpm);
    void setTimeSignature(int numerator, int denominator);

    // Prime the metronome so its next click lines up with the playhead.
    // playheadBeats: current playhead position in beats (0.0 = start).
    // If the playhead is already on or past an integer beat (e.g. 2.0, or 2.7),
    // the next click fires at the next upcoming integer beat, with the correct
    // beat-in-measure index (so a user who paused at beat 2.7 and resumes will
    // hear beat 3 next, not beat 0).
    void resetMetronome(double playheadBeats = 0.0);

    // ── AudioSource interface ────────────────────────────────────────────────
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

    // Read-only accessors for UI (beat flash etc.)
    bool didMetronomeTick() const { return metronomeTickFlag.load(); }
    void clearMetronomeTick() { metronomeTickFlag.store(false); }
    int  getLastMetronomeBeat() const { return metroLastBeat.load(); }

    // ── Master gain (thread-safe; applied after voices + metronome are mixed)
    //   0.0 = silent, 1.0 = unity (default), 1.5 = +3.5 dB ceiling. Clamped.
    //   Safe to call from any thread — atomic read in the audio callback.
    void  setMasterGain(float linearGain) noexcept
    {
        masterGain.store(juce::jlimit(0.0f, 1.5f, linearGain));
    }
    float getMasterGain() const noexcept { return masterGain.load(); }

private:
    // ── Dictionary ──────────────────────────────────────────────────────────
    std::unordered_map<std::string, std::vector<std::string>> dictionary;
    std::vector<std::string> lookupWord(const juce::String& word) const;
    std::string              stripStress(const std::string& phoneme) const;

    // ── Voice bank ──────────────────────────────────────────────────────────
    std::unordered_map<std::string, juce::AudioBuffer<float>> voiceBank;
    static std::string nearestNoteFolder(int midiPitch,
        const std::vector<std::pair<int, std::string>>& available);
    static int         gridPitchToMidi(int gridPitch);
    static int         noteNameToMidi(const std::string& name);

    // Discovered pitch folders (populated by loadVoiceBank)
    std::vector<std::pair<int, std::string>> availableNoteFolders;  // midi, name

    // ── Phoneme classification ──────────────────────────────────────────────
    enum class PhonemeType { Vowel, SustainedConsonant, StopConsonant };
    static PhonemeType classifyPhoneme(const std::string& phoneme);
    static float       phonemeDurationWeight(PhonemeType type);
    static int         phonemeMinSamples(PhonemeType type, double sampleRate);

    // ── Voice struct ────────────────────────────────────────────────────────
    struct PhonemeSlot
    {
        const juce::AudioBuffer<float>* buffer = nullptr;
        int   readPos = 0;
        int   slotLength = 0;
        int   samplesPlayed = 0;
        bool  isLast = false;
        PhonemeType type = PhonemeType::Vowel;

        // Per-phoneme amplitude envelope (in samples)
        int   attackLen = 0;
        int   releaseStart = 0;    // = slotLength - releaseLen
        int   releaseLen = 0;
    };

    struct Voice
    {
        std::vector<PhonemeSlot> phonemes;
        int   currentPhoneme = 0;
        bool  active = false;
        bool  releasing = false;
        int   releasePos = 0;
        int   releaseLen = 0;

        // Crossfade state
        const juce::AudioBuffer<float>* prevBuffer = nullptr;
        int   prevReadPos = 0;
        int   crossfadePos = 0;
        int   crossfadeLen = 0;

        // Vibrato state (per-voice, applied to vowels)
        float vibratoPhase = 0.0f;
        float vibratoRate = 0.0f;   // Hz, randomised per voice
        float vibratoDepth = 0.0f;   // semitones, randomised per voice
        float vibratoAccum = 0.0f;   // fractional sample accumulator

        // One-pole smoothing filter state
        float filterState = 0.0f;
        float filterCoeff = 0.0f;   // 0..1, set from sample rate
    };

    // ── Thread-safe voice queue ──────────────────────────────────────────────
    juce::CriticalSection          queueLock;
    std::vector<Voice>             pendingVoices;
    std::vector<Voice>             activeVoices;

    // ── Audio state ─────────────────────────────────────────────────────────
    double currentSampleRate = 44100.0;
    bool   shouldStop = false;
    std::atomic<bool> audioPaused{ false };

    static constexpr int kCrossfadeSamples = 1024;
    static constexpr int kReleaseSamples = 2048;

    // Master gain — applied as a final multiply after voices + metronome are
    // mixed into outL/outR in getNextAudioBlock. 1.0 = unity.
    std::atomic<float> masterGain{ 1.0f };

    // ── Humanisation RNG ────────────────────────────────────────────────────
    std::mt19937 rng{ std::random_device{}() };

    // ── Metronome (audio thread) ────────────────────────────────────────────
    std::atomic<bool>   metronomeEnabled{ false };
    std::atomic<double> metroBPM{ 120.0 };
    std::atomic<int>    metroTimeSigNum{ 4 };
    std::atomic<int>    metroTimeSigDen{ 4 };

    double metroSampleCounter = 0.0;
    int    metroBeatInMeasure = 0;

    std::atomic<bool> metronomeTickFlag{ false };
    std::atomic<int>  metroLastBeat{ 0 };

    juce::AudioBuffer<float> clickHi;
    juce::AudioBuffer<float> clickLo;
    int  clickPlayPos = -1;
    bool clickIsAccent = false;

    void generateClickBuffers();
    void renderMetronome(float* outL, float* outR, int numSamples);

    // ── Helpers ─────────────────────────────────────────────────────────────
    Voice buildVoice(const std::vector<std::string>& phonemes,
        const std::string& noteFolder,
        double secondsPerBeat,
        double durationBeats);

    // Lookup a buffer by key (e.g. "AH" or "AY-ER") trying noteFolder then all pitches
    const juce::AudioBuffer<float>* findBuffer(const std::string& key,
        const std::string& noteFolder) const;

    bool renderVoice(Voice& v, float* outL, float* outR, int numSamples);

    // Read sample: loop=true wraps with crossfade, loop=false fades to zero at end
    static float readSample(const juce::AudioBuffer<float>* buf, int& readPos, bool loop);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalSynthEngine)
};