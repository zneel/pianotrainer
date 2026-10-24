#include "PianoTrainerGameMode.h"

#include "AudioDevice.h"
#include "AudioMixerBlueprintLibrary.h"

#include "PianoPlayer.h"
#include "PianoScore.h"
#include "PianoLibrary.h"
#include "PianoPhrases.h"
#include "UI/PianoAppSettings.h"
#include "UI/PianoCoach.h"
#include "UI/SPianoApp.h"
#include "UI/SPianoRuler.h"
#include "Piano3D/PianoKeyboard3D.h"
#include "Piano3D/PianoNoteHighway.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "Widgets/SWindow.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Algo/Accumulate.h"
#include "Algo/Count.h"
#include "UnrealClient.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogPianoTrainer, Log, All);

APianoTrainerGameMode::APianoTrainerGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = nullptr;
}

void APianoTrainerGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Present in step with the display: scrolling sheet music judders without vsync (-novsync to measure without).
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Settings->SetVSyncEnabled(!FParse::Param(FCommandLine::Get(), TEXT("novsync")));
		Settings->SetFrameRateLimit(0.f);
		Settings->ApplyNonResolutionSettings();
	}

	Player = NewObject<UPianoPlayer>(this);
	{
		// the app's saved settings: the instrument before the sound loads, the rest at once
		FPianoAppSettings AppSettings;
		AppSettings.Load();
		AppSettings.ApplyInstrument(Player);
		AppSettings.Apply(Player);
		AppSettings.ApplyVSync();
		int32 Flag = 0; // tests: -expressive=0|1 -scorepedal=0|1, whatever is saved
		if (FParse::Value(FCommandLine::Get(), TEXT("-expressive="), Flag)) { Player->SetExpressive(Flag != 0); }
		if (FParse::Value(FCommandLine::Get(), TEXT("-scorepedal="), Flag)) { Player->SetScorePedal(Flag != 0); }
		if (FParse::Value(FCommandLine::Get(), TEXT("-metronome="), Flag)) { Player->SetMetronome(Flag != 0); }
		// -instrumentcc=20:0,21:0: instrument controls (0..1) for testing, e.g. a sample library's resonance and noise layers
		FString Controls;
		if (FParse::Value(FCommandLine::Get(), TEXT("-instrumentcc="), Controls, /*bShouldStopOnSeparator*/ false))
		{
			TArray<FString> Pairs;
			Controls.ParseIntoArray(Pairs, TEXT(","));
			for (const FString& Pair : Pairs)
			{
				FString Cc, Value;
				if (Pair.Split(TEXT(":"), &Cc, &Value))
				{
					Player->SetInstrumentControl(FCString::Atoi(*Cc), FCString::Atof(*Value));
				}
			}
		}
	}
	if (!Player->Initialize(GetWorld()))
	{
		UE_LOG(LogPianoTrainer, Error, TEXT("Piano player failed to start"));
	}

	const TCHAR* Cmd = FCommandLine::Get();
	FString Hands;
	if (FParse::Value(Cmd, TEXT("-hands="), Hands))
	{
		Player->SetHands(Hands == TEXT("right") ? EPianoHand::Right : Hands == TEXT("left") ? EPianoHand::Left : EPianoHand::Both);
	}
	float Tempo = 1.f;
	if (FParse::Value(Cmd, TEXT("-tempo="), Tempo))
	{
		Player->SetTempoScale(Tempo);
	}
	int32 Wait = 1;
	if (FParse::Value(Cmd, TEXT("-wait="), Wait))
	{
		Player->SetWaitMode(Wait != 0);
	}
	Player->SetListenMode(FParse::Param(Cmd, TEXT("listen")));

	// Score: command line (opens it straight away), else the menu comes first.
	FString ScorePath;
	const bool bScoreGiven = FParse::Value(Cmd, TEXT("-score="), ScorePath);
	bAutotest = FParse::Param(Cmd, TEXT("autotest"));
	if (bAutotest && !bScoreGiven)
	{
		ScorePath = FPaths::ProjectDir() / TEXT("Scores/ode_to_joy.musicxml");
	}

	// The stage is drawn in 2D by Slate; -keyboard3d renders falling notes and keyboard with the engine instead.
	SPianoApp::FPickPitch PickPitch;
	if (!FParse::Param(Cmd, TEXT("keyboard3d")))
	{
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->bDisableWorldRendering = true; // nothing 3D to draw: save the GPU
		}
	}
	else
	{
		Keyboard3D = GetWorld()->SpawnActor<APianoKeyboard3D>();
		Keyboard3D->Init(Player);
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			PC->SetViewTarget(Keyboard3D);
		}
		Highway = GetWorld()->SpawnActor<APianoNoteHighway>();
		Highway->Init(Player, Keyboard3D);
		TWeakObjectPtr<APianoKeyboard3D> WeakKeyboard = Keyboard3D;
		PickPitch = [WeakKeyboard](const FVector2D& Pixel)
		{
			return WeakKeyboard.IsValid() ? WeakKeyboard->PitchAtViewportPosition(Pixel) : int32(INDEX_NONE);
		};
	}

	App = SNew(SPianoApp).Player(Player).PickPitch(PickPitch).StartInMenu(!bScoreGiven && !bAutotest);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->AddViewportWidgetContent(App.ToSharedRef(), 10);
	}
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->bShowMouseCursor = true;
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(App);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	if (!ScorePath.IsEmpty())
	{
		App->OpenScore(ScorePath);
	}
	FSlateApplication::Get().SetKeyboardFocus(App, EFocusCause::SetDirectly);

	FParse::Value(Cmd, TEXT("-shot="), ShotAtMs);
	FParse::Value(Cmd, TEXT("-uishot="), UiShotSeconds); // screenshot after N seconds (Saved/PianoLogs/ui.png) and quit
	FParse::Value(Cmd, TEXT("-plantest="), PlanTestMode);
	FString DrillName;
	if (FParse::Value(Cmd, TEXT("-drill="), DrillName))
	{
		// -drill=outline | longshort | shortlong | impulses | blockchords | accent3 | accent4
		static const TMap<FString, EPianoDrill> Drills = { { TEXT("outline"), EPianoDrill::Outline }, { TEXT("longshort"), EPianoDrill::LongShort }, { TEXT("shortlong"), EPianoDrill::ShortLong },
			{ TEXT("impulses"), EPianoDrill::Impulses }, { TEXT("blockchords"), EPianoDrill::BlockChords }, { TEXT("accent3"), EPianoDrill::AccentThree }, { TEXT("accent4"), EPianoDrill::AccentFour } };
		if (const EPianoDrill* Drill = Drills.Find(DrillName.ToLower()))
		{
			Player->SetDrill(*Drill);
		}
	}
	int32 CountIn = 0;
	if (FParse::Value(Cmd, TEXT("-countin="), CountIn))
	{
		Player->SetCountInBars(CountIn);
	}
	if (bAutotest)
	{
		int32 ErrorEvery = 0, ErrorsUntil = 0;
		FParse::Value(Cmd, TEXT("-autoerrors="), ErrorEvery);
		FParse::Value(Cmd, TEXT("-autoerrorsuntil="), ErrorsUntil); // mistakes only among the first N notes
		Player->SetAutoplay(true, ErrorEvery, ErrorsUntil);
		Player->SetAutoPedal(FParse::Param(Cmd, TEXT("autopedal")));
		Player->Restart();
		double StartMs = 0.0;
		FParse::Value(Cmd, TEXT("-stop="), StopAtMs);
		if (FParse::Value(Cmd, TEXT("-start="), StartMs))
		{
			Player->Seek(StartMs); // -start=<song ms>: begin the run part-way into the piece
		}
		bRecordWav = FParse::Param(Cmd, TEXT("recordwav"));
		bCoachTest = FParse::Value(Cmd, TEXT("-coach="), CoachArg);
		bCoachGroups = FParse::Param(Cmd, TEXT("coachgroups"));
		bCoachSplit = FParse::Param(Cmd, TEXT("coachsplit"));
		bPluginWindowTest = FParse::Param(Cmd, TEXT("pluginwindow"));
		bAutotestPending = true; // starts in Tick once the piano has loaded
	}
}

void APianoTrainerGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (App.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(App.ToSharedRef());
	}
	App.Reset();
	if (Player)
	{
		Player->Shutdown();
	}
	Super::EndPlay(EndPlayReason);
}

void APianoTrainerGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Frame the 3D keyboard into the stage: under the falling notes, or filling a keyboard-high stage.
	float Left = 0.f, Top = 0.f, Right = 1.f, Bottom = 1.f;
	if (Keyboard3D && App.IsValid() && App->GetStageRect(Left, Top, Right, Bottom))
	{
		const FPianoViewSettings& View = App->GetSettings();
		Keyboard3D->SetScreenRect(Left, View.bShowHighway ? App->GetKeyboardTop() : Top, Right, Bottom);
		if (Highway)
		{
			Highway->SetShown(View.bShowHighway);
			Highway->SetStageTop(Top);
			Highway->SetSparks(View.bSparks);
			Highway->SetColorByHand(View.bColorByHand);
			Highway->SetLookaheadMs(View.LookaheadMs);
		}
	}
	if (UiShotSeconds > 0.f)
	{
		UiShotSeconds -= DeltaSeconds;
		if (UiShotSeconds <= 0.f)
		{
			const FString ShotPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("PianoLogs/ui.png"));
			FScreenshotRequest::RequestScreenshot(ShotPath, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
			ExitAtSeconds = FPlatformTime::Seconds() + 1.5;
		}
	}
	if (ExitAtSeconds > 0.0 && FPlatformTime::Seconds() >= ExitAtSeconds)
	{
		ExitAtSeconds = 0.0;
		FPlatformMisc::RequestExit(false, TEXT("PianoTrainer autotest"));
	}
	if (!PlanTestMode.IsEmpty() && Player)
	{
		TickPlanTest();
	}
	if (!bAutotest || !Player)
	{
		return;
	}
	if (bAutotestPending)
	{
		if (Player->GetSoundState() == UPianoPlayer::ESoundState::Loading)
		{
			return;
		}
		bAutotestPending = false;
		if (bCoachTest)
		{
			StartCoachTest();
		}
		else
		{
			Player->Play();
		}
		AutotestStartSeconds = FPlatformTime::Seconds();
		if (bRecordWav)
		{
			UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 600.f);
		}
		// -quiet: nothing out of the speakers (tests run while you work); the recording is taken before the main
		// output's volume
		FAudioDevice* Device = GetWorld()->GetAudioDeviceRaw();
		if (Device && FParse::Param(FCommandLine::Get(), TEXT("quiet")))
		{
			Device->SetSubmixOutputVolume(&Device->GetMainSubmixObject(), 0.f);
		}
		UE_LOG(LogPianoTrainer, Log, TEXT("Autotest started (sound %d)"), int32(Player->GetSoundState()));
	}
	FrameMs.Add(DeltaSeconds * 1000.f);
	const double RawMs = Player->GetRawSongMs(), SmoothMs = Player->GetSongMs();
	if (Player->IsPlaying() && !Player->IsWaiting() && LastRawMs >= 0.0)
	{
		const double Expected = DeltaSeconds * 1000.0 * Player->GetTempoScale();
		JitterRaw.Add(float(FMath::Abs((RawMs - LastRawMs) - Expected)));
		JitterSmooth.Add(float(FMath::Abs((SmoothMs - LastSmoothMs) - Expected)));
	}
	LastRawMs = RawMs;
	LastSmoothMs = SmoothMs;
	if (ShotAtMs >= 0.0 && Player->GetSongMs() >= ShotAtMs)
	{
		ShotAtMs = -1.0;
		const FString ShotPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("PianoLogs/shot.png"));
		FScreenshotRequest::RequestScreenshot(ShotPath, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogPianoTrainer, Log, TEXT("Screenshot requested at %.0f ms -> %s"), Player->GetSongMs(), *ShotPath);
	}
	if (bPluginWindowTest)
	{
		TickPluginWindowTest();
	}
	if (bCoachTest)
	{
		TickCoachTest();
		return;
	}
	const TSharedPtr<FPianoScore> Score = Player->GetScore();
	const double Timeout = 15.0 + (Score.IsValid() ? Score->GetLengthMs() / 1000.0 / FMath::Max(0.1f, Player->GetTempoScale()) * 2.0 : 0.0);
	if (Player->IsFinished())
	{
		WriteAutotestReport(TEXT("finished"));
	}
	else if (Player->IsAudioStalled())
	{
		WriteAutotestReport(TEXT("audio_stall"));
	}
	else if (StopAtMs > 0.0 && Player->GetSongMs() >= StopAtMs)
	{
		WriteAutotestReport(TEXT("stopped")); // -stop=<song ms>
	}
	else if (FPlatformTime::Seconds() - AutotestStartSeconds > Timeout)
	{
		WriteAutotestReport(TEXT("timeout"));
	}
}

