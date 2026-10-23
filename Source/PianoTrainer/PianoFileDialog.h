#pragma once

#include "CoreMinimal.h"

// Native "open file" dialog for scores (Win32 on Windows, NSOpenPanel on macOS).
namespace PianoFileDialog
{
	bool OpenScore(FString& OutPath);
}
