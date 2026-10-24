#include "PianoVst3Plugin.h"

#include "PianoVst3Window.h"

#include "Algo/StableSort.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

THIRD_PARTY_INCLUDES_START
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <string>
#include <vector>
THIRD_PARTY_INCLUDES_END

IMPLEMENT_MODULE(FDefaultModuleImpl, PianoVst3)

DEFINE_LOG_CATEGORY_STATIC(LogPianoVst3, Log, All);

using namespace Steinberg;

namespace PianoVst3Impl
{
	static constexpr int32 MaxEvents = 2048;          // per block
	static constexpr int32 MaxParameterChanges = 256; // parameters changed per block
	static constexpr uint32 StateMagic = 0x33565450;  // "PTV3"

	// the app as the plug-ins' host: its name, and the messages and attribute lists plug-ins ask it for
	static void EnsureHost()
	{
		static Vst::HostApplication* const Host = []()
		{
			Vst::HostApplication* App = new Vst::HostApplication();
			Vst::PluginContextFactory::instance().setPluginContext(App);
			return App;
		}();
	}

	// Edits made in the plug-in's window, passed on to its processor (when the two are separate objects).
	class FComponentHandler final : public Vst::IComponentHandler
	{
	public:
		Vst::ParameterChangeTransfer Changes{MaxParameterChanges};

		virtual tresult PLUGIN_API beginEdit(Vst::ParamID) override { return kResultOk; }
		virtual tresult PLUGIN_API performEdit(Vst::ParamID Id, Vst::ParamValue Value) override
		{
			Changes.addChange(Id, Value, 0);
			return kResultOk;
		}
		virtual tresult PLUGIN_API endEdit(Vst::ParamID) override { return kResultOk; }
		virtual tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
		virtual tresult PLUGIN_API queryInterface(const TUID Iid, void** Obj) override
		{
			QUERY_INTERFACE(Iid, Obj, FUnknown::iid, Vst::IComponentHandler)
			QUERY_INTERFACE(Iid, Obj, Vst::IComponentHandler::iid, Vst::IComponentHandler)
			*Obj = nullptr;
			return kNoInterface;
		}
		virtual uint32 PLUGIN_API addRef() override { return 1; } // lives as long as the plug-in
		virtual uint32 PLUGIN_API release() override { return 1; }
	};

	static void AppendUint32(TArray<uint8>& Out, uint32 Value)
	{
		Out.Append(reinterpret_cast<const uint8*>(&Value), sizeof(Value));
	}

	static void AppendStream(TArray<uint8>& Out, MemoryStream& Stream)
	{
		const uint32 Size = uint32(Stream.getSize());
		AppendUint32(Out, Size);
		Out.Append(reinterpret_cast<const uint8*>(Stream.getData()), Size);
	}

	static bool ReadBlock(const TArray<uint8>& In, int32& Offset, TArray<uint8>& Out)
	{
		uint32 Size = 0;
		if (Offset + int32(sizeof(Size)) > In.Num())
		{
			return false;
		}
		FMemory::Memcpy(&Size, In.GetData() + Offset, sizeof(Size));
		Offset += sizeof(Size);
		if (Size > uint32(In.Num() - Offset))
		{
			return false;
		}
		Out = TArray<uint8>(In.GetData() + Offset, int32(Size));
		Offset += int32(Size);
		return true;
	}
}

struct FPianoVst3Plugin::FImpl
{
	VST3::Hosting::Module::Ptr Module;
	IPtr<Vst::PlugProvider> Provider;
	IPtr<Vst::IComponent> Component;
	IPtr<Vst::IAudioProcessor> Processor;
	IPtr<Vst::IEditController> Controller;
	PianoVst3Impl::FComponentHandler Handler;

