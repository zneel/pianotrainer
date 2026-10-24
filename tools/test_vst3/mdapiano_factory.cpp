// mda Piano on its own, as a VST 3 instrument to test the app's plug-in host with (tools/build_test_vst3.ps1).
// The instrument is the VST 3 SDK's mda-vst3 sample (MIT, Paul Kellett / Steinberg); this replaces its factory,
// which registers all the mda plug-ins, and gives it a plain editor window (the samples draw theirs with VSTGUI) so
// the host's window can be tested too.

#include "mdaPianoController.h"
#include "mdaPianoProcessor.h"
#include "version.h"

#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/main/pluginfactory_constexpr.h"

#include <cstring>
#include <windows.h>

#define kVersionString FULL_VERSION_STR

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace PianoTest
{
	// A resizable panel with the plug-in's name.
	class TestView : public CPluginView
	{
	public:
		TestView () : CPluginView (&DefaultSize) {}

		tresult PLUGIN_API isPlatformTypeSupported (FIDString Type) SMTG_OVERRIDE
		{
			return std::strcmp (Type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
		}
		tresult PLUGIN_API attached (void* Parent, FIDString Type) SMTG_OVERRIDE
		{
			Panel = CreateWindowExW (0, L"STATIC", L"mda Piano - test window", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
				0, 0, rect.getWidth (), rect.getHeight (), static_cast<HWND> (Parent), nullptr, nullptr, nullptr);
			return CPluginView::attached (Parent, Type);
		}
		tresult PLUGIN_API removed () SMTG_OVERRIDE
		{
			if (Panel)
				DestroyWindow (Panel);
			Panel = nullptr;
			return CPluginView::removed ();
		}
		tresult PLUGIN_API canResize () SMTG_OVERRIDE { return kResultTrue; }
		tresult PLUGIN_API onSize (ViewRect* NewSize) SMTG_OVERRIDE
		{
			CPluginView::onSize (NewSize);
			if (Panel)
				MoveWindow (Panel, 0, 0, NewSize->getWidth (), NewSize->getHeight (), TRUE);
			return kResultTrue;
		}

	private:
		static inline const ViewRect DefaultSize {0, 0, 480, 240};
		HWND Panel = nullptr;
	};

	class TestPianoController : public mda::PianoController
	{
	public:
		static FUnknown* createInstance (void*) { return (IEditController*)new TestPianoController; }
		IPlugView* PLUGIN_API createView (FIDString Name) SMTG_OVERRIDE
		{
			return std::strcmp (Name, ViewType::kEditor) == 0 ? new TestView : nullptr;
		}
	};
}

BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail, 2)

DEF_CLASS (mda::PianoProcessor::uid, PClassInfo::kManyInstances, kVstAudioEffectClass, "mda Piano", Vst::kDistributable,
	Vst::PlugType::kInstrumentSynth, kVersionString, kVstVersionString, mda::PianoProcessor::createInstance, nullptr)

DEF_CLASS (mda::PianoController::uid, PClassInfo::kManyInstances, kVstComponentControllerClass, "mda Piano", Vst::kDistributable,
	"", kVersionString, kVstVersionString, PianoTest::TestPianoController::createInstance, nullptr)

END_FACTORY
