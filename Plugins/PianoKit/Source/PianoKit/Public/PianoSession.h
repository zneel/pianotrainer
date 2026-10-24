// State shared between the game thread (UI, MIDI input) and the audio-thread sequencer node.
// The game thread writes settings and live key presses; the sequencer writes the song position and feedback.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include <atomic>

enum class EPianoHand : uint8
{
	Right = 1,
	Left = 2,
	Both = 3,
};

struct FPianoSessionNote
{
	int32 Pitch = 60;
	double OnMs = 0.0;
	double OffMs = 0.0;
	EPianoHand Hand = EPianoHand::Right;
	uint8 Velocity = 80;
	bool bTied = false; // continuation of a tie: already sounding from the first note, never struck again
	bool bGrace = false; // grace note: wait mode doesn't hold for it and play-along doesn't count it as missed
	bool bAccent = false; // drills: play this one louder (the demo plays it louder too)
	int32 SourceIndex = INDEX_NONE; // index of the score note this plays (drills re-time and re-sort notes)
};

struct sfizz_synth_t;
class FPianoVst3Plugin;

// A loaded instrument - an SFZ in sfizz, or a VST 3 plug-in: loaded on the game thread (UPianoPlayer), played by the
// PianoKit Sfizz node. A plug-in is released on the game thread, whichever thread lets go of it last.
struct PIANOKIT_API FPianoSynth
{
	sfizz_synth_t* Synth = nullptr;
	TSharedPtr<FPianoVst3Plugin, ESPMode::ThreadSafe> Plugin;
	float SampleRate = 0.f;      // what it is configured for; the node reconfigures it if the graph runs at another rate
	int32 SamplesPerBlock = 0;
	~FPianoSynth();

	// Settings the game thread changes and the node applies before it renders (sfizz wants them on that thread).
	void SetQuality(int32 InQuality) { FScopeLock Guard(&SettingsLock); Quality = InQuality; ++SettingsVersion; }
	void SetVolumeDb(float InVolumeDb) { FScopeLock Guard(&SettingsLock); VolumeDb = InVolumeDb; ++SettingsVersion; }
	void SetControl(int32 Cc, float Value) { FScopeLock Guard(&SettingsLock); Controls.Add(Cc, Value); ++SettingsVersion; }
	std::atomic<uint32> SettingsVersion{1};
	FCriticalSection SettingsLock;
	int32 Quality = 2;
	float VolumeDb = 0.f;
	TMap<int32, float> Controls;
};

// Commands from the game thread, applied by the sequencer in the order they were sent
// (a seek followed by key presses must never be reordered).
struct FPianoLiveEvent
{
	bool bNoteOn = true;
	uint8 Pitch = 60;
	uint8 Velocity = 100;
	double SeekMs = -1.0; // >= 0: this is a seek, not a key
	int8 Pedal = -1;      // 0 / 1: sustain pedal up / down, not a key
};

enum class EPianoFeedback : uint8
{
	Hit,      // a score note was played (NoteIndex, ErrorMs = played time - written time)
	Wrong,    // a key that matched no expected note (Pitch)
	Missed,   // a score note passed without being played (NoteIndex)
	Wait,     // wait mode stopped the music at SongMs
	Resume,   // wait mode resumed
	Looped,   // playback jumped from the loop end back to the loop start
	Finished, // reached the end of the score
};

struct FPianoFeedbackEvent
{
	EPianoFeedback Type = EPianoFeedback::Hit;
	int32 NoteIndex = INDEX_NONE;
	int32 Pitch = 0;
	double SongMs = 0.0;
	float ErrorMs = 0.f;
	uint8 Velocity = 0; // Hit / Wrong: how hard the key was played
};

class PIANOKIT_API FPianoSession : public TSharedFromThis<FPianoSession, ESPMode::ThreadSafe>
{
public:
	using FNoteList = TArray<FPianoSessionNote>;
	using FNoteListPtr = TSharedPtr<const FNoteList, ESPMode::ThreadSafe>;

	// Registry so a MetaSound node can find its session from an int32 graph input.
	static int32 Register(const TSharedRef<FPianoSession, ESPMode::ThreadSafe>& Session);
	static void Unregister(int32 Id);
	static TSharedPtr<FPianoSession, ESPMode::ThreadSafe> Find(int32 Id);

	// Game thread: replace the score. Notes must be sorted by OnMs.
	void SetNotes(FNoteList&& Notes);
	// Audio thread: current notes; OutVersion changes whenever SetNotes is called.
	FNoteListPtr GetNotes(uint32& OutVersion) const;
	uint32 GetNotesVersion() const { return NotesVersion.load(std::memory_order_acquire); }

