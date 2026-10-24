// "PianoKit Sfizz" MetaSound node: plays the session's instrument, driven by a Harmonix MIDI stream - an SFZ file in
// sfizz (BSD-2, github.com/sfztools/sfizz; normally Piano in 162), or a VST 3 plug-in. The instrument does the voices,
// velocity layers, release and resonance samples and the sustain pedal; events go in at their frame in the block and
// it renders straight into the output buffers.
//
// It replaced our own sampler node, which replaced Harmonix's Fusion Sampler (that one clicked with its 32-voice
// pool and stalled the audio render with more voices).

#include "PianoSession.h"
#include "PianoVst3Plugin.h"
#include "SfizzApi.h"

#include "HarmonixMetasound/DataTypes/MidiStream.h"
#include "HarmonixMidi/MidiMsg.h"

#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"

#include "MetasoundAudioBuffer.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundParamHelper.h"
#include "MetasoundPrimitives.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "PianoKit_SfizzNode"

namespace PianoKit::SfizzNode
{
	using namespace Metasound;
	using namespace HarmonixMetasound;

	namespace Inputs
	{
		METASOUND_PARAM(MidiStream, "MIDI Stream", "Note, pedal and controller events (e.g. from the PianoKit Sequencer).")
		METASOUND_PARAM(Session, "Session", "PianoKit session id; its instrument is what plays.")
	}
	namespace Outputs
	{
		METASOUND_PARAM(AudioLeft, "Audio Left", "Left channel.")
		METASOUND_PARAM(AudioRight, "Audio Right", "Right channel.")
	}

	static constexpr int32 AllNotesOffCC = 123;
	static constexpr uint8 ClickChannel = 9;  // count-in / metronome clicks (the sequencer's drum channel)
	static constexpr int32 ClickAccentPitch = 76;

	class FPianoSfizzOperator final : public TExecutableOperator<FPianoSfizzOperator>
	{
	public:
		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto Init = []() -> FNodeClassMetadata
			{
				FNodeClassMetadata Info;
				Info.ClassName = { TEXT("PianoKit"), TEXT("Sfizz"), TEXT("") };
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = METASOUND_LOCTEXT("Sfizz_DisplayName", "PianoKit Sfizz");
				Info.Description = METASOUND_LOCTEXT("Sfizz_Description", "Plays a PianoKit session's SFZ instrument with sfizz.");
				Info.Author = TEXT("PianoKit");
				Info.PromptIfMissing = METASOUND_LOCTEXT("Sfizz_Missing", "Enable the PianoKit plugin.");
				Info.DefaultInterface = GetVertexInterface();
				Info.CategoryHierarchy = { METASOUND_LOCTEXT("PianoKit_Category", "PianoKit") };
				return Info;
			};
			static const FNodeClassMetadata Info = Init();
			return Info;
		}