	// the audio thread's
	Vst::HostProcessData Data;
	Vst::ProcessContext Context = {};
	Vst::EventList Events{PianoVst3Impl::MaxEvents};
	// a block's events as they come (releases first, from the sequencer), sorted by time before the plug-in sees them:
	// plug-ins read them in order (mda Piano ran its frame counter backwards over unsorted ones)
	TArray<Vst::Event> Pending;
	Vst::ParameterChanges InParams{PianoVst3Impl::MaxParameterChanges};
	Vst::ParameterChanges OutParams{PianoVst3Impl::MaxParameterChanges};
	Vst::ParamID CcParams[128];
	bool Held[128] = {};
	// each note gets its own id, as a DAW gives them: some plug-ins match a release to its note by id alone (mda Piano
	// released every voice on any note-off when all were -1, "no id")
	int32 NoteIds[128] = {};
	int32 NextNoteId = 1;
	int32 MaxFrames = 0;
	bool bProcessing = false;

	// the main thread's
	IPtr<IPlugView> View;
	TUniquePtr<FPianoVst3Window> Window;
};

TArray<FPianoVst3Plugin::FFound> FPianoVst3Plugin::Find(const TArray<FString>& ExtraFolders)
{
	TArray<FString> Paths;
	try
	{
		for (const std::string& Path : VST3::Hosting::Module::getModulePaths())
		{
			Paths.Add(UTF8_TO_TCHAR(Path.c_str()));
		}
	}
	catch (...)
	{
		UE_LOG(LogPianoVst3, Warning, TEXT("Could not list the system's VST 3 folders"));
	}
	for (const FString& Folder : ExtraFolders)
	{
		TArray<FString> InFolder;
		IFileManager::Get().FindFilesRecursive(InFolder, *Folder, TEXT("*.vst3"), /*Files*/ true, /*Directories*/ true);
		Paths.Append(InFolder);
	}
	// a bundle is a folder whose binary inside is also called .vst3: keep the outermost
	for (FString& Path : Paths)
	{
		FPaths::NormalizeFilename(Path);
	}
	Paths.Sort();
	TArray<FFound> Out;
	for (const FString& Path : Paths)
	{
		const bool bInside = Out.ContainsByPredicate([&Path](const FFound& Found) { return Path == Found.Path || Path.StartsWith(Found.Path + TEXT("/")); });
		if (!bInside)
		{
			Out.Add({ Path, FPaths::GetBaseFilename(Path) });
		}
	}
	Out.Sort([](const FFound& A, const FFound& B) { return A.Name < B.Name; });
	return Out;
}

