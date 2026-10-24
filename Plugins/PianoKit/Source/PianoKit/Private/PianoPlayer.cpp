#include "PianoPlayer.h"

#include "PianoKitLog.h"
#include "PianoScore.h"
#include "PianoDrills.h"

#include "SfizzApi.h"

#include "Algo/BinarySearch.h"
#include "Async/Async.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"
#include "MIDIDeviceInputController.h"
#include "MIDIDeviceManager.h"
#include "MetasoundBuilderSubsystem.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundSource.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"

namespace PianoPlayerImpl
{
	static const FName SessionInputName(TEXT("Session"));

	// Played when the Salamander samples are missing: a decaying sine per key (sfizz's built-in oscillator).
	static const char* FallbackSfz = "<region> sample=*sine ampeg_attack=0.003 ampeg_decay=1.5 ampeg_sustain=0 ampeg_release=0.3 amp_veltrack=100 volume=-12";
	static const TCHAR* SalamanderCredit = TEXT("Piano: Salamander Grand Piano V3 by Alexander Holm (CC-BY 3.0), played by sfizz");

	static bool Check(EMetaSoundBuilderResult Result, const TCHAR* What)
	{
		if (Result != EMetaSoundBuilderResult::Succeeded)
		{
			UE_LOG(LogPianoKit, Error, TEXT("MetaSound graph build failed at: %s"), What);
			return false;
		}
		return true;
	}
}

bool UPianoPlayer::Initialize(UWorld* World)
{
	Session = MakeShared<FPianoSession, ESPMode::ThreadSafe>();
	SessionId = FPianoSession::Register(Session.ToSharedRef());
	Held.Init(false, 128);
	PressResult.Init(-1, 128);
	WrongFlashSeconds.Init(-1000.0, 128);

	if (!BuildGraph(World))
	{
		return false;
	}
	ScanMidiDevices();
	if (FParse::Param(FCommandLine::Get(), TEXT("pianolog")))
	{
		OpenLog();
	}
	return true;
}

bool UPianoPlayer::BuildGraph(UWorld* World)
{
	using namespace PianoPlayerImpl;

	LoadInstrument(World);

	UMetaSoundBuilderSubsystem& Builders = UMetaSoundBuilderSubsystem::GetChecked();
	EMetaSoundBuilderResult Result;
	FMetaSoundBuilderNodeOutputHandle OnPlay;
	FMetaSoundBuilderNodeInputHandle OnFinished;
	TArray<FMetaSoundBuilderNodeInputHandle> AudioOuts;
	UMetaSoundSourceBuilder* Builder = Builders.CreateSourceBuilder(TEXT("PianoKitGraphBuilder"), OnPlay, OnFinished, AudioOuts, Result,
		EMetaSoundOutputAudioFormat::Stereo, /*bIsOneShot*/ false);
	if (!Check(Result, TEXT("CreateSourceBuilder")) || AudioOuts.Num() < 2)
	{
		return false;
	}

	FMetasoundFrontendLiteral ZeroLiteral;
	ZeroLiteral.Set(int32(0));
	const FMetaSoundBuilderNodeOutputHandle SessionIn = Builder->AddGraphInputNode(SessionInputName, TEXT("Int32"), ZeroLiteral, Result);
	if (!Check(Result, TEXT("graph input Session")))
	{
		return false;
	}

	const FMetaSoundNodeHandle Sequencer = Builder->AddNodeByClassName({ TEXT("PianoKit"), TEXT("PianoSequencer"), TEXT("") }, Result, 1);
	if (!Check(Result, TEXT("PianoKit Sequencer node")))
	{
		return false;
	}
	const FMetaSoundNodeHandle Sampler = Builder->AddNodeByClassName({ TEXT("PianoKit"), TEXT("Sfizz"), TEXT("") }, Result, 1);
	if (!Check(Result, TEXT("PianoKit Sfizz node")))
	{
		return false;
	}

	Builder->ConnectNodes(SessionIn, Builder->FindNodeInputByName(Sequencer, TEXT("Session"), Result), Result);
	Check(Result, TEXT("Session -> Sequencer"));
	Builder->ConnectNodes(SessionIn, Builder->FindNodeInputByName(Sampler, TEXT("Session"), Result), Result);
	Check(Result, TEXT("Session -> Sampler"));
	Builder->ConnectNodes(Builder->FindNodeOutputByName(Sequencer, TEXT("MIDI Stream"), Result),
		Builder->FindNodeInputByName(Sampler, TEXT("MIDI Stream"), Result), Result);
	if (!Check(Result, TEXT("Sequencer -> Sampler MIDI")))
	{
		return false;
	}

	Builder->ConnectNodes(Builder->FindNodeOutputByName(Sampler, TEXT("Audio Left"), Result), AudioOuts[0], Result);
	Builder->ConnectNodes(Builder->FindNodeOutputByName(Sampler, TEXT("Audio Right"), Result), AudioOuts[1], Result);
	if (!Check(Result, TEXT("Sampler -> audio out")))
	{
		return false;
	}

	FMetaSoundBuilderOptions Options;
	Options.Name = TEXT("PianoKitSource");
	TScriptInterface<IMetaSoundDocumentInterface> Built = Builder->Build(GetTransientPackage(), Options);
	UMetaSoundSource* Source = Cast<UMetaSoundSource>(Built.GetObject());
	if (!Source)
	{
		UE_LOG(LogPianoKit, Error, TEXT("MetaSound builder produced no source"));
		return false;
	}
	GraphSource = Source;

	Audio = UGameplayStatics::CreateSound2D(World, Source, 1.f, 1.f, 0.f, nullptr, /*bPersistAcrossLevelTransition*/ true, /*bAutoDestroy*/ false);
	if (!Audio)
	{
		UE_LOG(LogPianoKit, Error, TEXT("Could not create the audio component (no audio device?)"));
		return false;
	}
	Audio->SetIntParameter(SessionInputName, SessionId);
	Audio->Play();
	UE_LOG(LogPianoKit, Log, TEXT("MetaSound graph running: session %d"), SessionId);
	return true;
}