		static const FVertexInterface& GetVertexInterface()
		{
			static const FVertexInterface Interface(
				FInputVertexInterface(
					TInputDataVertex<FMidiStream>(METASOUND_GET_PARAM_NAME_AND_METADATA(Inputs::MidiStream)),
					TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(Inputs::Session), 0)),
				FOutputVertexInterface(
					TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(Outputs::AudioLeft)),
					TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(Outputs::AudioRight))));
			return Interface;
		}

		static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults&)
		{
			const FInputVertexInterfaceData& InputData = InParams.InputData;
			return MakeUnique<FPianoSfizzOperator>(InParams,
				InputData.GetOrCreateDefaultDataReadReference<FMidiStream>(METASOUND_GET_PARAM_NAME(Inputs::MidiStream), InParams.OperatorSettings),
				InputData.GetOrCreateDefaultDataReadReference<int32>(METASOUND_GET_PARAM_NAME(Inputs::Session), InParams.OperatorSettings));
		}

		FPianoSfizzOperator(const FBuildOperatorParams& InParams, FMidiStreamReadRef&& InMidi, FInt32ReadRef&& InSession)
			: MidiIn(MoveTemp(InMidi))
			, SessionIdIn(MoveTemp(InSession))
			, OutLeft(FAudioBufferWriteRef::CreateNew(InParams.OperatorSettings))
			, OutRight(FAudioBufferWriteRef::CreateNew(InParams.OperatorSettings))
			, FramesPerBlock(InParams.OperatorSettings.GetNumFramesPerBlock())
			, SampleRate(InParams.OperatorSettings.GetSampleRate())
		{
		}

		virtual void BindInputs(FInputVertexInterfaceData& InVertexData) override
		{
			InVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(Inputs::MidiStream), MidiIn);
			InVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(Inputs::Session), SessionIdIn);
		}

		virtual void BindOutputs(FOutputVertexInterfaceData& InVertexData) override
		{
			InVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(Outputs::AudioLeft), OutLeft);
			InVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(Outputs::AudioRight), OutRight);
		}

		void Reset(const IOperator::FResetParams&)
		{
			if (Synth.IsValid() && Synth->Synth)
			{
				FSfizzApi::Get().all_sound_off(Synth->Synth);
			}
			if (Synth.IsValid() && Synth->Plugin.IsValid())
			{
				Synth->Plugin->AllNotesOff(0);
			}
		}

		void Execute()
		{
			OutLeft->Zero();
			OutRight->Zero();

			SyncSynth();
			if (!Synth.IsValid() || (!Synth->Synth && !Synth->Plugin.IsValid()))
			{
				return;
			}
			const FSfizzApi& Api = FSfizzApi::Get();
			sfizz_synth_t* S = Synth->Synth;
			FPianoVst3Plugin* Plugin = Synth->Plugin.Get();

			// settings changed on the game thread: quality, level, instrument controls (e.g. microphone mix)
			if (Synth->SettingsVersion.load(std::memory_order_acquire) != AppliedSettings)
			{
				FScopeLock Guard(&Synth->SettingsLock);
				AppliedSettings = Synth->SettingsVersion.load(std::memory_order_acquire);
				PluginGain = FMath::Pow(10.f, Synth->VolumeDb / 20.f);
				for (const TPair<int32, float>& Control : Synth->Controls)
				{
					if (Plugin)
					{
						Plugin->Control(0, Control.Key, FMath::RoundToInt32(127.f * FMath::Clamp(Control.Value, 0.f, 1.f)));
					}
					else
					{
						Api.send_hdcc(S, 0, Control.Key, FMath::Clamp(Control.Value, 0.f, 1.f));
					}
				}
				if (S)
				{
					Api.set_sample_quality(S, 0, FMath::Clamp(Synth->Quality, 0, 10));
					Api.set_volume(S, Synth->VolumeDb);
				}
			}

			for (const FMidiStreamEvent& Event : MidiIn->GetEventsInBlock())
			{
				const FMidiMsg& Msg = Event.MidiMessage;
				const int32 Frame = FMath::Clamp(Event.BlockSampleFrameIndex, 0, FramesPerBlock - 1);
				if (bAudioStats && Msg.IsControlChange() && Msg.GetStdData1() == 64)
				{
					(Msg.GetStdData2() >= 64 ? StatPedalDown : StatPedalUp)++;
				}
				if (MidiDump && Msg.IsStd() && Msg.GetStdChannel() != ClickChannel)
				{
					// -mididump: what the instrument is sent, in order: block, frame, status, data1, data2
					const FString Line = FString::Printf(TEXT("%lld,%d,%d,%d,%d\n"), DumpBlock, Frame, Msg.GetStdStatus(), Msg.GetStdData1(), Msg.GetStdData2());
					MidiDump->Serialize(const_cast<ANSICHAR*>(StringCast<ANSICHAR>(*Line).Get()), Line.Len());
				}
				if (bAudioStats && Msg.IsStd() && Msg.GetStdChannel() != ClickChannel)
				{
					StatNoteOns += Msg.IsNoteOn() && Msg.GetStdData2() > 0 ? 1 : 0;
					StatNoteOffs += Msg.IsNoteOff() ? 1 : 0;
				}
				if (Msg.IsStd() && Msg.GetStdChannel() == ClickChannel)
				{
					if (Msg.IsNoteOn())
					{
						StartClick(Frame, Msg.GetStdData1() == ClickAccentPitch);
					}
					continue;
				}
				if (Plugin)
				{
					if (Msg.IsNoteOff())
					{
						Plugin->NoteOff(Frame, Msg.GetStdData1());
					}
					else if (Msg.IsNoteOn())
					{
						Plugin->NoteOn(Frame, Msg.GetStdData1(), Msg.GetStdData2());
					}
					else if (Msg.IsControlChange())
					{
						Plugin->Control(Frame, Msg.GetStdData1(), Msg.GetStdData2());
					}
					else if (Msg.IsAllNotesOff() || Msg.IsAllNotesKill())
					{
						Plugin->AllNotesOff(Frame);
					}
				}
				else if (Msg.IsNoteOff())
				{
					Api.send_note_off(S, Frame, Msg.GetStdData1(), 0);
				}
				else if (Msg.IsNoteOn())
				{
					Api.send_note_on(S, Frame, Msg.GetStdData1(), Msg.GetStdData2());
				}
				else if (Msg.IsControlChange())
				{
					Api.send_cc(S, Frame, Msg.GetStdData1(), Msg.GetStdData2());
				}
				else if (Msg.IsAllNotesOff())
				{
					Api.send_cc(S, Frame, AllNotesOffCC, 0);
				}
				else if (Msg.IsAllNotesKill())
				{
					Api.all_sound_off(S);
				}
			}

			++DumpBlock;
			float* Channels[2] = { OutLeft->GetData(), OutRight->GetData() };
			const uint64 RenderStart = FPlatformTime::Cycles64();
			if (Plugin)
			{
				Plugin->Render(Channels[0], Channels[1], FramesPerBlock);
				for (int32 Frame = 0; Frame < FramesPerBlock && PluginGain != 1.f; ++Frame)
				{
					Channels[0][Frame] *= PluginGain;
					Channels[1][Frame] *= PluginGain;
				}
			}
			else
			{
				Api.render_block(S, Channels, 2, FramesPerBlock);
			}
			RenderClick(Channels[0], Channels[1]);
			Limit(Channels[0], Channels[1]);
			if (bAudioStats)
			{
				CountStats(RenderStart, S ? Api.get_num_active_voices(S) : 0, Channels[0], Channels[1]);
			}
			if (MidiDump && S)
			{
				const FString Line = FString::Printf(TEXT("%lld,-1,%d,0,0\n"), DumpBlock - 1, Api.get_num_active_voices(S)); // voices after the block
				MidiDump->Serialize(const_cast<ANSICHAR*>(StringCast<ANSICHAR>(*Line).Get()), Line.Len());
			}
		}

	private:
		// A loud pedalled passage can add up past full scale, which the device clips (a harsh crackle). Keep the peak
		// under -1 dBFS: the gain drops at once to what the sample needs and comes back over about 150 ms.
		void Limit(float* Left, float* Right)
		{
			static constexpr float Ceiling = 0.89f;
			const float Recover = FMath::Exp(-1.f / (0.15f * SampleRate));
			for (int32 Frame = 0; Frame < FramesPerBlock; ++Frame)
			{
				const float Peak = FMath::Max(FMath::Abs(Left[Frame]), FMath::Abs(Right[Frame]));
				LimiterGain = 1.f - (1.f - LimiterGain) * Recover;
				if (Peak * LimiterGain > Ceiling)
				{
					LimiterGain = Ceiling / Peak;
				}
				Left[Frame] *= LimiterGain;
				Right[Frame] *= LimiterGain;
				StatMinGain = FMath::Min(StatMinGain, LimiterGain);
			}
		}

		// -audiostats: once a second, how long sfizz took per block against the block's real time, the time between
		// blocks (a starved device shows up as long gaps), the voices playing and the output peak
		void CountStats(uint64 RenderStart, int32 Voices, const float* Left, const float* Right)
		{
			const uint64 Now = FPlatformTime::Cycles64();
			const double RenderUs = FPlatformTime::ToMilliseconds64(Now - RenderStart) * 1000.0;
			const double GapUs = LastBlockEnd ? FPlatformTime::ToMilliseconds64(Now - LastBlockEnd) * 1000.0 : 0.0;
			LastBlockEnd = Now;
			StatBlocks++;
			StatSumUs += RenderUs;
			StatMaxUs = FMath::Max(StatMaxUs, RenderUs);
			StatMaxGapUs = FMath::Max(StatMaxGapUs, GapUs);
			StatMaxVoices = FMath::Max(StatMaxVoices, Voices);
			const double BlockUs = 1e6 * FramesPerBlock / SampleRate;
			StatOverHalf += RenderUs > 0.5 * BlockUs ? 1 : 0;
			for (int32 Frame = 0; Frame < FramesPerBlock; ++Frame)
			{
				StatPeak = FMath::Max(StatPeak, FMath::Max(FMath::Abs(Left[Frame]), FMath::Abs(Right[Frame])));
			}
			if (StatBlocks * FramesPerBlock >= SampleRate)
			{
				UE_LOG(LogTemp, Display, TEXT("audiostats: sfizz %.0f us mean, %.0f us max (block %.0f us, %d over half), longest gap %.0f us, voices %d, peak %.2f, limiter %.1f dB, pedal down %d up %d, notes on %d off %d"),
					StatSumUs / StatBlocks, StatMaxUs, BlockUs, StatOverHalf, StatMaxGapUs, StatMaxVoices, StatPeak, 20.f * FMath::LogX(10.f, StatMinGain), StatPedalDown, StatPedalUp, StatNoteOns, StatNoteOffs);
				StatBlocks = StatOverHalf = StatMaxVoices = StatPedalDown = StatPedalUp = StatNoteOns = StatNoteOffs = 0;
				StatSumUs = StatMaxUs = StatMaxGapUs = 0.0;
				StatPeak = 0.f;
				StatMinGain = 1.f;
			}
		}

		// A short woodblock-like tick: a decaying sine burst, higher and louder on the accented beat.
		void StartClick(int32 Frame, bool bAccent)
		{
			ClickStartFrame = Frame;
			ClickPhase = 0.f;
			ClickFreq = bAccent ? 2093.f : 1568.f; // C7 / G6
			ClickAmp = bAccent ? 0.32f : 0.22f;
			ClickDecay = FMath::Exp(-1.f / (0.009f * SampleRate));
		}

		void RenderClick(float* Left, float* Right)
		{
			if (ClickAmp < 1e-4f)
			{
				return;
			}
			const float Step = 2.f * PI * ClickFreq / SampleRate;
			for (int32 Frame = FMath::Max(0, ClickStartFrame); Frame < FramesPerBlock; ++Frame)
			{
				const float V = ClickAmp * FMath::Sin(ClickPhase);
				Left[Frame] += V;
				Right[Frame] += V;
				ClickPhase += Step;
				ClickAmp *= ClickDecay;
			}
			ClickStartFrame = 0;
		}

		void SyncSynth()
		{
			const int32 Id = *SessionIdIn;
			if (Id != BoundSessionId)
			{
				BoundSessionId = Id;
				Session = Id != 0 ? FPianoSession::Find(Id) : nullptr;
				SynthVersion = ~0u;
				Synth.Reset();
			}
			if (!Session.IsValid() || Session->GetSynthVersion() == SynthVersion)
			{
				return;
			}
			Synth = Session->GetSynth(SynthVersion);
			AppliedSettings = 0; // a new instrument: apply every setting
			// Loaded for the device rate; a graph running at another rate (or with bigger blocks) reconfigures it once.
			if (Synth.IsValid() && Synth->Synth && (Synth->SampleRate != SampleRate || Synth->SamplesPerBlock < FramesPerBlock))
			{
				FSfizzApi::Get().set_sample_rate(Synth->Synth, SampleRate);
				FSfizzApi::Get().set_samples_per_block(Synth->Synth, FramesPerBlock);
				Synth->SampleRate = SampleRate;
				Synth->SamplesPerBlock = FramesPerBlock;
			}
		}

		FMidiStreamReadRef MidiIn;
		FInt32ReadRef SessionIdIn;
		FAudioBufferWriteRef OutLeft;
		FAudioBufferWriteRef OutRight;
		int32 FramesPerBlock = 256;
		float SampleRate = 48000.f;

		TSharedPtr<FPianoSession, ESPMode::ThreadSafe> Session;
		int32 BoundSessionId = 0;
		FPianoSession::FSynthPtr Synth;
		uint32 SynthVersion = ~0u;
		uint32 AppliedSettings = 0;
		float PluginGain = 1.f; // a plug-in has its own level; the app's volume setting goes on top

		int32 ClickStartFrame = 0;
		float ClickPhase = 0.f, ClickFreq = 0.f, ClickAmp = 0.f, ClickDecay = 0.f;

		const bool bAudioStats = FParse::Param(FCommandLine::Get(), TEXT("audiostats"));
		uint64 LastBlockEnd = 0;
		int32 StatBlocks = 0, StatOverHalf = 0, StatMaxVoices = 0, StatPedalDown = 0, StatPedalUp = 0, StatNoteOns = 0, StatNoteOffs = 0;
		double StatSumUs = 0.0, StatMaxUs = 0.0, StatMaxGapUs = 0.0;
		float StatPeak = 0.f, StatMinGain = 1.f;
		float LimiterGain = 1.f;
		TUniquePtr<FArchive> MidiDump{ FParse::Param(FCommandLine::Get(), TEXT("mididump"))
			? IFileManager::Get().CreateFileWriter(*(FPaths::ProjectSavedDir() / TEXT("PianoLogs/mididump.csv"))) : nullptr };
		int64 DumpBlock = 0;
	};

	using FPianoSfizzNode = Metasound::TNodeFacade<FPianoSfizzOperator>;
	METASOUND_REGISTER_NODE(FPianoSfizzNode)
}

#undef LOCTEXT_NAMESPACE
