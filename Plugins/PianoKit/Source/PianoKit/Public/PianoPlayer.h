// Game-thread front end of a practice session: owns the score, the MetaSound graph
// (PianoKit Sequencer -> PianoKit Sfizz), MIDI keyboard input and the per-note feedback state.
#pragma once

#include "CoreMinimal.h"
#include "PianoDrills.h"
#include "PianoSession.h"
#include "Tasks/Pipe.h"
#include "Tasks/Task.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include "PianoPlayer.generated.h"

class FPianoScore;
class UAudioComponent;
class UMIDIDeviceInputController;

enum class EPianoNoteState : uint8
{
	Pending,
	Hit,
	Missed,
};

// What a keyboard view should show for one key right now.
struct FPianoKeyState
{
	uint8 UpcomingHand = 0;    // 1 right / 2 left: this key is (part of) the next thing to play
	bool bHeld = false;        // the player holds it
	int8 Verdict = -1;         // for a held key: 1 right, 0 wrong, -1 not judged
	float WrongFlash = 0.f;    // 1 just after a wrong press, fading to 0
	bool bAccompaniment = false; // the app is sounding this key (other hand / listen mode)
};

// One time through the loop: the practised hands' notes in it, played / missed, and wrong keys.
struct FPianoPass
{
	int32 Hits = 0;
	int32 Missed = 0;
	int32 Wrong = 0;
	float Tempo = 1.f;
	bool IsClean() const { return Hits > 0 && Missed == 0 && Wrong == 0; }
};

struct FPianoStats
{
	int32 Hits = 0;
	int32 Wrong = 0;
	int32 Missed = 0;
	int32 Waits = 0;
	double SumAbsErrorMs = 0.0;
	double MeanAbsErrorMs() const { return Hits > 0 ? SumAbsErrorMs / Hits : 0.0; }
	// accent drills: how much louder the accented notes were played (MIDI velocity)
	int32 AccentHits = 0, PlainHits = 0;
	double AccentVelocity = 0.0, PlainVelocity = 0.0;
	double AccentContrast() const { return AccentHits > 0 && PlainHits > 0 ? AccentVelocity / AccentHits - PlainVelocity / PlainHits : 0.0; }
};