void APianoTrainerGameMode::TickPluginWindowTest()
{
	const double Elapsed = FPlatformTime::Seconds() - AutotestStartSeconds;
	if (PluginWindowStep == 0 && Elapsed > 1.0)
	{
		PluginWindowStep = 1;
		const TSharedPtr<SWindow> Window = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
		const bool bOpened = Player->OpenInstrumentWindow(Window && Window->GetNativeWindow() ? Window->GetNativeWindow()->GetOSWindowHandle() : nullptr);
		UE_LOG(LogPianoTrainer, Log, TEXT("pluginwindow: opened %d, open %d"), bOpened, Player->IsInstrumentWindowOpen());
	}
	else if (PluginWindowStep == 1 && Elapsed > 6.0)
	{
		PluginWindowStep = 2;
		Player->CloseInstrumentWindow();
	}
	else if (PluginWindowStep == 2 && Elapsed > 6.5)
	{
		PluginWindowStep = 3;
		const FString State = UPianoPlayer::GetPluginStateFile(Player->GetInstrumentPath());
		UE_LOG(LogPianoTrainer, Log, TEXT("pluginwindow: open after closing %d, state %s, %lld bytes"), Player->IsInstrumentWindowOpen(), *State,
			IFileManager::Get().FileSize(*State));
	}
}

