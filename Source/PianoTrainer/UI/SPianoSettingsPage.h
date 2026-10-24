// The menu's Settings page:
//   Display  - window, borderless or full screen; resolution; vsync
//   Sound    - the piano samples (an .sfz: found in the Instruments folders, or chosen), volume, resampling quality
//   MIDI     - the keyboards connected (with a light when they send), look again, touch (velocity curve), pedal polarity
//   Playback - play the score expressively (dynamics, voicing, accents), and with its pedal marks
// Every change applies at once and is saved.
#pragma once

#include "CoreMinimal.h"
#include "PianoAppSettings.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class UPianoPlayer;

class SPianoSettingsPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPianoSettingsPage) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPianoPlayer>, Player)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;

private:
	TSharedRef<SWidget> MakeDisplay();
	TSharedRef<SWidget> MakeSound();
	TSharedRef<SWidget> MakeMidi();
	TSharedRef<SWidget> MakePlayback();
	TSharedRef<SWidget> MakeDevices();
	TSharedRef<SWidget> MakeResolutions();
	void Changed(); // save and apply

	TWeakObjectPtr<UPianoPlayer> Player;
	FPianoAppSettings Settings;
	FText PluginWindowNote; // shown when a plug-in has no window of its own
	TSharedPtr<SBox> DevicesBox, ResolutionsBox, InstrumentsBox;
	TArray<FString> ShownDevices;
	int32 ShownWindowMode = -1;
};