UCLASS()
class PIANOKIT_API UPianoPlayer : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// Instrument played by sfizz: the Salamander Grand Piano SFZ in the plugin folder (tools/fetch_salamander.py puts
	// it there). Without it a plain built-in tone plays, so the app still makes sound.
	static constexpr const TCHAR* InstrumentFile = TEXT("ThirdParty/Salamander/Salamander Grand Piano V3.sfz");
	static constexpr int32 InstrumentVoices = 1024; // sfizz voice limit (-voices=N overrides)

	// The instrument loads in the background (~2.5 s); the graph is silent until then.
	enum class ESoundState : uint8 { Loading, Ready, Fallback, Failed };
	ESoundState GetSoundState() const;
	// Attribution for the piano samples in use (empty until they are loaded, and for the built-in tone).
	FString GetSoundCredit() const;

	bool Initialize(UWorld* World);
	void Shutdown();
	virtual void BeginDestroy() override;

	// Loads now (blocking; cached scores are instant).
	bool LoadScore(const FString& Path, FString& OutError);
	// Loads in the background and switches to it when ready (cached scores switch at once); OnDone on the game thread.
	void LoadScoreAsync(const FString& Path, TFunction<void(bool bOk, const FString& Error)> OnDone = nullptr);
	bool IsLoadingScore() const { return PendingLoads > 0; }
	TSharedPtr<FPianoScore> GetScore() const { return Score; }
	// Changes whenever another score is shown.
	int32 GetScoreSerial() const { return ScoreSerial; }

	// What the sequencer plays: the score's notes re-timed by the current drill, sorted by time (SourceIndex = score note).
	const TArray<FPianoSessionNote>& GetPlayNotes() const { return PlayNotes; }
	void SetDrill(EPianoDrill InDrill);
	EPianoDrill GetDrill() const { return Drill; }

	// Bars of clicks before the music starts after Play (0 = none): one click per beat of the measure played first.
	void SetCountInBars(int32 Bars);
	int32 GetCountInBars() const { return CountInBars; }
	int32 GetCountInLeft() const;
	// Play-along judges presses this much earlier (keyboard + audio latency).
	void SetInputLatencyMs(float Ms);
	float GetInputLatencyMs() const;

	// Transport
	void Play();
	// Counts in again (the count-in bars at the tempo of SongMs) and plays on from where the clock is; plays if paused.
	void CountInAgain(double SongMs);
	void Pause();
	void TogglePlay() { IsPlaying() ? Pause() : Play(); }
	bool IsPlaying() const;
	void Seek(double SongMs);
	void Restart();
	// Song position for display: advances smoothly every frame and is steered towards the audio clock.
	double GetSongMs() const;
	// The audio clock as published by the sequencer (extrapolated between audio blocks; can step).
	double GetRawSongMs() const;
	bool IsWaiting() const;
	bool IsFinished() const { return bFinished; }
	// True once the audio clock has made no progress for 1.5 s while playing (audio render stalled).
	bool IsAudioStalled() const { return bAudioStalled; }

	// Practice settings
	void SetWaitMode(bool bEnabled);
	bool GetWaitMode() const;
	void SetHands(EPianoHand Hands);
	EPianoHand GetHands() const;
	void SetTempoScale(float Scale);
	float GetTempoScale() const;
	void SetListenMode(bool bEnabled); // the app plays every hand (demo)
	bool GetListenMode() const;
	void SetEchoLiveKeys(bool bEnabled); // sound the player's own keys (off if the piano makes its own sound)
	bool GetEchoLiveKeys() const;
	void SetLoop(double StartMs, double EndMs);
	void ClearLoop();
	bool GetLoop(double& OutStartMs, double& OutEndMs) const;

	// Live input (MIDI keyboards are bound automatically; the UI calls these for the computer keyboard)
	void NoteOn(int32 Pitch, int32 Velocity = 100);
	void NoteOff(int32 Pitch);
	// Sustain pedal (MIDI CC64 from keyboards is bound automatically).
	void SetSustainPedal(bool bDown);
	bool IsPedalDown() const { return bPedal; }
	bool IsKeyHeld(int32 Pitch) const { return Pitch >= 0 && Pitch < 128 && Held[Pitch]; }
	// Verdict on the latest press of this key: 1 right, 0 wrong, -1 not judged (yet, or listen mode).
	int32 GetPressResult(int32 Pitch) const { return PressResult.IsValidIndex(Pitch) ? PressResult[Pitch] : -1; }
	const TArray<FString>& GetMidiDeviceNames() const { return MidiDeviceNames; }

	// Feedback for the UI
	EPianoNoteState GetNoteState(int32 NoteIndex) const;
	const FPianoStats& GetStats() const { return Stats; }
	// The latest completed loop pass, and a counter that changes with every pass.
	const FPianoPass& GetLastPass() const { return LastPass; }
	int32 GetPassSerial() const { return PassSerial; }
	// Broadcast as each pass is judged, before anything else this frame (autoplay included) acts on the new pass.
	FSimpleMulticastDelegate OnPassJudged;
	// Score notes of the practised hands that start at the next onset at/after the cursor (the keys to play next).
	void GetUpcomingNotes(TArray<int32>& OutNoteIndices) const;
	// Wrong key flashes: pitch -> wall-clock seconds of the last wrong press.
	float GetWrongFlash(int32 Pitch) const;
	// Per-key display state for all 128 MIDI pitches.
	void GetKeyStates(TArray<FPianoKeyState>& OutStates) const;

	// Automation: plays the practised hands perfectly (used by -autotest runs).
	// ErrorEvery > 0: every Nth practised note is played a semitone too high instead (tests wrong / missed scoring).
	// InErrorsUntil > 0: mistakes only among the first that many notes played
	void SetAutoplay(bool bEnabled, int32 InErrorEvery = 0, int32 InErrorsUntil = 0) { bAutoplay = bEnabled; AutoplayErrorEvery = InErrorEvery; AutoplayErrorsUntil = InErrorsUntil; }
	// With autoplay: change the sustain pedal at every barline (up just after the downbeat, down again 80 ms later).
	void SetAutoPedal(bool bEnabled) { bAutoPedal = bEnabled; }

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return Session.IsValid(); }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }

private:
	bool BuildGraph(UWorld* World);
	void LoadInstrument(UWorld* World);
	void ApplyScore(const TSharedPtr<FPianoScore>& NewScore);
	void CacheScore(const FString& Path, const TSharedPtr<FPianoScore>& NewScore);
	void RebuildPlayNotes();
	double CountInBeatMsAt(double SongMs) const;
	int32 BeatsInMeasureAt(double SongMs) const;
	void ScanMidiDevices();
	void ResetFeedback();
	void OpenLog();
	void LogLine(const TCHAR* Event, int32 NoteIndex, int32 Pitch, double SongMs, float ErrorMs);

	UFUNCTION()
	void HandleMidiNoteOn(UMIDIDeviceInputController* Controller, int32 Timestamp, int32 Channel, int32 Note, int32 Velocity);
	UFUNCTION()
	void HandleMidiNoteOff(UMIDIDeviceInputController* Controller, int32 Timestamp, int32 Channel, int32 Note, int32 Velocity);
	UFUNCTION()
	void HandleMidiControlChange(UMIDIDeviceInputController* Controller, int32 Timestamp, int32 Channel, int32 Type, int32 Value);

	UPROPERTY()
	TObjectPtr<UAudioComponent> Audio;

	UPROPERTY()
	TObjectPtr<UObject> GraphSource;

	UPROPERTY()
	TArray<TObjectPtr<UMIDIDeviceInputController>> MidiInputs;

	TSharedPtr<FPianoSession, ESPMode::ThreadSafe> Session;
	int32 SessionId = 0;
	TSharedPtr<FPianoScore> Score;
	TArray<FPianoSessionNote> PlayNotes;
	EPianoDrill Drill = EPianoDrill::AsWritten;
	int32 CountInBars = 0;
	int32 ScoreSerial = 0;
	int32 PendingLoads = 0;
	TArray<TPair<FString, TSharedPtr<FPianoScore>>> ScoreCache; // most recent last
	UE::Tasks::FPipe LoadPipe{ TEXT("PianoScoreLoads") };

	TArray<EPianoNoteState> NoteStates;
	FPianoStats Stats;
	FPianoPass LastPass;
	int32 PassSerial = 0;
	int32 PassWrongStart = 0;
	TBitArray<> Held;
	TArray<int8> PressResult;
	TArray<double> WrongFlashSeconds;
	TArray<FString> MidiDeviceNames;
	struct FSoundLoad
	{
		std::atomic<uint8> State{uint8(ESoundState::Loading)};
	};
	TSharedPtr<FSoundLoad, ESPMode::ThreadSafe> SoundLoad;
	UE::Tasks::FTask LoadTask;
	double NextMidiScanSeconds = 0.0;
	bool bFinished = false;

	void UpdateSmoothClock(float DeltaTime);
	void CheckAudioStall();
	double StallLastMs = -1.0;
	double StallLastProgressSeconds = 0.0;
	bool bAudioStalled = false;
	double SmoothMs = 0.0;
	bool bSmoothValid = false;

	bool bPedal = false;
	bool bAutoplay = false;
	bool bAutoPedal = false;
	int32 AutoPedalMeasure = 0;
	int32 AutoplayErrorEvery = 0;
	int32 AutoplayErrorsUntil = 0;
	int32 AutoplayCount = 0;
	int32 AutoplayNext = 0;
	// after a seek: song ms it went to, and when; autoplay waits until the audio thread is there
	double AutoplaySeekMs = -1.0, AutoplaySeekSeconds = 0.0;
	double AutoplayCountInSeconds = 0.0; // Play() with a count-in, until the audio thread has started it
	TArray<TPair<double, int32>> AutoplayReleases; // song ms, pitch
	TArray<TPair<double, int32>> AutoplayFixes;    // wall seconds to press, pitch (corrections after a mistake)

	TUniquePtr<FArchive> Log;
};
