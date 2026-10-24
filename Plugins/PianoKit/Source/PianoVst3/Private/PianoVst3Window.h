#pragma once

#include "CoreMinimal.h"

namespace Steinberg
{
	class IPlugView;
}

// Plug-ins expect their controller and window calls on the main thread; on a Mac the game thread is another one.
void PianoVst3RunOnMainThread(TFunctionRef<void()> Work);

// A plug-in editor's native window: on Windows a top-level window owned by the app's, on a Mac an NSWindow.
class FPianoVst3Window
{
public:
	// Attaches the view (it must stay alive while the window is open); null if it cannot be shown here. OnClosed runs
	// on the main thread once the view is detached, whoever closed the window, and must not destroy it right away.
	static TUniquePtr<FPianoVst3Window> Open(Steinberg::IPlugView* View, void* ParentWindowHandle, const FString& Title, TFunction<void()> OnClosed);
	virtual ~FPianoVst3Window() {}
	virtual bool IsOpen() const = 0;
	virtual void BringToFront() = 0;
	virtual void Close() = 0;
};
