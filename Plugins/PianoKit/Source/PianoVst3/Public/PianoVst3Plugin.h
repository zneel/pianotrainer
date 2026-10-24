#pragma once

#include "CoreMinimal.h"

// A VST 3 instrument (a .vst3 bundle) hosted by the app.
//
// Threads: Load, the editor, state and the destructor on the game thread (the main thread, where plug-ins expect
// their user interface); the note events and Render on the audio thread, in block order. A plug-in that is still
// rendering keeps itself alive through the shared pointer the audio node holds.
class PIANOVST3_API FPianoVst3Plugin : public TSharedFromThis<FPianoVst3Plugin, ESPMode::ThreadSafe>
{
public:
	struct FFound
	{
		FString Path; // the .vst3 bundle
		FString Name; // its file name, without loading it
	};
	// .vst3 bundles in the system's plug-in folders (Common Files\VST3 on Windows, Library/Audio/Plug-Ins/VST3 on
	// a Mac) and in the extra folders; nothing is loaded
	static TArray<FFound> Find(const TArray<FString>& ExtraFolders);

	// Loads the bundle's first instrument (or first audio class with a note input) for this rate and block size.
	static TSharedPtr<FPianoVst3Plugin, ESPMode::ThreadSafe> Load(const FString& Path, float SampleRate, int32 MaxFrames, FString& OutError);
	~FPianoVst3Plugin();

	const FString& GetPath() const { return Path; }
	const FString& GetName() const { return Name; }   // the instrument's name, e.g. "ARIA Player"
	const FString& GetVendor() const { return Vendor; }
	float GetSampleRate() const { return SampleRate; }

	// The plug-in's own window, over (and owned by) the app's window. Closing it keeps the plug-in loaded.
	bool HasEditor() const;
	bool OpenEditor(void* ParentWindowHandle);
	void CloseEditor();
	bool IsEditorOpen() const;
	// Called on the game thread when the window closes (e.g. to keep the instrument chosen in it).
	TFunction<void()> OnEditorClosed;

	// Everything the plug-in keeps (the loaded instrument, its settings): restored after Load.
	bool GetState(TArray<uint8>& OutState) const;
	bool SetState(const TArray<uint8>& State);

	// Audio thread: events at their frame in the coming block, then Render.
	void NoteOn(int32 Frame, int32 Pitch, int32 Velocity);
	void NoteOff(int32 Frame, int32 Pitch);
	void Control(int32 Frame, int32 Cc, int32 Value); // MIDI controllers the plug-in maps (sustain pedal, ...)
	void AllNotesOff(int32 Frame);
	void Render(float* Left, float* Right, int32 Frames);

private:
	FPianoVst3Plugin() = default;
	struct FImpl;
	TUniquePtr<FImpl> Impl;
	FString Path, Name, Vendor;
	float SampleRate = 48000.f;
};
