// Verovio's C API (tools/c_wrapper.h), resolved at runtime from verovio.dll / libverovio.dylib.
// No import library, delay-load or rpath setup is needed, so Windows and macOS load it the same way.
#pragma once

#include "CoreMinimal.h"

struct FVerovioApi
{
	void* (*constructorResourcePath)(const char* ResourcePath) = nullptr;
	void (*destructor)(void* Toolkit) = nullptr;
	bool (*setOptions)(void* Toolkit, const char* Options) = nullptr;
	bool (*loadData)(void* Toolkit, const char* Data) = nullptr;
	bool (*loadZipDataBuffer)(void* Toolkit, const unsigned char* Data, int Length) = nullptr;
	const char* (*getLog)(void* Toolkit) = nullptr;
	const char* (*getVersion)(void* Toolkit) = nullptr;
	const char* (*renderToSVG)(void* Toolkit, int PageNo, bool XmlDeclaration) = nullptr;
	const char* (*renderToTimemap)(void* Toolkit, const char* Options) = nullptr;
	const char* (*getMIDIValuesForElement)(void* Toolkit, const char* XmlId) = nullptr;
	const char* (*getMEI)(void* Toolkit, const char* Options) = nullptr;
	void (*enableLogToBuffer)(bool Value) = nullptr;

	// Resolves every entry point; returns false (and logs the missing name) if any is absent.
	bool Load(void* DllHandle);
	bool IsLoaded() const { return bLoaded; }

	static FVerovioApi& Get();

private:
	bool bLoaded = false;
};
