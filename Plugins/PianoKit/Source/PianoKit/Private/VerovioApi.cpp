#include "VerovioApi.h"

#include "PianoKitLog.h"

#include "HAL/PlatformProcess.h"

FVerovioApi& FVerovioApi::Get()
{
	static FVerovioApi Api;
	return Api;
}

bool FVerovioApi::Load(void* DllHandle)
{
	bLoaded = false;
	if (!DllHandle)
	{
		return false;
	}

	bool bAll = true;
	auto Resolve = [&](auto& Fn, const TCHAR* Name)
	{
		Fn = reinterpret_cast<std::remove_reference_t<decltype(Fn)>>(FPlatformProcess::GetDllExport(DllHandle, Name));
		if (!Fn)
		{
			UE_LOG(LogPianoKit, Error, TEXT("Verovio is missing export %s"), Name);
			bAll = false;
		}
	};
	Resolve(constructorResourcePath, TEXT("vrvToolkit_constructorResourcePath"));
	Resolve(destructor, TEXT("vrvToolkit_destructor"));
	Resolve(setOptions, TEXT("vrvToolkit_setOptions"));
	Resolve(loadData, TEXT("vrvToolkit_loadData"));
	Resolve(loadZipDataBuffer, TEXT("vrvToolkit_loadZipDataBuffer"));
	Resolve(getLog, TEXT("vrvToolkit_getLog"));
	Resolve(getVersion, TEXT("vrvToolkit_getVersion"));
	Resolve(renderToSVG, TEXT("vrvToolkit_renderToSVG"));
	Resolve(renderToTimemap, TEXT("vrvToolkit_renderToTimemap"));
	Resolve(getMIDIValuesForElement, TEXT("vrvToolkit_getMIDIValuesForElement"));
	Resolve(getMEI, TEXT("vrvToolkit_getMEI"));
	Resolve(enableLogToBuffer, TEXT("enableLogToBuffer"));
	bLoaded = bAll;
	return bAll;
}