void APianoTrainerGameMode::StartCoachTest()
{
	FPianoCoach* Coach = App.IsValid() ? App->GetCoach() : nullptr;
	const TSharedPtr<FPianoScore> Score = Player->GetScore();
	if (!Coach || !Score.IsValid())
	{
		WriteAutotestReport(TEXT("coach_unavailable"));
		return;
	}
	if (!Coach->GetPlan())
	{
		Coach->MakePlan(Score->GetTitle());
	}
	FString Hands = TEXT("auto"); // -coachhands=auto|separate|together (for this run only, whatever the saved choice)
	FParse::Value(FCommandLine::Get(), TEXT("-coachhands="), Hands);
	Coach->SetHandsMode(Hands == TEXT("separate") ? ECoachHands::Separate : Hands == TEXT("together") ? ECoachHands::Together : ECoachHands::Auto, false);
	if (CoachArg.StartsWith(TEXT("s")))
	{
		Coach->StartSection(FCString::Atoi(*CoachArg.Mid(1)));
	}
	else
	{
		Coach->Start(FCString::Atoi(*CoachArg));
	}
	if (bCoachGroups)
	{
		Coach->StartGroups();
	}
	CoachSeenPass = Player->GetPassSerial();
	UE_LOG(LogPianoTrainer, Log, TEXT("Coach test: %s (%s)"), *Coach->GetLabel(), *Coach->GetAdvice().ToString());
}

void APianoTrainerGameMode::TickCoachTest()
{
	static const TCHAR* PhaseNames[] = { TEXT("idle"), TEXT("trial"), TEXT("ingrain"), TEXT("groups"), TEXT("done"), TEXT("toobig"), TEXT("hands") };
	static const TCHAR* HandNames[] = { TEXT("?"), TEXT("right"), TEXT("left"), TEXT("both") };
	FPianoCoach* Coach = App.IsValid() ? App->GetCoach() : nullptr;
	if (!Coach)
	{
		WriteAutotestReport(TEXT("coach_unavailable"));
		return;
	}
	const int32 Phase = int32(Coach->GetPhase());
	if (Player->GetPassSerial() != CoachSeenPass)
	{
		CoachSeenPass = Player->GetPassSerial();
		CoachGroupPasses += Phase == int32(ECoachPhase::Groups) ? 1 : 0;
		const FPianoPass& Pass = Player->GetLastPass();
		UE_LOG(LogPianoTrainer, Log, TEXT("Coach pass: %s (next: %s hand) hits %d missed %d wrong %d -> %s"), PhaseNames[Phase], HandNames[uint8(Player->GetHands()) & 3],
			Pass.Hits, Pass.Missed, Pass.Wrong, *Coach->GetAdvice().ToString());
	}
	if (Phase != CoachLastPhase)
	{
		CoachPhases += (CoachPhases.IsEmpty() ? TEXT("") : TEXT(">")) + FString(PhaseNames[Phase]);
		CoachLastPhase = Phase;
	}
	if (Coach->GetPhase() == ECoachPhase::TooBig && bCoachSplit && SectionsBeforeSplit < 0)
	{
		SectionsBeforeSplit = Coach->GetPlan() ? Coach->GetPlan()->Sections.Num() : 0;
		Coach->Split();
		SectionsAfterSplit = Coach->GetPlan() ? Coach->GetPlan()->Sections.Num() : 0;
		WriteAutotestReport(TEXT("coach_split"));
	}
	else if (Coach->GetPhase() == ECoachPhase::Done)
	{
		WriteAutotestReport(TEXT("coach_done"));
	}
	else if (Coach->GetPhase() == ECoachPhase::TooBig)
	{
		WriteAutotestReport(TEXT("coach_toobig"));
	}
	else if (Coach->GetPhase() == ECoachPhase::Idle)
	{
		WriteAutotestReport(TEXT("coach_idle"));
	}
	else if (FPlatformTime::Seconds() - AutotestStartSeconds > 600.0)
	{
		WriteAutotestReport(TEXT("timeout"));
	}
}

