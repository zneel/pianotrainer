// The plug-in editor in a top-level window owned by the app's (so it stays in front of it), sized to the view and
// resized when the view asks.

#include "PianoVst3Window.h"

#include "Windows/WindowsHWrapper.h"

THIRD_PARTY_INCLUDES_START
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
THIRD_PARTY_INCLUDES_END

using namespace Steinberg;

void PianoVst3RunOnMainThread(TFunctionRef<void()> Work)
{
	Work(); // the game thread is the main thread on Windows
}

namespace PianoVst3WindowImpl
{
	static const TCHAR* ClassName = TEXT("PianoTrainerPluginWindow");
	static constexpr DWORD FixedStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
}

class FPianoVst3WindowWindows final : public FPianoVst3Window, public IPlugFrame
{
public:
	FPianoVst3WindowWindows(IPlugView* InView, TFunction<void()> InOnClosed)
		: View(InView)
		, OnClosed(MoveTemp(InOnClosed))
	{
	}

	virtual ~FPianoVst3WindowWindows() override
	{
		Close();
	}

	bool Create(HWND Parent, const FString& Title)
	{
		using namespace PianoVst3WindowImpl;
		static const bool bRegistered = []()
		{
			WNDCLASSEXW Class = {};
			Class.cbSize = sizeof(Class);
			Class.lpfnWndProc = &FPianoVst3WindowWindows::WindowProc;
			Class.hInstance = GetModuleHandleW(nullptr);
			Class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
			Class.hbrBackground = HBRUSH(GetStockObject(BLACK_BRUSH));
			Class.lpszClassName = ClassName;
			return RegisterClassExW(&Class) != 0;
		}();
		if (!bRegistered || View->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue)
		{
			return false;
		}
		ViewRect Size(0, 0, 800, 600);
		View->getSize(&Size);
		const DWORD Style = FixedStyle | (View->canResize() == kResultTrue ? WS_THICKFRAME | WS_MAXIMIZEBOX : 0);
		RECT Outer = { 0, 0, Size.getWidth(), Size.getHeight() };
		AdjustWindowRectEx(&Outer, Style, false, 0);
		Hwnd = CreateWindowExW(0, ClassName, *Title, Style, CW_USEDEFAULT, CW_USEDEFAULT, Outer.right - Outer.left, Outer.bottom - Outer.top,
			Parent, nullptr, GetModuleHandleW(nullptr), this);
		if (!Hwnd)
		{
			return false;
		}
		if (FUnknownPtr<IPlugViewContentScaleSupport> Scale{View})
		{
			Scale->setContentScaleFactor(float(GetDpiForWindow(Hwnd)) / 96.f);
		}
		View->setFrame(this);
		if (View->attached(Hwnd, kPlatformTypeHWND) != kResultOk)
		{
			View->setFrame(nullptr);
			SetWindowLongPtrW(Hwnd, GWLP_USERDATA, 0);
			DestroyWindow(Hwnd);
			Hwnd = nullptr;
			return false;
		}
		bAttached = true;
		ShowWindow(Hwnd, SW_SHOWNORMAL);
		return true;
	}

	virtual bool IsOpen() const override { return Hwnd != nullptr; }

	virtual void BringToFront() override
	{
		if (Hwnd)
		{
			ShowWindow(Hwnd, SW_RESTORE);
			SetForegroundWindow(Hwnd);
		}
	}

	virtual void Close() override
	{
		if (bAttached)
		{
			bAttached = false;
			View->removed();
			View->setFrame(nullptr);
		}
		if (Hwnd)
		{
			const HWND Closing = Hwnd;
			Hwnd = nullptr;
			SetWindowLongPtrW(Closing, GWLP_USERDATA, 0);
			DestroyWindow(Closing);
			if (OnClosed)
			{
				OnClosed();
			}
		}
	}

	// IPlugFrame: the view wants another size
	virtual tresult PLUGIN_API resizeView(IPlugView* InView, ViewRect* NewSize) override
	{
		if (!Hwnd || !NewSize)
		{
			return kInvalidArgument;
		}
		RECT Outer = { 0, 0, NewSize->getWidth(), NewSize->getHeight() };
		AdjustWindowRectEx(&Outer, DWORD(GetWindowLongPtrW(Hwnd, GWL_STYLE)), false, 0);
		bResizing = true;
		SetWindowPos(Hwnd, nullptr, 0, 0, Outer.right - Outer.left, Outer.bottom - Outer.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		bResizing = false;
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
	virtual uint32 PLUGIN_API addRef() override { return 1; } // lives as long as the window
	virtual uint32 PLUGIN_API release() override { return 1; }

private:
	static LRESULT CALLBACK WindowProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam)
	{
		if (Message == WM_NCCREATE)
		{
			SetWindowLongPtrW(Hwnd, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(LParam)->lpCreateParams));
		}
		FPianoVst3WindowWindows* Self = reinterpret_cast<FPianoVst3WindowWindows*>(GetWindowLongPtrW(Hwnd, GWLP_USERDATA));
		if (Self)
		{
			switch (Message)
			{
			case WM_SIZE:
				// the user dragged the frame of a resizable view
				if (Self->bAttached && !Self->bResizing && WParam != SIZE_MINIMIZED)
				{
					ViewRect Size(0, 0, LOWORD(LParam), HIWORD(LParam));
					if (Self->View->checkSizeConstraint(&Size) == kResultTrue)
					{
						Self->View->onSize(&Size);
					}
				}
				break;
			case WM_DPICHANGED:
				if (FUnknownPtr<IPlugViewContentScaleSupport> Scale{Self->View})
				{
					Scale->setContentScaleFactor(float(HIWORD(WParam)) / 96.f);
				}
				break;
			case WM_CLOSE:
				Self->Close();
				return 0;
			default:
				break;
			}
		}
		return DefWindowProcW(Hwnd, Message, WParam, LParam);
	}

	IPlugView* View = nullptr;
	TFunction<void()> OnClosed;
	HWND Hwnd = nullptr;
	bool bAttached = false;
	bool bResizing = false;
};

TUniquePtr<FPianoVst3Window> FPianoVst3Window::Open(IPlugView* View, void* ParentWindowHandle, const FString& Title, TFunction<void()> OnClosed)
{
	if (!View)
	{
		return nullptr;
	}
	TUniquePtr<FPianoVst3WindowWindows> Window = MakeUnique<FPianoVst3WindowWindows>(View, MoveTemp(OnClosed));
	if (!Window->Create(static_cast<HWND>(ParentWindowHandle), Title))
	{
		return nullptr;
	}
	return Window;
}