void UPianoPlayer::LoadInstrument(UWorld* World)
{
	float SampleRate = 48000.f;
	if (World)
	{
		if (FAudioDeviceHandle Device = World->GetAudioDevice())
		{
			SampleRate = Device->GetSampleRate();
		}
	}
	int32 Voices = InstrumentVoices;
	FParse::Value(FCommandLine::Get(), TEXT("-voices="), Voices); // -voices=N: voice limit for testing
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PianoKit"));
	const FString Path = FPaths::ConvertRelativePathToFull((Plugin ? Plugin->GetBaseDir() : FPaths::ProjectPluginsDir() / TEXT("PianoKit")) / InstrumentFile);

	// sfizz parses the SFZ and preloads the start of every sample (the rest streams from disk while playing):
	// ~2.5 s for the Salamander, so it runs in the background and the sound starts when it is done.
	SoundLoad = MakeShared<FSoundLoad, ESPMode::ThreadSafe>();
	const TSharedRef<FSoundLoad, ESPMode::ThreadSafe> Load = SoundLoad.ToSharedRef();
	const TWeakPtr<FPianoSession, ESPMode::ThreadSafe> WeakSession = Session;
	LoadTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Load, WeakSession, Path, SampleRate, Voices]()
	{
		using namespace PianoPlayerImpl;
		const FSfizzApi& Api = FSfizzApi::Get();
		if (!Api.IsLoaded())
		{
			UE_LOG(LogPianoKit, Error, TEXT("sfizz is not loaded: no sound"));
			Load->State.store(uint8(ESoundState::Failed));
			return;
		}
		const double StartSeconds = FPlatformTime::Seconds();
		const TSharedRef<FPianoSynth, ESPMode::ThreadSafe> Synth = MakeShared<FPianoSynth, ESPMode::ThreadSafe>();
		Synth->Synth = Api.create_synth();
		Synth->SampleRate = SampleRate;
		Synth->SamplesPerBlock = 1024;
		Api.set_sample_rate(Synth->Synth, SampleRate);
		Api.set_samples_per_block(Synth->Synth, Synth->SamplesPerBlock);
		Api.set_num_voices(Synth->Synth, FMath::Clamp(Voices, 1, 1024));
		Api.set_volume(Synth->Synth, 0.f); // sfizz defaults to -7.35 dB of headroom; the Salamander peaks ~-6 dBFS at unity
		const bool bSalamander = FPaths::FileExists(Path) && Api.load_file(Synth->Synth, TCHAR_TO_UTF8(*Path)) && Api.get_num_regions(Synth->Synth) > 0;
		if (!bSalamander)
		{
			UE_LOG(LogPianoKit, Warning, TEXT("Piano samples not found or not loadable at %s (run tools/fetch_salamander.py); using a plain tone"), *Path);
			Api.load_string(Synth->Synth, "builtin.sfz", FallbackSfz);
		}
		UE_LOG(LogPianoKit, Log, TEXT("Instrument %s: %d regions, %d samples preloaded, %d voices, %.0f Hz, loaded in %.0f ms"),
			bSalamander ? *Path : TEXT("(built-in tone)"), Api.get_num_regions(Synth->Synth), int32(Api.get_num_preloaded_samples(Synth->Synth)),
			Api.get_num_voices(Synth->Synth), SampleRate, (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		if (const TSharedPtr<FPianoSession, ESPMode::ThreadSafe> Pinned = WeakSession.Pin())
		{
			Pinned->SetSynth(Synth);
		}
		Load->State.store(uint8(bSalamander ? ESoundState::Ready : ESoundState::Fallback));
	});
}

UPianoPlayer::ESoundState UPianoPlayer::GetSoundState() const
{
	return SoundLoad.IsValid() ? ESoundState(SoundLoad->State.load()) : ESoundState::Failed;
}

FString UPianoPlayer::GetSoundCredit() const
{
	return GetSoundState() == ESoundState::Ready ? FString(PianoPlayerImpl::SalamanderCredit) : FString();
}

void UPianoPlayer::Shutdown()
{
	for (UMIDIDeviceInputController* Input : MidiInputs)
	{
		if (Input)
		{
			Input->OnMIDINoteOn.RemoveAll(this);
			Input->OnMIDINoteOff.RemoveAll(this);
			Input->OnMIDIControlChange.RemoveAll(this);
		}
	}
	MidiInputs.Reset();
	if (Audio)
	{
		Audio->Stop();
		Audio = nullptr;
	}
	LoadPipe.WaitUntilEmpty(); // background score loads use Verovio
	if (LoadTask.IsValid())
	{
		LoadTask.Wait(); // the loader uses sfizz; let it finish before the module (and the DLL) can go away
	}
	if (SessionId != 0)
	{
		FPianoSession::Unregister(SessionId);
		SessionId = 0;
	}
	Session.Reset();
	if (Log)
	{
		Log->Close();
		Log.Reset();
	}
}