TSharedPtr<FPianoVst3Plugin, ESPMode::ThreadSafe> FPianoVst3Plugin::Load(const FString& InPath, float InSampleRate, int32 InMaxFrames, FString& OutError)
{
	using namespace PianoVst3Impl;
	TSharedPtr<FPianoVst3Plugin, ESPMode::ThreadSafe> Plugin(new FPianoVst3Plugin());
	Plugin->Path = InPath;
	Plugin->SampleRate = InSampleRate;
	Plugin->Impl = MakeUnique<FImpl>();
	FImpl& I = *Plugin->Impl;
	bool bLoaded = false;
	PianoVst3RunOnMainThread([&]()
	{
		EnsureHost();
		std::string Error;
		I.Module = VST3::Hosting::Module::create(TCHAR_TO_UTF8(*InPath), Error);
		if (!I.Module)
		{
			OutError = FString::Printf(TEXT("not a VST 3 plug-in (%s)"), UTF8_TO_TCHAR(Error.c_str()));
			return;
		}
		// the bundle's instrument, else its first audio processor
		const VST3::Hosting::PluginFactory& Factory = I.Module->getFactory();
		const std::vector<VST3::Hosting::ClassInfo> Classes = Factory.classInfos();
		const VST3::Hosting::ClassInfo* Chosen = nullptr;
		for (const VST3::Hosting::ClassInfo& Class : Classes)
		{
			if (Class.category() != kVstAudioEffectClass)
			{
				continue;
			}
			if (!Chosen || Class.subCategoriesString().find("Instrument") != std::string::npos)
			{
				const bool bInstrument = Class.subCategoriesString().find("Instrument") != std::string::npos;
				Chosen = &Class;
				if (bInstrument)
				{
					break;
				}
			}
		}
		if (!Chosen)
		{
			OutError = TEXT("the bundle has no instrument");
			return;
		}
		Plugin->Name = UTF8_TO_TCHAR(Chosen->name().c_str());
		Plugin->Vendor = UTF8_TO_TCHAR(Chosen->vendor().c_str());
		I.Provider = owned(new Vst::PlugProvider(Factory, *Chosen, true));
		if (!I.Provider->initialize())
		{
			OutError = TEXT("the plug-in did not start");
			return;
		}
		I.Component = I.Provider->getComponentPtr();
		I.Controller = I.Provider->getControllerPtr();
		I.Processor = U::cast<Vst::IAudioProcessor>(I.Component);
		if (!I.Component || !I.Processor)
		{
			OutError = TEXT("the plug-in has no audio processor");
			return;
		}

		for (Vst::ParamID& Id : I.CcParams)
		{
			Id = Vst::kNoParamId;
		}
		if (I.Controller)
		{
			I.Controller->setComponentHandler(&I.Handler);
			// the controller starts from the processor's state
			MemoryStream State;
			if (I.Component->getState(&State) == kResultOk)
			{
				State.seek(0, IBStream::kIBSeekSet, nullptr);
				I.Controller->setComponentState(&State);
			}
			// MIDI controllers (the sustain pedal...) are parameters in VST 3: the plug-in says which
			if (FUnknownPtr<Vst::IMidiMapping> Mapping{I.Controller})
			{
				for (int32 Cc = 0; Cc < 128; ++Cc)
				{
					Mapping->getMidiControllerAssignment(0, 0, Vst::CtrlNumber(Cc), I.CcParams[Cc]);
				}
			}
		}

		// the main note input and the main output, in stereo; the other buses off
		const int32 AudioIns = I.Component->getBusCount(Vst::kAudio, Vst::kInput);
		const int32 AudioOuts = I.Component->getBusCount(Vst::kAudio, Vst::kOutput);
		if (AudioOuts < 1)
		{
			OutError = TEXT("the plug-in has no audio output");
			return;
		}
		std::vector<Vst::SpeakerArrangement> InArrangements(AudioIns), OutArrangements(AudioOuts);
		for (int32 Bus = 0; Bus < AudioIns; ++Bus)
		{
			I.Processor->getBusArrangement(Vst::kInput, Bus, InArrangements[Bus]);
		}
		for (int32 Bus = 0; Bus < AudioOuts; ++Bus)
		{
			I.Processor->getBusArrangement(Vst::kOutput, Bus, OutArrangements[Bus]);
		}
		OutArrangements[0] = Vst::SpeakerArr::kStereo;
		I.Processor->setBusArrangements(InArrangements.data(), AudioIns, OutArrangements.data(), AudioOuts); // may keep its own
		for (int32 Bus = 0; Bus < I.Component->getBusCount(Vst::kEvent, Vst::kInput); ++Bus)
		{
			I.Component->activateBus(Vst::kEvent, Vst::kInput, Bus, Bus == 0);
		}
		for (int32 Bus = 0; Bus < AudioOuts; ++Bus)
		{
			I.Component->activateBus(Vst::kAudio, Vst::kOutput, Bus, Bus == 0);
		}
		for (int32 Bus = 0; Bus < AudioIns; ++Bus)
		{
			I.Component->activateBus(Vst::kAudio, Vst::kInput, Bus, false);
		}

		I.MaxFrames = FMath::Max(InMaxFrames, 64);
		I.Pending.Reserve(MaxEvents); // no allocation on the audio thread
		Vst::ProcessSetup Setup = { Vst::kRealtime, Vst::kSample32, I.MaxFrames, InSampleRate };
		if (I.Processor->setupProcessing(Setup) != kResultOk)
		{
			OutError = FString::Printf(TEXT("the plug-in cannot play at %.0f Hz"), InSampleRate);
			return;
		}
		if (I.Component->setActive(true) != kResultOk)
		{
			OutError = TEXT("the plug-in did not switch on");
			return;
		}
		I.Data.prepare(*I.Component, I.MaxFrames, Vst::kSample32); // it owns a buffer for every channel
		I.Data.inputEvents = &I.Events;
		I.Data.inputParameterChanges = &I.InParams;
		I.Data.outputParameterChanges = &I.OutParams;
		I.Data.processContext = &I.Context;
		I.Context.sampleRate = InSampleRate;
		I.Context.tempo = 120.0;
		I.Context.timeSigNumerator = 4;
		I.Context.timeSigDenominator = 4;
		I.Context.state = Vst::ProcessContext::kTempoValid | Vst::ProcessContext::kTimeSigValid | Vst::ProcessContext::kContTimeValid;
		I.Processor->setProcessing(true); // many answer "not implemented"
		I.bProcessing = true;
		bLoaded = true;
	});
	if (!bLoaded)
	{
		UE_LOG(LogPianoVst3, Warning, TEXT("VST 3 plug-in %s: %s"), *InPath, *OutError);
		return nullptr;
	}
	UE_LOG(LogPianoVst3, Log, TEXT("VST 3 plug-in %s (%s): %s, %.0f Hz, up to %d frames a block, editor %s, sustain pedal %s"),
		*Plugin->Name, *Plugin->Vendor, *InPath, InSampleRate, I.MaxFrames, Plugin->HasEditor() ? TEXT("yes") : TEXT("no"),
		I.CcParams[64] != Vst::kNoParamId ? TEXT("mapped") : TEXT("not mapped"));
	return Plugin;
}