void APianoTrainerGameMode::TickPlanTest()
{
	FPianoCoach* Coach = App.IsValid() ? App->GetCoach() : nullptr;
	const TSharedPtr<SPianoRuler> Ruler = App.IsValid() ? App->GetRuler() : nullptr;
	const TSharedPtr<FPianoScore> Score = Player->GetScore();
	if (++PlanTestFrame < PlanTestNextFrame || !Coach || !Ruler.IsValid() || !Score.IsValid())
	{
		return;
	}
	static const TCHAR* StageNames[] = { TEXT("new"), TEXT("learned"), TEXT("mastered"), TEXT("omniscient") };
	if (!PlanTestResult.IsValid())
	{
		PlanTestResult = MakeShared<FJsonObject>();
	}
	// the plan as it is now: one entry per section
	auto Record = [&](const TCHAR* Step)
	{
		TArray<TSharedPtr<FJsonValue>> List;
		if (const FPianoPlan* Plan = Coach->GetPlan())
		{
			for (const FPianoSection& S : Plan->Sections)
			{
				TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("label"), S.Label);
				O->SetNumberField(TEXT("first"), S.FirstBeat);
				O->SetNumberField(TEXT("end"), S.EndBeat);
				O->SetNumberField(TEXT("repeatOf"), S.RepeatOf);
				O->SetStringField(TEXT("stage"), StageNames[uint8(S.Stage) & 3]);
				List.Add(MakeShared<FJsonValueObject>(O));
			}
		}
		PlanTestResult->SetArrayField(Step, List);
		UE_LOG(LogPianoTrainer, Log, TEXT("Plan test: %s, %d sections"), Step, List.Num());
	};
	auto Finish = [&]()
	{
		// the phrase cues the plan was cut from (beats scoring at least 1)
		const FPianoPhrases Phrases = FPianoPhrases::Analyse(*Score);
		TArray<TSharedPtr<FJsonValue>> Cues;
		for (int32 B = 1; B < Phrases.NumBeats; ++B)
		{
			if (Phrases.Strength[B] >= 1.f)
			{
				TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
				const int32 Measure = Score->GetMeasureAt(Phrases.BeatMs[B] + 1.0);
				O->SetNumberField(TEXT("beat"), B);
				O->SetStringField(TEXT("at"), FString::Printf(TEXT("%d.%d"), Measure + 1, B - Phrases.NearestBeat(Score->GetMeasureStartMs(Measure)) + 1));
				O->SetNumberField(TEXT("strength"), FMath::RoundToInt32(Phrases.Strength[B] * 10.f) / 10.0);
				O->SetStringField(TEXT("why"), Phrases.Why[B]);
				Cues.Add(MakeShared<FJsonValueObject>(O));
			}
		}
		PlanTestResult->SetArrayField(TEXT("cues"), Cues);
		TArray<TSharedPtr<FJsonValue>> Slurs;
		auto At = [&](double Ms)
		{
			const int32 B = Phrases.BeatAt(Ms), Measure = Score->GetMeasureAt(Phrases.BeatMs[B] + 1.0);
			return FString::Printf(TEXT("%d.%d"), Measure + 1, B - Phrases.NearestBeat(Score->GetMeasureStartMs(Measure)) + 1);
		};
		for (const FPianoScoreSlur& Slur : Score->GetMarks().Slurs)
		{
			Slurs.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s-%s (%d beats, staff %d)"), *At(Slur.StartMs), *At(Slur.EndMs),
				Phrases.BeatAt(Slur.EndMs) - Phrases.BeatAt(Slur.StartMs), Slur.Staff)));
		}
		PlanTestResult->SetArrayField(TEXT("slurList"), Slurs);

		// scrolling: how even the line's speed is (every 20 ms; jumps back at repeats left out), and how far each
		// note's head is from the cursor when its time comes (engraving pixels; a notehead is ~20 wide)
		TArray<double> Speeds, Errors;
		for (double Ms = 0.0; Ms + 20.0 < Score->GetLengthMs(); Ms += 20.0)
		{
			const double Speed = (Score->TimeToX(Ms + 20.0) - Score->TimeToX(Ms)) / 20.0;
			if (Speed > 0.0)
			{
				Speeds.Add(Speed);
			}
		}
		TArray<double> OnsetMs, OnsetX, OldSpeeds; // the old mapping: straight from notehead to notehead
		for (const FPianoScoreNote& Note : Score->GetNotes())
		{
			if (Note.Head.bIsValid && Note.IsStruck())
			{
				Errors.Add(FMath::Abs(Score->TimeToX(Note.OnMs) - Note.Head.GetCenter().X));
				if (OnsetMs.Num() == 0 || Note.OnMs - OnsetMs.Last() > 1.0)
				{
					OnsetMs.Add(Note.OnMs);
					OnsetX.Add(Note.Head.GetCenter().X);
				}
			}
		}
		for (int32 I = 0; I + 1 < OnsetMs.Num(); ++I)
		{
			const double Speed = (OnsetX[I + 1] - OnsetX[I]) / FMath::Max(1.0, OnsetMs[I + 1] - OnsetMs[I]);
			for (double Ms = OnsetMs[I]; Speed > 0.0 && Ms < OnsetMs[I + 1]; Ms += 20.0)
			{
				OldSpeeds.Add(Speed);
			}
		}
		auto Percentile = [](TArray<double> Values, double P) { Values.Sort(); return Values.Num() > 0 ? Values[FMath::Clamp(int32(P * Values.Num()), 0, Values.Num() - 1)] : 0.0; };
		double Mean = 0.0, Var = 0.0, Accel = 0.0;
		for (const double S : Speeds) { Mean += S / FMath::Max(1, Speeds.Num()); }
		for (int32 I = 0; I < Speeds.Num(); ++I)
		{
			Var += FMath::Square(Speeds[I] - Mean) / FMath::Max(1, Speeds.Num());
			Accel = I > 0 ? FMath::Max(Accel, FMath::Abs(Speeds[I] - Speeds[I - 1]) / Mean) : Accel;
		}
		TSharedRef<FJsonObject> Scroll = MakeShared<FJsonObject>();
		Scroll->SetNumberField(TEXT("speed_cv"), FMath::RoundToInt32(FMath::Sqrt(Var) / FMath::Max(1e-9, Mean) * 1000.0) / 1000.0);
		Scroll->SetNumberField(TEXT("speed_p95_over_p5"), FMath::RoundToInt32(Percentile(Speeds, 0.95) / FMath::Max(1e-9, Percentile(Speeds, 0.05)) * 100.0) / 100.0);
		Scroll->SetNumberField(TEXT("max_step_change"), FMath::RoundToInt32(Accel * 1000.0) / 1000.0); // largest change of speed in 20 ms, relative
		Scroll->SetNumberField(TEXT("note_offset_px_median"), FMath::RoundToInt32(Percentile(Errors, 0.5) * 10.0) / 10.0);
		Scroll->SetNumberField(TEXT("note_offset_px_p95"), FMath::RoundToInt32(Percentile(Errors, 0.95) * 10.0) / 10.0);
		Scroll->SetNumberField(TEXT("width_px"), FMath::RoundToInt32(Score->GetSize().X));
		double OldMean = 0.0, OldVar = 0.0;
		for (const double S : OldSpeeds) { OldMean += S / FMath::Max(1, OldSpeeds.Num()); }
		for (const double S : OldSpeeds) { OldVar += FMath::Square(S - OldMean) / FMath::Max(1, OldSpeeds.Num()); }
		Scroll->SetNumberField(TEXT("note_by_note_speed_cv"), FMath::RoundToInt32(FMath::Sqrt(OldVar) / FMath::Max(1e-9, OldMean) * 1000.0) / 1000.0);
		Scroll->SetNumberField(TEXT("note_by_note_p95_over_p5"), FMath::RoundToInt32(Percentile(OldSpeeds, 0.95) / FMath::Max(1e-9, Percentile(OldSpeeds, 0.05)) * 100.0) / 100.0);
		PlanTestResult->SetObjectField(TEXT("scroll"), Scroll);
		PlanTestResult->SetStringField(TEXT("score"), Score->GetTitle());
		PlanTestResult->SetNumberField(TEXT("beats"), Phrases.NumBeats);
		PlanTestResult->SetNumberField(TEXT("slurs"), Score->GetMarks().Slurs.Num());
		PlanTestResult->SetNumberField(TEXT("fermatas"), Score->GetMarks().Fermatas.Num());
		FString Text;
		FJsonSerializer::Serialize(PlanTestResult.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
		const FString Path = FPaths::ProjectSavedDir() / TEXT("PianoLogs/plantest.json");
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogPianoTrainer, Log, TEXT("Plan test -> %s"), *FPaths::ConvertRelativePathToFull(Path));
		PlanTestMode.Reset();
		ExitAtSeconds = FPlatformTime::Seconds() + 1.0;
	};

	// real mouse input, routed by Slate to whatever is under the pointer (the ruler's plan strip)
	FSlateApplication& Slate = FSlateApplication::Get();
	auto Pointer = [](const FVector2D& At, bool bHeld, FKey Button)
	{
		TSet<FKey> Pressed;
		if (bHeld)
		{
			Pressed.Add(EKeys::LeftMouseButton);
		}
		return FPointerEvent(0, 0, At, At, Pressed, Button, 0.f, FModifierKeysState());
	};
	auto DragFromTo = [&](const FVector2D& From, const FVector2D& To)
	{
		Slate.ProcessMouseMoveEvent(Pointer(From, false, EKeys::Invalid));
		const bool bDown = Slate.ProcessMouseButtonDownEvent(nullptr, Pointer(From, true, EKeys::LeftMouseButton));
		Slate.ProcessMouseMoveEvent(Pointer(FMath::Lerp(From, To, 0.5), true, EKeys::Invalid));
		Slate.ProcessMouseMoveEvent(Pointer(To, true, EKeys::Invalid));
		const bool bUp = Slate.ProcessMouseButtonUpEvent(Pointer(To, false, EKeys::LeftMouseButton));
		const FGeometry& G = Ruler->GetTickSpaceGeometry();
		UE_LOG(LogPianoTrainer, Log, TEXT("Plan test drag (%.0f,%.0f) -> (%.0f,%.0f): down %d up %d; ruler at (%.0f,%.0f) size (%.0f,%.0f) scale %.2f"),
			From.X, From.Y, To.X, To.Y, bDown, bUp, G.GetAbsolutePosition().X, G.GetAbsolutePosition().Y, G.GetAbsoluteSize().X, G.GetAbsoluteSize().Y, G.Scale);
	};
	const FPianoPhrases Phrases = FPianoPhrases::Analyse(*Score);
	auto BarBeat = [&](int32 Bar) { return Phrases.NearestBeat(Score->GetMeasureStartMs(Bar - 1)); }; // bar 1 = the first

	switch (PlanTestStep++)
	{
	case 0:
		if (!Coach->GetPlan())
		{
			Coach->MakePlan(Score->GetTitle());
		}
		Record(TEXT("made"));
		if (PlanTestMode != TEXT("edit"))
		{
			Finish();
		}
		PlanTestNextFrame = PlanTestFrame + 6; // the ruler grows its plan strip
		break;
	case 1:
		// drag the edge after the first section to the start of bar 4
		DragFromTo(Ruler->GetPlanBeatScreenPosition(Coach->GetPlan()->Sections[1].FirstBeat), Ruler->GetPlanBeatScreenPosition(BarBeat(4)));
		Record(TEXT("moved"));
		PlanTestNextFrame = PlanTestFrame + 3;
		break;
	case 2:
	{
		// double-click at bar 11: that section splits there
		const FVector2D At = Ruler->GetPlanBeatScreenPosition(BarBeat(11));
		Slate.ProcessMouseMoveEvent(Pointer(At, false, EKeys::Invalid));
		Slate.ProcessMouseButtonDownEvent(nullptr, Pointer(At, true, EKeys::LeftMouseButton));
		Slate.ProcessMouseButtonUpEvent(Pointer(At, false, EKeys::LeftMouseButton));
		Slate.ProcessMouseButtonDoubleClickEvent(nullptr, Pointer(At, true, EKeys::LeftMouseButton));
		Slate.ProcessMouseButtonUpEvent(Pointer(At, false, EKeys::LeftMouseButton));
		Record(TEXT("split"));
		PlanTestNextFrame = PlanTestFrame + 3;
		break;
	}
	case 3:
		// drag that new edge onto the start of its section: the two merge again
		DragFromTo(Ruler->GetPlanBeatScreenPosition(BarBeat(11)), Ruler->GetPlanBeatScreenPosition(BarBeat(9)));
		Record(TEXT("merged"));
		PlanTestNextFrame = PlanTestFrame + 3;
		break;
	default:
		// the seven-times test failed on the last section: cut it in half
		Coach->StartSection(Coach->GetPlan()->Sections.Num() - 1);
		Coach->Split();
		Record(TEXT("halved"));
		Coach->Stop();
		Finish();
		break;
	}
}