void UPianoPlayer::BeginDestroy()
{
	Shutdown();
	Super::BeginDestroy();
}

void UPianoPlayer::ScanMidiDevices()
{
	TArray<FMIDIDeviceInfo> Inputs, Outputs;
	UMIDIDeviceManager::FindAllMIDIDeviceInfo(Inputs, Outputs);

	TArray<FString> Names;
	for (const FMIDIDeviceInfo& Info : Inputs)
	{
		Names.Add(Info.DeviceName);
	}
	if (Names == MidiDeviceNames && MidiInputs.Num() == Inputs.Num())
	{
		return;
	}

	for (UMIDIDeviceInputController* Input : MidiInputs)
	{
		if (Input)
		{
			Input->OnMIDINoteOn.RemoveAll(this);
			Input->OnMIDINoteOff.RemoveAll(this);
			Input->OnMIDIControlChange.RemoveAll(this);
		}
	}
	MidiInputs.Reset();
	MidiDeviceNames.Reset();
	for (const FMIDIDeviceInfo& Info : Inputs)
	{
		if (UMIDIDeviceInputController* Input = UMIDIDeviceManager::CreateMIDIDeviceInputController(Info.DeviceID))
		{
			Input->OnMIDINoteOn.AddDynamic(this, &UPianoPlayer::HandleMidiNoteOn);
			Input->OnMIDINoteOff.AddDynamic(this, &UPianoPlayer::HandleMidiNoteOff);
			Input->OnMIDIControlChange.AddDynamic(this, &UPianoPlayer::HandleMidiControlChange);
			MidiInputs.Add(Input);
			MidiDeviceNames.Add(Info.DeviceName);
			UE_LOG(LogPianoKit, Log, TEXT("MIDI input: %s"), *Info.DeviceName);
		}
	}
}

void UPianoPlayer::HandleMidiNoteOn(UMIDIDeviceInputController*, int32, int32, int32 Note, int32 Velocity)
{
	if (Velocity == 0)
	{
		NoteOff(Note); // running-status keyboards send note-on with velocity 0 as note-off
	}
	else
	{
		NoteOn(Note, Velocity);
	}
}

void UPianoPlayer::HandleMidiNoteOff(UMIDIDeviceInputController*, int32, int32, int32 Note, int32)
{
	NoteOff(Note);
}

void UPianoPlayer::NoteOn(int32 Pitch, int32 Velocity)
{
	if (!Session.IsValid() || Pitch < 0 || Pitch > 127)
	{
		return;
	}
	Held[Pitch] = true;
	PressResult[Pitch] = -1;
	Session->PushLive({ true, uint8(Pitch), uint8(FMath::Clamp(Velocity, 1, 127)) });
}

void UPianoPlayer::HandleMidiControlChange(UMIDIDeviceInputController*, int32, int32, int32 Type, int32 Value)
{
	if (Type == 64) // sustain (damper) pedal
	{
		SetSustainPedal(Value >= 64);
	}
}

void UPianoPlayer::SetSustainPedal(bool bDown)
{
	if (!Session.IsValid() || bDown == bPedal)
	{
		return;
	}
	bPedal = bDown;
	Session->PushPedal(bDown);
	LogLine(bDown ? TEXT("pedal_down") : TEXT("pedal_up"), INDEX_NONE, 64, GetSongMs(), 0.f);
}

void UPianoPlayer::NoteOff(int32 Pitch)
{
	if (!Session.IsValid() || Pitch < 0 || Pitch > 127)
	{
		return;
	}
	Held[Pitch] = false;
	Session->PushLive({ false, uint8(Pitch), 0 });
}

bool UPianoPlayer::LoadScore(const FString& Path, FString& OutError)
{
	for (const TPair<FString, TSharedPtr<FPianoScore>>& Entry : ScoreCache)
	{
		if (Entry.Key == Path)
		{
			ApplyScore(Entry.Value);
			return true;
		}
	}
	TSharedPtr<FPianoScore> NewScore = MakeShared<FPianoScore>();
	if (!NewScore->LoadFile(Path, OutError))
	{
		UE_LOG(LogPianoKit, Warning, TEXT("Load failed: %s"), *OutError);
		return false;
	}
	CacheScore(Path, NewScore);
	ApplyScore(NewScore);
	return true;
}