FPianoVst3Plugin::~FPianoVst3Plugin()
{
	if (!Impl)
	{
		return;
	}
	check(IsInGameThread()); // FPianoSynth hands its plug-in back to the game thread
	FImpl& I = *Impl;
	PianoVst3RunOnMainThread([&I]()
	{
		I.Window.Reset(); // detaches the view
		I.View = nullptr;
		if (I.bProcessing)
		{
			I.bProcessing = false;
			I.Processor->setProcessing(false);
			I.Component->setActive(false);
		}
		I.Data.unprepare();
		if (I.Controller)
		{
			I.Controller->setComponentHandler(nullptr);
		}
		I.Controller = nullptr;
		I.Processor = nullptr;
		I.Component = nullptr;
		I.Provider = nullptr; // terminates and releases the plug-in
		I.Module.reset();     // then unloads its library
	});
}

bool FPianoVst3Plugin::HasEditor() const
{
	return Impl && Impl->Controller;
}

bool FPianoVst3Plugin::OpenEditor(void* ParentWindowHandle)
{
	if (!HasEditor())
	{
		return false;
	}
	FImpl& I = *Impl;
	bool bOpen = false;
	PianoVst3RunOnMainThread([&]()
	{
		if (I.Window && I.Window->IsOpen())
		{
			I.Window->BringToFront();
			bOpen = true;
			return;
		}
		I.Window.Reset();
		I.View = owned(I.Controller->createView(Vst::ViewType::kEditor));
		if (!I.View)
		{
			return;
		}
		const TWeakPtr<FPianoVst3Plugin, ESPMode::ThreadSafe> Weak = AsShared();
		I.Window = FPianoVst3Window::Open(I.View, ParentWindowHandle, Name, [Weak]()
		{
			// once the window's own message is handled: drop it and its view, then tell the app
			AsyncTask(ENamedThreads::GameThread, [Weak]()
			{
				if (const TSharedPtr<FPianoVst3Plugin, ESPMode::ThreadSafe> Plugin = Weak.Pin())
				{
					PianoVst3RunOnMainThread([&Plugin]()
					{
						if (Plugin->Impl->Window && !Plugin->Impl->Window->IsOpen())
						{
							Plugin->Impl->Window.Reset();
							Plugin->Impl->View = nullptr;
						}
					});
					if (Plugin->OnEditorClosed)
					{
						Plugin->OnEditorClosed();
					}
				}
			});
		});
		bOpen = I.Window.IsValid();
		if (!bOpen)
		{
			I.View = nullptr;
		}
	});
	if (!bOpen)
	{
		UE_LOG(LogPianoVst3, Warning, TEXT("%s has no window that can be shown here"), *Name);
	}
	return bOpen;
}

