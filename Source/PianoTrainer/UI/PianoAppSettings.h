// The app's own settings (Settings page): sound, MIDI and playback, saved with the user settings and applied to the
// player at start. Display settings (window, resolution) are the engine's UGameUserSettings; vsync is kept here
// because the app wants it on by default (the scrolling sheet judders without it).
#pragma once

#include "CoreMinimal.h"

class UPianoPlayer;

struct FPianoAppSettings
{
	FString InstrumentPath;     // empty: the first .sfz found in the Instruments folders
	int32 SampleQuality = 2;    // sfizz resampling: 2 standard, 4 high, 8 best (4 doubled the render time per voice)
	float VolumeDb = -6.f; // headroom: a loud pedalled passage sums well above one note
	float VelocityCurve = 1.f;  // key touch: < 1 lighter, > 1 heavier
	bool bPedalInverted = false;
	bool bExpressive = true;    // play the score with its dynamics, voicing and articulation
	bool bScorePedal = true;    // and its pedal marks
	bool bMetronome = false;    // the light click on every beat (the transport's metronome)
	bool bVSync = true;

	void Load();
	void Save() const;
	// Everything but the instrument path (that one reloads the sound: ApplyInstrument).
	void Apply(UPianoPlayer* Player) const;
	void ApplyInstrument(UPianoPlayer* Player) const;
	void ApplyVSync() const;
};