void UPianoPlayer::LoadScoreAsync(const FString& Path, TFunction<void(bool bOk, const FString& Error)> OnDone)
{
	for (const TPair<FString, TSharedPtr<FPianoScore>>& Entry : ScoreCache)
	{
		if (Entry.Key == Path)
		{
			ApplyScore(Entry.Value);
			if (OnDone) { OnDone(true, FString()); }
			return;
		}
	}
	// Verovio + engraving take 0.2-2 s: done on one background pipe (one score at a time), switched on the game thread.
	++PendingLoads;
	const TWeakObjectPtr<UPianoPlayer> Weak(this);
	LoadPipe.Launch(UE_SOURCE_LOCATION, [Weak, Path, OnDone]()
	{
		TSharedPtr<FPianoScore> NewScore = MakeShared<FPianoScore>();
		FString Error;
		const bool bOk = NewScore->LoadFile(Path, Error);
		AsyncTask(ENamedThreads::GameThread, [Weak, Path, NewScore, bOk, Error, OnDone]()
		{
			UPianoPlayer* Self = Weak.Get();
			if (!Self)
			{
				return;
			}
			--Self->PendingLoads;
			if (bOk)
			{
				Self->CacheScore(Path, NewScore);
				Self->ApplyScore(NewScore);
			}
			else
			{
				UE_LOG(LogPianoKit, Warning, TEXT("Load failed: %s"), *Error);
			}
			if (OnDone) { OnDone(bOk, Error); }
		});
	});
}

void UPianoPlayer::CacheScore(const FString& Path, const TSharedPtr<FPianoScore>& NewScore)
{
	ScoreCache.RemoveAll([&Path](const TPair<FString, TSharedPtr<FPianoScore>>& Entry) { return Entry.Key == Path; });
	ScoreCache.Add({ Path, NewScore });
	if (ScoreCache.Num() > 8)
	{
		ScoreCache.RemoveAt(0);
	}
}

void UPianoPlayer::ApplyScore(const TSharedPtr<FPianoScore>& NewScore)
{
	Pause();
	Score = NewScore;
	++ScoreSerial;
	Drill = EPianoDrill::AsWritten;
	ClearLoop();
	RebuildPlayNotes();
	ResetFeedback();
	Session->PushSeek(0.0);
	LogLine(TEXT("load"), INDEX_NONE, Score->GetNotes().Num(), 0.0, 0.f);
	if (Log)
	{
		// the note list the sequencer plays, for offline analysis
		FString Csv = TEXT("index,on_ms,off_ms,pitch,hand,tied,grace\n");
		for (const FPianoSessionNote& N : PlayNotes)
		{
			Csv += FString::Printf(TEXT("%d,%.1f,%.1f,%d,%d,%d,%d\n"), N.SourceIndex, N.OnMs, N.OffMs, N.Pitch, int32(N.Hand), N.bTied ? 1 : 0, N.bGrace ? 1 : 0);
		}
		FFileHelper::SaveStringToFile(Csv, *(FPaths::ProjectSavedDir() / TEXT("PianoLogs/notes.csv")));
	}
}

void UPianoPlayer::RebuildPlayNotes()
{
	if (!Score.IsValid())
	{
		return;
	}
	PlayNotes = FPianoDrills::Apply(Drill, Score->MakeSessionNotes(), Score->GetBeatTimes(), GetHands());
	Session->SetNotes(TArray<FPianoSessionNote>(PlayNotes));
}

void UPianoPlayer::SetDrill(EPianoDrill InDrill)
{
	if (InDrill == Drill || !Score.IsValid())
	{
		return;
	}
	Drill = InDrill;
	const double Now = GetSongMs();
	RebuildPlayNotes();
	Seek(Now); // new notes restart the sequencer at 0; carry on from here
	LogLine(TEXT("drill"), INDEX_NONE, int32(Drill), Now, 0.f);
}

void UPianoPlayer::SetCountInBars(int32 Bars)
{
	CountInBars = FMath::Clamp(Bars, 0, 4);
}

int32 UPianoPlayer::BeatsInMeasureAt(double SongMs) const
{
	if (!Score.IsValid())
	{
		return 4;
	}
	const TArray<FPianoScoreMeasure>& Measures = Score->GetMeasures();
	const int32 Measure = Score->GetMeasureAt(SongMs);
	const double Quarters = Measures.IsValidIndex(Measure + 1) ? Measures[Measure + 1].QStamp - Measures[Measure].QStamp : 4.0;
	return FMath::Clamp(FMath::RoundToInt32(Quarters), 1, 12);
}

int32 UPianoPlayer::GetCountInLeft() const
{
	return Session ? Session->CountInLeft.load() : 0;
}

void UPianoPlayer::SetInputLatencyMs(float Ms)
{
	if (Session)
	{
		Session->InputLatencyMs.store(FMath::Clamp(Ms, 0.f, 300.f));
	}
}

float UPianoPlayer::GetInputLatencyMs() const
{
	return Session ? Session->InputLatencyMs.load() : 0.f;
}

double UPianoPlayer::CountInBeatMsAt(double SongMs) const
{
	if (!Score.IsValid())
	{
		return 500.0;
	}
	const TArray<FPianoScoreMeasure>& Measures = Score->GetMeasures();
	const int32 Measure = Score->GetMeasureAt(SongMs);
	if (!Measures.IsValidIndex(Measure))
	{
		return 500.0;
	}
	const double Quarters = Measures.IsValidIndex(Measure + 1) ? Measures[Measure + 1].QStamp - Measures[Measure].QStamp : 4.0;
	return (Score->GetMeasureEndMs(Measure) - Measures[Measure].OnMs) / FMath::Max(1.0, Quarters);
}

void UPianoPlayer::ResetFeedback()
{
	NoteStates.Init(EPianoNoteState::Pending, Score.IsValid() ? Score->GetNotes().Num() : 0);
	Stats = FPianoStats();
	PassWrongStart = 0;
	bFinished = false;
	AutoplayNext = 0;
	AutoplayCount = 0;
	AutoplayReleases.Reset();
	AutoplayFixes.Reset();
	AutoPedalMeasure = 0;
}