	// Game thread: the score's sustain pedal marks, (song ms, 1 down / 0 up) sorted by time; played when bScorePedal.
	using FPedalList = TArray<FVector2D>;
	using FPedalListPtr = TSharedPtr<const FPedalList, ESPMode::ThreadSafe>;
	void SetPedals(FPedalList&& InPedals);
	FPedalListPtr GetPedals(uint32& OutVersion) const;
	uint32 GetPedalsVersion() const { return PedalsVersion.load(std::memory_order_acquire); }
	// The score's beats for the metronome: X = song ms, Y = 1 on the first beat of a bar.
	void SetBeats(FPedalList&& InBeats);
	FPedalListPtr GetBeats(uint32& OutVersion) const;
	uint32 GetBeatsVersion() const { return BeatsVersion.load(std::memory_order_acquire); }

	// Game thread: the instrument the PianoKit Sfizz node plays. Audio thread: current one (version changes on update).
	using FSynthPtr = TSharedPtr<FPianoSynth, ESPMode::ThreadSafe>;
	void SetSynth(FSynthPtr InSynth);
	FSynthPtr GetSynth(uint32& OutVersion) const;
	uint32 GetSynthVersion() const { return SynthVersion.load(std::memory_order_acquire); }

	// Settings (game thread writes, audio thread reads every block).
	std::atomic<bool> bPlaying{false};
	std::atomic<bool> bWaitMode{true};
	std::atomic<bool> bEchoLive{true};      // sound the user's own keys through the instrument
	std::atomic<bool> bPlayUserHands{false}; // also play the hands the user is practising (listen mode)
	std::atomic<uint8> UserHands{uint8(EPianoHand::Both)};
	std::atomic<float> TempoScale{1.f};
	std::atomic<double> LoopStartMs{0.0};
	std::atomic<double> LoopEndMs{-1.0}; // < 0: no loop
	std::atomic<int32> CountInBeats{0};   // clicks before the music starts after Play
	std::atomic<double> CountInBeatMs{500.0}; // beat length at the written tempo
	std::atomic<int32> CountInLeft{0};    // published: clicks still to come (0 = playing)
	std::atomic<bool> bCountInRequest{false}; // count in again while playing (the clock waits for the clicks)
	std::atomic<float> InputLatencyMs{0.f}; // play-along judges presses this much earlier (keyboard + audio latency)
	std::atomic<bool> bMetronome{false};   // a light click on every beat while the music plays
	std::atomic<bool> bScorePedal{false};  // play the score's pedal marks with what the app plays

	// Game thread -> audio thread.
	void PushLive(const FPianoLiveEvent& Event) { LiveIn.Enqueue(Event); }
	void PushSeek(double SongMs) { LiveIn.Enqueue({ false, 0, 0, FMath::Max(0.0, SongMs) }); }
	void PushPedal(bool bDown) { LiveIn.Enqueue({ false, 0, 0, -1.0, int8(bDown ? 1 : 0) }); }
	bool PopLive(FPianoLiveEvent& Out) { return LiveIn.Dequeue(Out); }

	// Audio thread -> game thread.
	void PushFeedback(const FPianoFeedbackEvent& Event) { FeedbackOut.Enqueue(Event); }
	bool PopFeedback(FPianoFeedbackEvent& Out) { return FeedbackOut.Dequeue(Out); }

	// Song position published by the audio thread once per block, read by the UI with extrapolation.
	void PublishPosition(double SongMs, double Rate, bool bWaiting);
	// Interpolated song position for display at the current wall-clock time (and the clock's current rate).
	double GetDisplaySongMs(bool* OutWaiting = nullptr, double* OutRate = nullptr) const;

private:
	TQueue<FPianoLiveEvent, EQueueMode::Mpsc> LiveIn;
	TQueue<FPianoFeedbackEvent, EQueueMode::Spsc> FeedbackOut;

	mutable FCriticalSection NotesLock;
	FNoteListPtr Notes = MakeShared<const FNoteList, ESPMode::ThreadSafe>();
	std::atomic<uint32> NotesVersion{1};

	FPedalListPtr Pedals = MakeShared<const FPedalList, ESPMode::ThreadSafe>();
	std::atomic<uint32> PedalsVersion{1};
	FPedalListPtr Beats = MakeShared<const FPedalList, ESPMode::ThreadSafe>();
	std::atomic<uint32> BeatsVersion{1};

	FSynthPtr Synth;
	std::atomic<uint32> SynthVersion{0};

	// Seqlock-protected position snapshot.
	std::atomic<uint32> PosSeq{0};
	std::atomic<double> PosSongMs{0.0};
	std::atomic<double> PosRate{0.0};
	std::atomic<double> PosWallSeconds{0.0};
	std::atomic<bool> PosWaiting{false};
};
