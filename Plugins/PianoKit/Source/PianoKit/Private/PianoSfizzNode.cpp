// "PianoKit Sfizz" MetaSound node: plays the session's sfizz instrument (an SFZ file, here the Salamander Grand
// Piano), driven by a Harmonix MIDI stream. sfizz (BSD-2, github.com/sfztools/sfizz) does the voices, velocity
// layers, release and resonance samples and the sustain pedal; events go in at their frame in the block and it
// renders straight into the output buffers.
//
// It replaced our own sampler node, which replaced Harmonix's Fusion Sampler (that one clicked with its 32-voice
// pool and stalled the audio render with more voices).

#include "PianoSession.h"
#include "SfizzApi.h"

#include "HarmonixMetasound/DataTypes/MidiStream.h"
#include "HarmonixMidi/MidiMsg.h"

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
		}

		void Execute()
		{
			OutLeft->Zero();
			OutRight->Zero();

			SyncSynth();
			if (!Synth.IsValid() || !Synth->Synth)
			{
				return;
			}
			const FSfizzApi& Api = FSfizzApi::Get();
			sfizz_synth_t* S = Synth->Synth;

			for (const FMidiStreamEvent& Event : MidiIn->GetEventsInBlock())
			{
				const FMidiMsg& Msg = Event.MidiMessage;
				const int32 Frame = FMath::Clamp(Event.BlockSampleFrameIndex, 0, FramesPerBlock - 1);
				if (Msg.IsStd() && Msg.GetStdChannel() == ClickChannel)
				{
					if (Msg.IsNoteOn())
					{
						StartClick(Frame, Msg.GetStdData1() == ClickAccentPitch);
					}
					continue;
				}
				if (Msg.IsNoteOff())
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

			float* Channels[2] = { OutLeft->GetData(), OutRight->GetData() };
			Api.render_block(S, Channels, 2, FramesPerBlock);
			RenderClick(Channels[0], Channels[1]);
		}

	private:
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

		int32 ClickStartFrame = 0;
		float ClickPhase = 0.f, ClickFreq = 0.f, ClickAmp = 0.f, ClickDecay = 0.f;
	};

	using FPianoSfizzNode = Metasound::TNodeFacade<FPianoSfizzOperator>;
	METASOUND_REGISTER_NODE(FPianoSfizzNode)
}

#undef LOCTEXT_NAMESPACE