void UPianoPlayer::Play()
{
	if (Session.IsValid() && Score.IsValid())
	{
		if (bFinished)
		{
			Restart();
		}
		Session->CountInBeatMs.store(CountInBeatMsAt(GetSongMs()));
		Session->CountInBeats.store(CountInBars * BeatsInMeasureAt(GetSongMs()));
		// until the audio thread starts counting in, CountInLeft still reads 0
		AutoplayCountInSeconds = CountInBars > 0 ? FPlatformTime::Seconds() : 0.0;
		Session->bPlaying.store(true);
		LogLine(TEXT("play"), INDEX_NONE, 0, GetSongMs(), 0.f);
	}
}

void UPianoPlayer::CountInAgain(double SongMs)
{
	if (!Session.IsValid() || !Score.IsValid())
	{
		return;
	}
	if (!IsPlaying() || CountInBars <= 0)
	{
		Play();
		return;
	}
	Session->CountInBeatMs.store(CountInBeatMsAt(SongMs));
	Session->CountInBeats.store(CountInBars * BeatsInMeasureAt(SongMs));
	AutoplayCountInSeconds = FPlatformTime::Seconds();
	Session->bCountInRequest.store(true);
	LogLine(TEXT("countin"), INDEX_NONE, 0, SongMs, 0.f);
}

void UPianoPlayer::Pause()
{
	if (Session.IsValid() && Session->bPlaying.exchange(false))
	{
		LogLine(TEXT("pause"), INDEX_NONE, 0, GetSongMs(), 0.f);
	}
}

bool UPianoPlayer::IsPlaying() const
{
	return Session.IsValid() && Session->bPlaying.load();
}

void UPianoPlayer::Seek(double SongMs)
{
	if (!Session.IsValid())
	{
		return;
	}
	Session->PushSeek(SongMs);
	LogLine(TEXT("seek"), INDEX_NONE, 0, SongMs, 0.f);
	// notes from the seek point onwards can be played again
	for (const FPianoSessionNote& N : PlayNotes)
	{
		if (N.OnMs >= SongMs - 0.5 && NoteStates.IsValidIndex(N.SourceIndex))
		{
			NoteStates[N.SourceIndex] = EPianoNoteState::Pending;
		}
	}
	AutoplayNext = Algo::LowerBoundBy(PlayNotes, SongMs - 0.5, &FPianoSessionNote::OnMs);
	if (bAutoplay)
	{
		for (const TPair<double, int32>& Release : AutoplayReleases)
		{
			NoteOff(Release.Value);
		}
		AutoplayReleases.Reset();
		AutoplaySeekMs = SongMs;
		AutoplaySeekSeconds = FPlatformTime::Seconds();
	}
	PassWrongStart = Stats.Wrong; // a seek starts a new pass
	bFinished = false;
}

void UPianoPlayer::Restart()
{
	double LoopStart = 0.0, LoopEnd = 0.0;
	const bool bLoop = GetLoop(LoopStart, LoopEnd);
	ResetFeedback();
	Seek(bLoop ? LoopStart : 0.0);
}

double UPianoPlayer::GetSongMs() const
{
	return bSmoothValid ? SmoothMs : GetRawSongMs();
}

double UPianoPlayer::GetRawSongMs() const
{
	return Session.IsValid() ? Session->GetDisplaySongMs() : 0.0;
}

void UPianoPlayer::CheckAudioStall()
{
	// The song clock is advanced by the audio render; if it stops while playing (not waiting), audio has stalled.
	const double NowSeconds = FPlatformTime::Seconds();
	if (!IsPlaying() || IsWaiting() || GetCountInLeft() > 0) // the clock rightly stands still during a count-in
	{
		StallLastProgressSeconds = NowSeconds;
		StallLastMs = -1.0;
		return;
	}
	const double Raw = GetRawSongMs();
	if (StallLastMs < 0.0 || Raw > StallLastMs + 1.0 || Raw < StallLastMs - 1.0)
	{
		StallLastMs = Raw;
		StallLastProgressSeconds = NowSeconds;
		return;
	}
	if (!bAudioStalled && NowSeconds - StallLastProgressSeconds > 1.5)
	{
		bAudioStalled = true;
		UE_LOG(LogPianoKit, Error, TEXT("Audio clock stalled at %.0f ms: no progress for 1.5 s while playing"), Raw);
		LogLine(TEXT("audio_stall"), INDEX_NONE, 0, Raw, 0.f);
	}
}

void UPianoPlayer::UpdateSmoothClock(float DeltaTime)
{
	// The audio thread publishes its position once per rendered block, and blocks are rendered in bursts, so the
	// raw clock steps. While playing, the display clock instead advances by frame time x rate and is steered towards
	// the audio clock (about 8 % of the error per frame at 60 fps).
	bool bWaiting = false;
	double Rate = 0.0;
	const double Raw = Session->GetDisplaySongMs(&bWaiting, &Rate);
	const double Predicted = SmoothMs + double(DeltaTime) * 1000.0 * Rate;
	const double Error = Raw - Predicted;
	if (!bSmoothValid || Rate <= 0.0 || FMath::Abs(Error) > 150.0)
	{
		SmoothMs = Raw; // paused, waiting, scrubbing or a jump: follow the audio clock exactly
	}
	else
	{
		SmoothMs = Predicted + Error * FMath::Min(1.0, double(DeltaTime) * 5.0);
	}
	bSmoothValid = true;
}

