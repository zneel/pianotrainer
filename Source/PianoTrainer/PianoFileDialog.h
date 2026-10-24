#pragma once

#include "CoreMinimal.h"

// Native "open file" dialogs (Win32 on Windows, NSOpenPanel on macOS).
namespace PianoFileDialog
{
	bool OpenScore(FString& OutPath);
	// An instrument: a sample library (.sfz, e.g. Piano in 162) or a VST 3 plug-in (.vst3; picking the binary inside a
	// bundle gives the bundle).
	bool OpenInstrument(FString& OutPath);
}
