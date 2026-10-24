#include "PianoFileDialog.h"

#include "Misc/Paths.h"

#if PLATFORM_WINDOWS

#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "Widgets/SWindow.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <commdlg.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace PianoFileDialogImpl
{
	static bool Open(const WCHAR* Filter, const WCHAR* Title, FString& OutPath);
}

bool PianoFileDialog::OpenScore(FString& OutPath)
{
	return PianoFileDialogImpl::Open(L"Scores (MusicXML, MXL, MEI, MuseScore)\0*.musicxml;*.xml;*.mxl;*.mei;*.krn;*.abc;*.mscz;*.mscx\0"
		L"All files\0*.*\0", L"Open a score", OutPath);
}

bool PianoFileDialog::OpenInstrument(FString& OutPath)
{
	if (!PianoFileDialogImpl::Open(L"Instruments (SFZ, VST 3)\0*.sfz;*.vst3\0All files\0*.*\0", L"Choose an instrument (.sfz or .vst3)", OutPath))
	{
		return false;
	}
	// a VST 3 bundle is a folder (X.vst3/Contents/x86_64-win/X.vst3): the dialog can only pick the binary in it
	const int32 Contents = OutPath.Find(TEXT(".vst3/Contents/"), ESearchCase::IgnoreCase);
	if (Contents != INDEX_NONE)
	{
		OutPath.LeftInline(Contents + 5);
	}
	return true;
}

bool PianoFileDialogImpl::Open(const WCHAR* Filter, const WCHAR* Title, FString& OutPath)
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

	OPENFILENAMEW Ofn = {};
	Ofn.lStructSize = sizeof(Ofn);
	Ofn.hwndOwner = Owner;
	Ofn.lpstrFilter = Filter;
	Ofn.lpstrFile = FileName;
	Ofn.nMaxFile = UE_ARRAY_COUNT(FileName);
	Ofn.lpstrTitle = Title;
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

bool PianoFileDialog::OpenInstrument(FString& OutPath)
{
	return false;
}

#endif