bool UPianoPlayer::IsWaiting() const
{
	bool bWaiting = false;
	if (Session.IsValid())
	{
		Session->GetDisplaySongMs(&bWaiting);
	}
	return bWaiting;
}

void UPianoPlayer::SetWaitMode(bool bEnabled) { if (Session) Session->bWaitMode.store(bEnabled); }
bool UPianoPlayer::GetWaitMode() const { return Session && Session->bWaitMode.load(); }
void UPianoPlayer::SetHands(EPianoHand Hands)
{
	if (!Session || Hands == GetHands())
	{
		return;
	}
	Session->UserHands.store(uint8(Hands));
	if (Drill != EPianoDrill::AsWritten)
	{
		const double Now = GetSongMs();
		RebuildPlayNotes(); // drills re-time the practised hands only
		Seek(Now);
	}
}
EPianoHand UPianoPlayer::GetHands() const { return Session ? EPianoHand(Session->UserHands.load()) : EPianoHand::Both; }
void UPianoPlayer::SetTempoScale(float Scale) { if (Session) Session->TempoScale.store(FMath::Clamp(Scale, 0.1f, 2.f)); }
float UPianoPlayer::GetTempoScale() const { return Session ? Session->TempoScale.load() : 1.f; }
void UPianoPlayer::SetListenMode(bool bEnabled) { if (Session) Session->bPlayUserHands.store(bEnabled); }
bool UPianoPlayer::GetListenMode() const { return Session && Session->bPlayUserHands.load(); }
void UPianoPlayer::SetEchoLiveKeys(bool bEnabled) { if (Session) Session->bEchoLive.store(bEnabled); }
bool UPianoPlayer::GetEchoLiveKeys() const { return Session && Session->bEchoLive.load(); }

void UPianoPlayer::SetLoop(double StartMs, double EndMs)
{
	if (!Session)
	{
		return;
	}
	if (EndMs < StartMs)
	{
		Swap(StartMs, EndMs);
	}
	Session->LoopStartMs.store(StartMs);
	Session->LoopEndMs.store(EndMs);
}

void UPianoPlayer::ClearLoop()
{
	if (Session)
	{
		Session->LoopStartMs.store(0.0);
		Session->LoopEndMs.store(-1.0);
	}
}

bool UPianoPlayer::GetLoop(double& OutStartMs, double& OutEndMs) const
{
	if (!Session)
	{
		return false;
	}
	OutStartMs = Session->LoopStartMs.load();
	OutEndMs = Session->LoopEndMs.load();
	return OutEndMs > OutStartMs;
}

EPianoNoteState UPianoPlayer::GetNoteState(int32 NoteIndex) const
{
	return NoteStates.IsValidIndex(NoteIndex) ? NoteStates[NoteIndex] : EPianoNoteState::Pending;
}

void UPianoPlayer::GetUpcomingNotes(TArray<int32>& OutNoteIndices) const
{
	OutNoteIndices.Reset();
	if (!Score.IsValid())
	{
		return;
	}
	const uint8 Hands = uint8(GetHands());
	const double Now = GetSongMs();
	double GroupMs = -1.0;
	for (int32 Index = Algo::LowerBoundBy(PlayNotes, Now - 60.0, &FPianoSessionNote::OnMs); Index < PlayNotes.Num(); ++Index)
	{
		const FPianoSessionNote& Note = PlayNotes[Index];
		if ((uint8(Note.Hand) & Hands) == 0 || Note.bTied || !NoteStates.IsValidIndex(Note.SourceIndex) || NoteStates[Note.SourceIndex] != EPianoNoteState::Pending)
		{
			continue;
		}
		if (GroupMs < 0.0)
		{
			GroupMs = Note.OnMs;
		}
		if (Note.OnMs - GroupMs > 25.0)
		{
			break;
		}
		OutNoteIndices.Add(Note.SourceIndex);
	}
}

void UPianoPlayer::GetKeyStates(TArray<FPianoKeyState>& OutStates) const
{
	OutStates.SetNum(128);
	for (int32 Pitch = 0; Pitch < 128; ++Pitch)
	{
		FPianoKeyState& State = OutStates[Pitch];
		State = FPianoKeyState();
		State.bHeld = IsKeyHeld(Pitch);
		State.Verdict = int8(GetPressResult(Pitch));
		State.WrongFlash = FMath::Clamp(1.f - GetWrongFlash(Pitch) / 0.35f, 0.f, 1.f);
	}
	if (!Score.IsValid())
	{
		return;
	}
	const TArray<FPianoScoreNote>& Notes = Score->GetNotes();
	TArray<int32> Upcoming;
	GetUpcomingNotes(Upcoming);
	for (int32 Index : Upcoming)
	{
		OutStates[Notes[Index].Pitch & 127].UpcomingHand = uint8(Notes[Index].Hand);
	}
	// Keys the app is sounding: the other hand, or every hand in listen mode.
	const double Now = GetSongMs();
	const uint8 Hands = uint8(GetHands());
	const bool bListen = GetListenMode();
	const int32 First = Algo::LowerBoundBy(PlayNotes, Now - 20000.0, &FPianoSessionNote::OnMs);
	for (int32 Index = First; Index < PlayNotes.Num() && PlayNotes[Index].OnMs <= Now; ++Index)
	{
		const FPianoSessionNote& Note = PlayNotes[Index];
		if (Now < Note.OffMs && !Note.bTied && (bListen || (uint8(Note.Hand) & Hands) == 0))
		{
			OutStates[Note.Pitch & 127].bAccompaniment = true;
		}
	}
}