void FPianoVst3Plugin::CloseEditor()
{
	if (Impl && Impl->Window)
	{
		Impl->Window->Close();
	}
}

bool FPianoVst3Plugin::IsEditorOpen() const
{
	return Impl && Impl->Window && Impl->Window->IsOpen();
}

bool FPianoVst3Plugin::GetState(TArray<uint8>& OutState) const
{
	using namespace PianoVst3Impl;
	if (!Impl)
	{
		return false;
	}
	bool bOk = false;
	PianoVst3RunOnMainThread([&]()
	{
		MemoryStream ComponentState, ControllerState;
		if (Impl->Component->getState(&ComponentState) != kResultOk)
		{
			return;
		}
		if (Impl->Controller)
		{
			Impl->Controller->getState(&ControllerState);
		}
		OutState.Reset();
		AppendUint32(OutState, StateMagic);
		AppendStream(OutState, ComponentState);
		AppendStream(OutState, ControllerState);
		bOk = true;
	});
	return bOk;
}

bool FPianoVst3Plugin::SetState(const TArray<uint8>& State)
{
	using namespace PianoVst3Impl;
	uint32 Magic = 0;
	if (!Impl || State.Num() < int32(sizeof(Magic)))
	{
		return false;
	}
	FMemory::Memcpy(&Magic, State.GetData(), sizeof(Magic));
	int32 Offset = sizeof(Magic);
	TArray<uint8> ComponentBytes, ControllerBytes;
	if (Magic != StateMagic || !ReadBlock(State, Offset, ComponentBytes) || !ReadBlock(State, Offset, ControllerBytes))
	{
		return false;
	}
	bool bOk = false;
	PianoVst3RunOnMainThread([&]()
	{
		MemoryStream ComponentState(ComponentBytes.GetData(), ComponentBytes.Num());
		bOk = Impl->Component->setState(&ComponentState) == kResultOk;
		if (bOk && Impl->Controller)
		{
			ComponentState.seek(0, IBStream::kIBSeekSet, nullptr);
			Impl->Controller->setComponentState(&ComponentState);
			if (ControllerBytes.Num() > 0)
			{
				MemoryStream ControllerState(ControllerBytes.GetData(), ControllerBytes.Num());
				Impl->Controller->setState(&ControllerState);
			}
		}
	});
	return bOk;
}

void FPianoVst3Plugin::NoteOn(int32 Frame, int32 Pitch, int32 Velocity)
{
	if (Velocity <= 0)
	{
		NoteOff(Frame, Pitch);
		return;
	}
	Vst::Event Event = {};
	Event.busIndex = 0;
	Event.sampleOffset = Frame;
	Event.flags = Vst::Event::kIsLive;
	Event.type = Vst::Event::kNoteOnEvent;
	Event.noteOn.channel = 0;
	Event.noteOn.pitch = int16(Pitch & 127);
	Event.noteOn.velocity = float(FMath::Clamp(Velocity, 1, 127)) / 127.f;
	Event.noteOn.noteId = Impl->NextNoteId;
	Impl->NextNoteId = Impl->NextNoteId == TNumericLimits<int32>::Max() ? 1 : Impl->NextNoteId + 1;
	Impl->NoteIds[Pitch & 127] = Event.noteOn.noteId;
	Impl->Pending.Add(Event);
	Impl->Held[Pitch & 127] = true;
}

