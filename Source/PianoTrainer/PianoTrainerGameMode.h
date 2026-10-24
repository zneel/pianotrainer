#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PianoTrainerGameMode.generated.h"

class SPianoApp;
class UPianoPlayer;

// Starts a practice session and puts the Slate app on screen. Command line:
//   -score="path"        score to open (default: last opened, else Scores/ode_to_joy.musicxml)
//   -hands=right|left|both  -tempo=0.8  -wait=0|1  -listen
//   -autotest            play the practised hands perfectly, write Saved/PianoLogs/autotest.json, quit
//   -pianolog            log every feedback event to Saved/PianoLogs/session_*.csv
//   -keyboard3d          engine-rendered falling notes and keyboard instead of the 2D (Slate) stage
//   -drill=outline|longshort|shortlong|impulses|blockchords|accent3|accent4   -countin=<bars>
//   -coach=<n> | -coach=s<k>   with -autotest: run session n of the piece's practice plan (made if missing), or
//                        section k alone, until the coach says done or too big; -coachgroups starts note-groups,
//                        -coachsplit cuts a too-big section in half; -diary=<file> keeps the test's diary apart;
//                        -coachhands=auto|separate|together (default auto); -autoerrorsuntil=<n>: mistakes only
//                        among the first n notes
//   -uishot=<s>          screenshot after s seconds (Saved/PianoLogs/ui.png), then quit; with -menupage=etudes|diary|library
//                        and -sidebartab=practice|plan|visual to open on a page or tab
UCLASS()
class APianoTrainerGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APianoTrainerGameMode();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void WriteAutotestReport(const TCHAR* Outcome);
	void StartCoachTest();
	void TickCoachTest();

	UPROPERTY()
	TObjectPtr<UPianoPlayer> Player;

	UPROPERTY()
	TObjectPtr<class APianoKeyboard3D> Keyboard3D;

	UPROPERTY()
	TObjectPtr<class APianoNoteHighway> Highway;

	TSharedPtr<SPianoApp> App;
	bool bAutotest = false;
	bool bAutotestPending = false; // waiting for the piano to load before playing
	double AutotestStartSeconds = 0.0;
	bool bRecordWav = false;   // -recordwav: master output -> Saved/PianoLogs/autotest.wav
	TArray<float> FrameMs;     // frame times during the autotest, for the report
	// Scroll smoothness: per frame, |song-time step - frame time x tempo| for the raw audio clock and the display clock.
	TArray<float> JitterRaw, JitterSmooth;
	double LastRawMs = -1.0, LastSmoothMs = -1.0;
	double ExitAtSeconds = 0.0;
	double StopAtMs = -1.0;
	double ShotAtMs = -1.0; // -shot=<song ms>: save Saved/PianoLogs/shot.png (with UI) during an autotest
	float UiShotSeconds = 0.f;
	// -coach: the session to run, phases seen ("trial>ingrain>done"), passes while in note-groups, sections around a split
	FString CoachArg;
	bool bCoachTest = false, bCoachGroups = false, bCoachSplit = false;
	FString CoachPhases;
	int32 CoachLastPhase = -1, CoachGroupPasses = 0, CoachSeenPass = 0, SectionsBeforeSplit = -1, SectionsAfterSplit = -1;
};