float UPianoPlayer::GetWrongFlash(int32 Pitch) const
{
	return WrongFlashSeconds.IsValidIndex(Pitch) ? float(FPlatformTime::Seconds() - WrongFlashSeconds[Pitch]) : 1000.f;
}

void UPianoPlayer::OpenLog()
{
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("PianoLogs");
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Path = Dir / FString::Printf(TEXT("session_%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	Log.Reset(IFileManager::Get().CreateFileWriter(*Path, FILEWRITE_AllowRead));
	if (Log)
	{
		const char* Header = "event,note,pitch,song_ms,error_ms,wall_s\n";
		Log->Serialize(const_cast<char*>(Header), FCStringAnsi::Strlen(Header));
		UE_LOG(LogPianoKit, Log, TEXT("Logging session events to %s"), *Path);
	}
}

void UPianoPlayer::Tick(float DeltaTime)
{
	if (!Session.IsValid())
	{
		return;
	}
	UpdateSmoothClock(DeltaTime);
	CheckAudioStall();

	FPianoFeedbackEvent Event;
	while (Session->PopFeedback(Event))
	{
		switch (Event.Type)
		{
		case EPianoFeedback::Hit:
			if (PressResult.IsValidIndex(Event.Pitch)) { PressResult[Event.Pitch] = 1; }
			if (NoteStates.IsValidIndex(Event.NoteIndex))
			{
				NoteStates[Event.NoteIndex] = EPianoNoteState::Hit;
			}
			++Stats.Hits;
			Stats.SumAbsErrorMs += FMath::Abs(Event.ErrorMs);
			if (Drill == EPianoDrill::AccentThree || Drill == EPianoDrill::AccentFour)
			{
				const FPianoSessionNote* Played = PlayNotes.FindByPredicate([&Event](const FPianoSessionNote& N) { return N.SourceIndex == Event.NoteIndex; });
				const bool bAccent = Played && Played->bAccent;
				(bAccent ? Stats.AccentHits : Stats.PlainHits) += 1;
				(bAccent ? Stats.AccentVelocity : Stats.PlainVelocity) += Event.Velocity;
			}
			break;
		case EPianoFeedback::Missed:
			if (NoteStates.IsValidIndex(Event.NoteIndex))
			{
				NoteStates[Event.NoteIndex] = EPianoNoteState::Missed;
			}
			++Stats.Missed;
			break;
		case EPianoFeedback::Wrong:
			if (PressResult.IsValidIndex(Event.Pitch)) { PressResult[Event.Pitch] = 0; }
			++Stats.Wrong;
			if (WrongFlashSeconds.IsValidIndex(Event.Pitch))
			{
				WrongFlashSeconds[Event.Pitch] = FPlatformTime::Seconds();
			}
			break;
		case EPianoFeedback::Wait:
			++Stats.Waits;
			break;
		case EPianoFeedback::Looped:
			{
				double LoopStart = 0.0, LoopEnd = 0.0;
				GetLoop(LoopStart, LoopEnd);
				// judge the pass that just ended: every practised note in the loop played, no wrong key
				FPianoPass Pass;
				Pass.Wrong = Stats.Wrong - PassWrongStart;
				Pass.Tempo = GetTempoScale();
				const uint8 Hands = uint8(GetHands());
				for (const FPianoSessionNote& N : PlayNotes)
				{
					// (a note within 10 ms of the loop's end belongs to what follows: engraving times round to the ms)
					if (N.OnMs >= LoopStart - 0.5 && N.OnMs < LoopEnd - 10.0 && !N.bTied && !N.bGrace && (uint8(N.Hand) & Hands) != 0 && !GetListenMode())
					{
						const bool bHit = NoteStates.IsValidIndex(N.SourceIndex) && NoteStates[N.SourceIndex] == EPianoNoteState::Hit;
						(bHit ? Pass.Hits : Pass.Missed) += 1;
						UE_CLOG(!bHit && bAutoplay, LogPianoKit, Log, TEXT("Pass miss: pitch %d at %.1f ms (source %d, state %d, loop %.1f-%.1f)"), N.Pitch, N.OnMs, N.SourceIndex,
							NoteStates.IsValidIndex(N.SourceIndex) ? int32(NoteStates[N.SourceIndex]) : -1, LoopStart, LoopEnd);
					}
				}
				LastPass = Pass;
				++PassSerial;
				PassWrongStart = Stats.Wrong;
				for (const FPianoSessionNote& N : PlayNotes)
				{
					if (N.OnMs >= LoopStart - 0.5 && NoteStates.IsValidIndex(N.SourceIndex))
					{
						NoteStates[N.SourceIndex] = EPianoNoteState::Pending;
					}
				}
				AutoplayNext = Algo::LowerBoundBy(PlayNotes, LoopStart - 0.5, &FPianoSessionNote::OnMs);
				OnPassJudged.Broadcast();
			}
			break;
		case EPianoFeedback::Finished:
			bFinished = true;
			Pause();
			break;
		default:
			break;
		}

		static const TCHAR* Names[] = { TEXT("hit"), TEXT("wrong"), TEXT("missed"), TEXT("wait"), TEXT("resume"), TEXT("looped"), TEXT("finished") };
		LogLine(Names[uint8(Event.Type)], Event.NoteIndex, Event.Pitch, Event.SongMs, Event.ErrorMs);
	}

	// Automated pedalling: lift just after each downbeat, press again 80 ms later (legato pedalling).
	if (bAutoPedal && Score.IsValid() && IsPlaying())
	{
		const double Now = GetSongMs();
		const TArray<FPianoScoreMeasure>& Measures = Score->GetMeasures();
		if (Measures.IsValidIndex(AutoPedalMeasure))
		{
			const double Downbeat = Measures[AutoPedalMeasure].OnMs;
			if (bPedal && Now >= Downbeat + 20.0 && Now < Downbeat + 100.0)
			{
				SetSustainPedal(false);
			}
			else if (Now >= Downbeat + 100.0)
			{
				SetSustainPedal(true);
				++AutoPedalMeasure;
			}
		}
	}

	// Perfect player for automated runs: presses each practised note when the clock reaches it.
	if (bAutoplay && AutoplaySeekMs >= 0.0)
	{
		// until the audio thread has taken the seek, the clock still shows the old place: playing from it would press
		// the new place's notes at once
		const double Raw = GetRawSongMs();
		if ((Raw >= AutoplaySeekMs - 1.0 && Raw <= AutoplaySeekMs + 150.0) || FPlatformTime::Seconds() - AutoplaySeekSeconds > 0.5)
		{
			AutoplaySeekMs = -1.0;
		}
	}
	if (AutoplayCountInSeconds > 0.0 && (GetCountInLeft() > 0 || FPlatformTime::Seconds() - AutoplayCountInSeconds > 0.5))
	{
		AutoplayCountInSeconds = 0.0; // the count-in has started (from here CountInLeft tells when it ends)
	}
	if (bAutoplay && AutoplaySeekMs < 0.0 && AutoplayCountInSeconds == 0.0 && Score.IsValid() && IsPlaying() && GetCountInLeft() == 0)
	{
		const double Now = GetSongMs();
		const TArray<FPianoSessionNote>& Notes = PlayNotes;
		const uint8 Hands = uint8(GetHands());
		const bool bAccentDrill = Drill == EPianoDrill::AccentThree || Drill == EPianoDrill::AccentFour;
		for (int32 Index = AutoplayReleases.Num() - 1; Index >= 0; --Index)
		{
			if (AutoplayReleases[Index].Key <= Now)
			{
				NoteOff(AutoplayReleases[Index].Value);
				AutoplayReleases.RemoveAtSwap(Index);
			}
		}
		for (int32 Index = AutoplayFixes.Num() - 1; Index >= 0; --Index)
		{
			if (AutoplayFixes[Index].Key <= FPlatformTime::Seconds())
			{
				NoteOn(AutoplayFixes[Index].Value, 90);
				AutoplayReleases.Add({ Now + 300.0, AutoplayFixes[Index].Value });
				AutoplayFixes.RemoveAtSwap(Index);
			}
		}
		while (AutoplayNext < Notes.Num() && Notes[AutoplayNext].OnMs <= Now + 1.0)
		{
			const FPianoSessionNote& Note = Notes[AutoplayNext++];
			if ((uint8(Note.Hand) & Hands) != 0 && !Note.bTied && !GetListenMode())
			{
				++AutoplayCount;
				const bool bMistake = AutoplayErrorEvery > 0 && AutoplayCount % AutoplayErrorEvery == 0 && (AutoplayErrorsUntil <= 0 || AutoplayCount <= AutoplayErrorsUntil);
				const int32 Pitch = bMistake ? Note.Pitch + 1 : Note.Pitch;
				NoteOn(Pitch, bAccentDrill ? (Note.bAccent ? 112 : 64) : 90);
				AutoplayReleases.Add({ Note.OnMs + (Note.OffMs - Note.OnMs) * 0.9, Pitch });
				if (bMistake && GetWaitMode())
				{
					// a real player hears the mistake and plays the right key a moment later
					AutoplayFixes.Add({ FPlatformTime::Seconds() + 0.25, Note.Pitch });
				}
			}
		}
	}

	const double NowSeconds = FPlatformTime::Seconds();
	if (NowSeconds >= NextMidiScanSeconds)
	{
		NextMidiScanSeconds = NowSeconds + 3.0; // pick up keyboards plugged in later
		ScanMidiDevices();
	}
}

void UPianoPlayer::LogLine(const TCHAR* Event, int32 NoteIndex, int32 Pitch, double SongMs, float ErrorMs)
{
	if (!Log)
	{
		return;
	}
	const FString Line = FString::Printf(TEXT("%s,%d,%d,%.2f,%.2f,%.4f\n"), Event, NoteIndex, Pitch, SongMs, ErrorMs, FPlatformTime::Seconds());
	const auto Utf8 = StringCast<ANSICHAR>(*Line);
	Log->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
	Log->Flush();
}

TStatId UPianoPlayer::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPianoPlayer, STATGROUP_Tickables);
}