void APianoTrainerGameMode::WriteAutotestReport(const TCHAR* Outcome)
{
	bAutotest = false;
	const FPianoStats& Stats = Player->GetStats();
	const TSharedPtr<FPianoScore> Score = Player->GetScore();
	int32 Expected = 0;
	if (Score.IsValid())
	{
		for (const FPianoScoreNote& Note : Score->GetNotes())
		{
			Expected += (Note.IsStruck() && (uint8(Note.Hand) & uint8(Player->GetHands())) != 0) ? 1 : 0;
		}
	}
	// Frame times, skipping the first second (startup, first tiles).
	TArray<float> Frames(FrameMs.GetData() + FMath::Min(FrameMs.Num(), 60), FMath::Max(0, FrameMs.Num() - 60));
	Frames.Sort();
	const float FrameMean = Frames.Num() > 0 ? Algo::Accumulate(Frames, 0.f) / Frames.Num() : 0.f;
	const float FrameP99 = Frames.Num() > 0 ? Frames[FMath::Min(Frames.Num() - 1, Frames.Num() * 99 / 100)] : 0.f;
	const float FrameMax = Frames.Num() > 0 ? Frames.Last() : 0.f;
	const int32 Over33 = Algo::CountIf(Frames, [](float Ms) { return Ms > 33.4f; });
	auto Rms = [](const TArray<float>& Values) { double Sum = 0.0; for (float V : Values) { Sum += V * V; } return Values.Num() > 0 ? FMath::Sqrt(Sum / Values.Num()) : 0.0; };
	auto P99 = [](TArray<float> Values) { Values.Sort(); return Values.Num() > 0 ? Values[FMath::Min(Values.Num() - 1, Values.Num() * 99 / 100)] : 0.f; };
	const FString FrameJson = FString::Printf(TEXT("  \"frames\": %d,\n  \"frame_ms_mean\": %.2f,\n  \"frame_ms_p99\": %.2f,\n  \"frame_ms_max\": %.2f,\n  \"frames_over_33ms\": %d,\n")
		TEXT("  \"scroll_jitter_raw_ms_rms\": %.2f,\n  \"scroll_jitter_raw_ms_p99\": %.2f,\n  \"scroll_jitter_smooth_ms_rms\": %.2f,\n  \"scroll_jitter_smooth_ms_p99\": %.2f,\n"),
		Frames.Num(), FrameMean, FrameP99, FrameMax, Over33, Rms(JitterRaw), P99(JitterRaw), Rms(JitterSmooth), P99(JitterSmooth));

	FString CoachJson;
	if (FPianoCoach* Coach = bCoachTest && App.IsValid() ? App->GetCoach() : nullptr)
	{
		static const TCHAR* StageNames[] = { TEXT("new"), TEXT("learned"), TEXT("mastered"), TEXT("omniscient") };
		const FPianoPlan* Plan = Coach->GetPlan();
		const FPianoPlanSession Session = Coach->GetSession();
		const FPianoSection* Section = Plan && Plan->Sections.IsValidIndex(Session.First) ? &Plan->Sections[Session.First] : nullptr;
		const TArray<FPianoDiaryEntry>& Entries = Coach->GetDiary().GetEntries();
		CoachJson = FString::Printf(TEXT("  \"coach_label\": \"%s\",\n  \"coach_phases\": \"%s\",\n  \"coach_passes\": %d,\n  \"coach_clean\": %d,\n  \"coach_target\": %d,\n")
			TEXT("  \"coach_seconds\": %.1f,\n  \"coach_group_passes\": %d,\n  \"coach_cold_check\": %s,\n  \"plan_sections\": %d,\n  \"plan_next_session\": %d,\n")
			TEXT("  \"section_stage\": \"%s\",\n  \"section_label\": \"%s\",\n  \"section_best_tempo\": %.2f,\n  \"sections_before_split\": %d,\n  \"sections_after_split\": %d,\n")
			TEXT("  \"plan_hardest\": \"%s\",\n  \"plan_due_review\": \"%s\",\n  \"coach_hands_mode\": %d,\n  \"coach_hand_turns\": \"%s\",\n  \"diary_entries\": %d,\n  \"diary_last\": \"%s\",\n"),
			*Coach->GetLabel(), *CoachPhases, Coach->GetPasses(), Coach->GetClean(), Coach->GetTarget(), Coach->GetSeconds(), CoachGroupPasses,
			Coach->IsColdCheck() ? TEXT("true") : TEXT("false"), Plan ? Plan->Sections.Num() : 0, Plan ? Plan->NextSession : -1,
			Section ? StageNames[uint8(Section->Stage)] : TEXT(""), Section ? *Section->Label : TEXT(""), Section ? Section->BestTempo : 0.f,
			SectionsBeforeSplit, SectionsAfterSplit,
			Plan && Plan->Hardest() != INDEX_NONE ? *Plan->Sections[Plan->Hardest()].Label : TEXT(""),
			Plan && Plan->DueReview(FPianoDiary::Today()) != INDEX_NONE ? *Plan->Sections[Plan->DueReview(FPianoDiary::Today())].Label : TEXT(""),
			int32(Coach->GetHandsMode()), *Coach->GetHandTurns(),
			Entries.Num(),
			Entries.Num() > 0 ? *FString::Printf(TEXT("%s %s: %s"), *Entries.Last().What, *Entries.Last().Date, *Entries.Last().Result) : TEXT(""));
	}
	const FString Json = FString::Printf(
		TEXT("{\n  \"outcome\": \"%s\",\n  \"score\": \"%s\",\n  \"expected\": %d,\n  \"hits\": %d,\n  \"wrong\": %d,\n  \"missed\": %d,\n")
		TEXT("  \"waits\": %d,\n  \"mean_abs_error_ms\": %.2f,\n  \"accent_contrast\": %.1f,\n  \"wall_seconds\": %.2f,\n  \"song_ms\": %.1f,\n%s%s  \"wait_mode\": %s,\n  \"tempo\": %.2f\n}\n"),
		Outcome, Score.IsValid() ? *Score->GetTitle() : TEXT(""), Expected, Stats.Hits, Stats.Wrong, Stats.Missed, Stats.Waits,
		Stats.MeanAbsErrorMs(), Stats.AccentContrast(), FPlatformTime::Seconds() - AutotestStartSeconds, Player->GetSongMs(), *FrameJson, *CoachJson,
		Player->GetWaitMode() ? TEXT("true") : TEXT("false"), Player->GetTempoScale());
	const FString Path = FPaths::ProjectSavedDir() / TEXT("PianoLogs/autotest.json");
	FFileHelper::SaveStringToFile(Json, *Path);
	UE_LOG(LogPianoTrainer, Log, TEXT("Autotest %s -> %s\n%s"), Outcome, *FPaths::ConvertRelativePathToFull(Path), *Json);
	if (bRecordWav)
	{
		// master output of the whole run, for checking that the sampler really sounds at the note onsets
		UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile, TEXT("autotest"),
			FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("PianoLogs")));
	}
	ExitAtSeconds = FPlatformTime::Seconds() + 2.0; // let the wav writer finish
}