void FPianoVst3Plugin::NoteOff(int32 Frame, int32 Pitch)
{
	Vst::Event Event = {};
	Event.busIndex = 0;
	Event.sampleOffset = Frame;
	Event.flags = Vst::Event::kIsLive;
	Event.type = Vst::Event::kNoteOffEvent;
	Event.noteOff.channel = 0;
	Event.noteOff.pitch = int16(Pitch & 127);
	Event.noteOff.noteId = Impl->Held[Pitch & 127] ? Impl->NoteIds[Pitch & 127] : -1;
	Impl->Pending.Add(Event);
	Impl->Held[Pitch & 127] = false;
}

void FPianoVst3Plugin::Control(int32 Frame, int32 Cc, int32 Value)
{
	const Vst::ParamID Id = Cc >= 0 && Cc < 128 ? Impl->CcParams[Cc] : Vst::kNoParamId;
	if (Id == Vst::kNoParamId)
	{
		return;
	}
	int32 Index = 0;
	if (Vst::IParamValueQueue* Queue = Impl->InParams.addParameterData(Id, Index))
	{
		Queue->addPoint(Frame, Vst::ParamValue(FMath::Clamp(Value, 0, 127)) / 127.0, Index);
	}
}

void FPianoVst3Plugin::AllNotesOff(int32 Frame)
{
	for (int32 Pitch = 0; Pitch < 128; ++Pitch)
	{
		if (Impl->Held[Pitch])
		{
			NoteOff(Frame, Pitch);
		}
	}
	Control(Frame, 64, 0);
}

void FPianoVst3Plugin::Render(float* Left, float* Right, int32 Frames)
{
	FImpl& I = *Impl;
	if (!I.bProcessing || I.Data.numOutputs < 1)
	{
		FMemory::Memzero(Left, Frames * sizeof(float));
		FMemory::Memzero(Right, Frames * sizeof(float));
		return;
	}
	I.Handler.Changes.transferChangesTo(I.InParams); // edits from its window
	Algo::StableSortBy(I.Pending, [](const Vst::Event& Event) { return Event.sampleOffset * 2 + (Event.type == Vst::Event::kNoteOnEvent ? 1 : 0); });
	for (Vst::Event& Event : I.Pending)
	{
		Event.sampleOffset = FMath::Clamp(Event.sampleOffset, 0, FMath::Min(Frames, I.MaxFrames) - 1);
		I.Events.addEvent(Event);
	}
	I.Pending.Reset();
	for (int32 Done = 0; Done < Frames;)
	{
		// a block longer than the plug-in was set up for goes in pieces; the events all fall in the first
		const int32 Count = FMath::Min(Frames - Done, I.MaxFrames);
		I.Data.numSamples = Count;
		const bool bOk = I.Processor->process(I.Data) == kResultOk;
		const Vst::AudioBusBuffers& Out = I.Data.outputs[0];
		if (bOk && Out.numChannels > 0)
		{
			FMemory::Memcpy(Left + Done, Out.channelBuffers32[0], Count * sizeof(float));
			FMemory::Memcpy(Right + Done, Out.channelBuffers32[Out.numChannels > 1 ? 1 : 0], Count * sizeof(float));
		}
		else
		{
			FMemory::Memzero(Left + Done, Count * sizeof(float));
			FMemory::Memzero(Right + Done, Count * sizeof(float));
		}
		I.Events.clear();
		I.InParams.clearQueue();
		I.OutParams.clearQueue();
		I.Context.continousTimeSamples += Count;
		I.Context.projectTimeSamples += Count;
		Done += Count;
	}
}
