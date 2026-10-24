// The plug-in editor in an NSWindow; plug-in calls run on the main (Cocoa) thread, which is not the game thread on a
// Mac. Compiled with ARC (PianoVst3.Build.cs).

#include "PianoVst3Window.h"

#include "Mac/CocoaThread.h"

THIRD_PARTY_INCLUDES_START
#include "pluginterfaces/gui/iplugview.h"
THIRD_PARTY_INCLUDES_END

#import <Cocoa/Cocoa.h>

using namespace Steinberg;

void PianoVst3RunOnMainThread(TFunctionRef<void()> Work)
{
	if ([NSThread isMainThread])
	{
		Work();
	}
	else
	{
		MainThreadCall(^{ Work(); }, NSDefaultRunLoopMode, true);
	}
}

class FPianoVst3WindowMac;

@interface FPianoVst3WindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) FPianoVst3WindowMac* Owner;
@end

class FPianoVst3WindowMac final : public FPianoVst3Window, public IPlugFrame
{
public:
	FPianoVst3WindowMac(IPlugView* InView, TFunction<void()> InOnClosed)
		: View(InView)
		, OnClosed(MoveTemp(InOnClosed))
	{
	}

	virtual ~FPianoVst3WindowMac() override
	{
		Close();
	}

	bool Create(const FString& Title)
	{
		if (View->isPlatformTypeSupported(kPlatformTypeNSView) != kResultTrue)
		{
			return false;
		}
		ViewRect Size(0, 0, 800, 600);
		View->getSize(&Size);
		NSWindowStyleMask Style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
		if (View->canResize() == kResultTrue)
		{
			Style |= NSWindowStyleMaskResizable;
		}
		Window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, Size.getWidth(), Size.getHeight()) styleMask:Style
			backing:NSBackingStoreBuffered defer:NO];
		Window.releasedWhenClosed = NO;
		Window.title = Title.GetNSString();
		Delegate = [FPianoVst3WindowDelegate new];
		Delegate.Owner = this;
		Window.delegate = Delegate;
		View->setFrame(this);
		if (View->attached((__bridge void*)Window.contentView, kPlatformTypeNSView) != kResultOk)
		{
			View->setFrame(nullptr);
			Window.delegate = nil;
			Window = nil;
			return false;
		}
		bAttached = true;
		[Window center];
		[Window makeKeyAndOrderFront:nil];
		return true;
	}

	virtual bool IsOpen() const override { return Window != nil; }

	virtual void BringToFront() override
	{
		PianoVst3RunOnMainThread([this]() { [Window makeKeyAndOrderFront:nil]; });
	}

	virtual void Close() override
	{
		PianoVst3RunOnMainThread([this]() { Detach(); [Window close]; Forget(); });
	}

	// the window is closing (its close button, or Close): detach the view first
	void Detach()
	{
		if (bAttached)
		{
			bAttached = false;
			View->removed();
			View->setFrame(nullptr);
		}
	}

	void Forget()
	{
		if (Window)
		{
			Window.delegate = nil;
			Window = nil;
			Delegate = nil;
			if (OnClosed)
			{
				OnClosed();
			}
		}
	}

	virtual tresult PLUGIN_API resizeView(IPlugView* InView, ViewRect* NewSize) override
	{
		if (!Window || !NewSize)
		{
			return kInvalidArgument;
		}
		[Window setContentSize:NSMakeSize(NewSize->getWidth(), NewSize->getHeight())];
		InView->onSize(NewSize);
		return kResultTrue;
	}
	virtual tresult PLUGIN_API queryInterface(const TUID Iid, void** Obj) override
	{
		QUERY_INTERFACE(Iid, Obj, FUnknown::iid, IPlugFrame)
		QUERY_INTERFACE(Iid, Obj, IPlugFrame::iid, IPlugFrame)
		*Obj = nullptr;
		return kNoInterface;
	}
	virtual uint32 PLUGIN_API addRef() override { return 1; }
	virtual uint32 PLUGIN_API release() override { return 1; }

private:
	IPlugView* View = nullptr;
	TFunction<void()> OnClosed;
	NSWindow* Window = nil;
	FPianoVst3WindowDelegate* Delegate = nil;
	bool bAttached = false;
};

@implementation FPianoVst3WindowDelegate
- (void)windowWillClose:(NSNotification*)Notification
{
	if (FPianoVst3WindowMac* Owner = self.Owner)
	{
		Owner->Detach();
		Owner->Forget();
	}
}
@end

TUniquePtr<FPianoVst3Window> FPianoVst3Window::Open(IPlugView* View, void* ParentWindowHandle, const FString& Title, TFunction<void()> OnClosed)
{
	if (!View)
	{
		return nullptr;
	}
	TUniquePtr<FPianoVst3WindowMac> Window = MakeUnique<FPianoVst3WindowMac>(View, MoveTemp(OnClosed));
	bool bCreated = false;
	PianoVst3RunOnMainThread([&]() { bCreated = Window->Create(Title); });
	if (!bCreated)
	{
		return nullptr;
	}
	return Window;
}
