#include "PianoFileDialog.h"

#include "Misc/Paths.h"

#if PLATFORM_WINDOWS

#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "Widgets/SWindow.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <commdlg.h>
#include "Windows/HideWindowsPlatformTypes.h"

bool PianoFileDialog::OpenScore(FString& OutPath)
{
	HWND Owner = nullptr;
	if (FSlateApplication::IsInitialized())
	{
		if (TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow())
		{
			if (Window->GetNativeWindow().IsValid())
			{
				Owner = static_cast<HWND>(Window->GetNativeWindow()->GetOSWindowHandle());
			}
		}
	}

	WCHAR FileName[MAX_PATH * 4] = {};
	static const WCHAR Filter[] =
		L"Scores (MusicXML, MXL, MEI, MuseScore)\0*.musicxml;*.xml;*.mxl;*.mei;*.krn;*.abc;*.mscz;*.mscx\0"
		L"All files\0*.*\0";

	OPENFILENAMEW Ofn = {};
	Ofn.lStructSize = sizeof(Ofn);
	Ofn.hwndOwner = Owner;
	Ofn.lpstrFilter = Filter;
	Ofn.lpstrFile = FileName;
	Ofn.nMaxFile = UE_ARRAY_COUNT(FileName);
	Ofn.lpstrTitle = L"Open a score";
	Ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
	if (!GetOpenFileNameW(&Ofn))
	{
		return false;
	}
	OutPath = FString(FileName);
	FPaths::NormalizeFilename(OutPath);
	return true;
}

#elif !PLATFORM_MAC

bool PianoFileDialog::OpenScore(FString& OutPath)
{
	return false;
}

#endif
