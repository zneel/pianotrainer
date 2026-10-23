#include "PianoKitLog.h"

#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "MetasoundFrontendModuleRegistrationMacros.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#include "SfizzApi.h"
#include "VerovioApi.h"
#include "lunasvg.h"

DEFINE_LOG_CATEGORY(LogPianoKit);

class FPianoKitModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Verovio is loaded from the plugin's ThirdParty folder (staged next to it in packaged builds).
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PianoKit"));
		const FString Base = FPaths::ConvertRelativePathToFull(Plugin ? Plugin->GetBaseDir() : FPaths::ProjectPluginsDir() / TEXT("PianoKit"));
#if PLATFORM_WINDOWS
		const FString LibPath = Base / TEXT("ThirdParty/Verovio/Win64/verovio.dll");
#elif PLATFORM_MAC
		const FString LibPath = Base / TEXT("ThirdParty/Verovio/Mac/libverovio.dylib");
#else
		const FString LibPath;
#endif
		VerovioHandle = LibPath.IsEmpty() ? nullptr : FPlatformProcess::GetDllHandle(*LibPath);
		if (VerovioHandle && FVerovioApi::Get().Load(VerovioHandle))
		{
			FVerovioApi::Get().enableLogToBuffer(true); // keep Verovio's diagnostics out of stdout; read per toolkit with getLog
			UE_LOG(LogPianoKit, Log, TEXT("Verovio loaded from %s"), *LibPath);
		}
		else
		{
			UE_LOG(LogPianoKit, Error, TEXT("Could not load Verovio from %s"), *LibPath);
		}

		// sfizz plays the piano (PianoKit Sfizz node); same loading scheme.
#if PLATFORM_WINDOWS
		const FString SfizzPath = Base / TEXT("ThirdParty/Sfizz/Win64/sfizz.dll");
#elif PLATFORM_MAC
		const FString SfizzPath = Base / TEXT("ThirdParty/Sfizz/Mac/libsfizz.dylib");
#else
		const FString SfizzPath;
#endif
		SfizzHandle = SfizzPath.IsEmpty() ? nullptr : FPlatformProcess::GetDllHandle(*SfizzPath);
		if (SfizzHandle && FSfizzApi::Get().Load(SfizzHandle))
		{
			UE_LOG(LogPianoKit, Log, TEXT("sfizz loaded from %s"), *SfizzPath);
		}
		else
		{
			UE_LOG(LogPianoKit, Error, TEXT("Could not load sfizz from %s"), *SfizzPath);
		}

		// Verovio draws most symbols as paths, but text such as tempo marks uses SMuFL characters in the
		// "Leipzig" font family: give lunasvg the font so those don't render as empty boxes.
		const FString LeipzigPath = Base / TEXT("ThirdParty/Verovio/fonts/Leipzig.ttf");
		if (!lunasvg_add_font_face_from_file("Leipzig", false, false, TCHAR_TO_UTF8(*LeipzigPath)))
		{
			UE_LOG(LogPianoKit, Warning, TEXT("Could not register music font %s"), *LeipzigPath);
		}

		METASOUND_REGISTER_ITEMS_IN_MODULE
	}

	virtual void ShutdownModule() override
	{
		METASOUND_UNREGISTER_ITEMS_IN_MODULE
		if (VerovioHandle)
		{
			FPlatformProcess::FreeDllHandle(VerovioHandle);
			VerovioHandle = nullptr;
		}
		if (SfizzHandle)
		{
			FPlatformProcess::FreeDllHandle(SfizzHandle);
			SfizzHandle = nullptr;
		}
	}

private:
	void* VerovioHandle = nullptr;
	void* SfizzHandle = nullptr;
};

METASOUND_IMPLEMENT_MODULE_REGISTRATION_LIST
IMPLEMENT_MODULE(FPianoKitModule, PianoKit)
