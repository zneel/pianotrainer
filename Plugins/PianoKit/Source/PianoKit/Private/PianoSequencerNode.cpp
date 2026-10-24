// "PianoKit Sequencer" MetaSound node: plays the score's accompaniment and the player's live keys as a Harmonix
// MIDI stream, and runs the practice logic sample-accurately on the audio thread:
//   - wait mode: the song clock stops exactly at the next chord of the practised hand(s) until those keys are played
//   - play-along: presses are matched against the score within a timing window (hit / wrong / missed)
//   - tempo scaling, seeking and A-B looping
// Feed its output into the "PianoKit Sfizz" node for sound (it also receives the sustain pedal as CC64).

#include "PianoSession.h"

#include "HarmonixMetasound/DataTypes/MidiStream.h"
#include "HarmonixMidi/MidiMsg.h"
#include "HarmonixMidi/MidiVoiceId.h"

#include "Algo/BinarySearch.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundParamHelper.h"
#include "MetasoundPrimitives.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "PianoKit_SequencerNode"

namespace PianoKit::SequencerNode
{
	using namespace Metasound;
	using namespace HarmonixMetasound;

	namespace Inputs
	{
		METASOUND_PARAM(Session, "Session", "PianoKit session id (UPianoPlayer sets this graph input).")
	}
	namespace Outputs
	{
		METASOUND_PARAM(MidiStream, "MIDI Stream", "Accompaniment + live key events (track 1).")
	}

	static constexpr int32 MidiTrack = 1;
	static constexpr double ChordToleranceMs = 25.0;   // onsets closer than this are one chord for wait mode
	static constexpr double PlayAlongWindowMs = 220.0; // +- window for matching presses in play-along mode
	static constexpr double FinishPadMs = 400.0;
	static constexpr uint8 ClickChannel = 9;           // count-in clicks go out as notes on the drum channel
	static constexpr int32 ClickAccentPitch = 76, ClickPitch = 77;
	static constexpr int32 CountInVelocity = 100, MetronomeVelocity = 40; // the metronome clicks lightly

	class FPianoSequencerOperator final : public TExecutableOperator<FPianoSequencerOperator>
	{
	public:
		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto Init = []() -> FNodeClassMetadata
			{
				FNodeClassMetadata Info;
				Info.ClassName = { TEXT("PianoKit"), TEXT("PianoSequencer"), TEXT("") };
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = METASOUND_LOCTEXT("PianoSequencer_DisplayName", "PianoKit Sequencer");
				Info.Description = METASOUND_LOCTEXT("PianoSequencer_Description", "Practice sequencer: score accompaniment, live keys, wait mode, looping.");
				Info.Author = TEXT("PianoKit");
				Info.PromptIfMissing = METASOUND_LOCTEXT("PianoSequencer_Missing", "Enable the PianoKit plugin.");
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
					TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(Inputs::Session), 0)),
				FOutputVertexInterface(
					TOutputDataVertex<FMidiStream>(METASOUND_GET_PARAM_NAME_AND_METADATA(Outputs::MidiStream))));
			return Interface;
		}

		static TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults&)
		{
			FInt32ReadRef Session = InParams.InputData.GetOrCreateDefaultDataReadReference<int32>(
				METASOUND_GET_PARAM_NAME(Inputs::Session), InParams.OperatorSettings);
			return MakeUnique<FPianoSequencerOperator>(InParams, MoveTemp(Session));
		}

		FPianoSequencerOperator(const FBuildOperatorParams& InParams, FInt32ReadRef&& InSession)
			: SessionIdIn(MoveTemp(InSession))
			, MidiOut(FMidiStreamWriteRef::CreateNew())
			, FramesPerBlock(InParams.OperatorSettings.GetNumFramesPerBlock())
			, SampleRate(InParams.OperatorSettings.GetSampleRate())
		{
		}

		virtual void BindInputs(FInputVertexInterfaceData& InVertexData) override
		{
			InVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(Inputs::Session), SessionIdIn);
		}

		virtual void BindOutputs(FOutputVertexInterfaceData& InVertexData) override
		{
			InVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(Outputs::MidiStream), MidiOut);
		}

		void Reset(const IOperator::FResetParams&)
		{
			MidiOut->PrepareBlock();
			Session.Reset();
			BoundSessionId = 0;
			Notes.Reset();
			NotesVersion = 0;
			Pedals.Reset();
			PedalsVersion = 0;
			NextPedal = 0;
			BeatsVersion = 0;
			NextBeat = 0;
			bScorePedalDown = false;
			Sounding.Reset();
			LiveDown.Init(false, 128);
			SongMs = 0.0;
			bWaiting = false;
		}

		void Execute()
		{
			const uint64 Start = bAudioStats ? FPlatformTime::Cycles64() : 0;
			ExecuteBlock();
			if (bAudioStats)
			{
				// -audiostats: once a second, the longest block (a wait on the session's locks would show here)
				const double Us = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start) * 1000.0;
				StatMaxUs = FMath::Max(StatMaxUs, Us);
				StatSumUs += Us;
				if (++StatBlocks * FramesPerBlock >= SampleRate)
				{
					UE_LOG(LogTemp, Display, TEXT("audiostats: sequencer %.0f us mean, %.0f us max"), StatSumUs / StatBlocks, StatMaxUs);
					StatBlocks = 0;
					StatSumUs = StatMaxUs = 0.0;
				}
			}
		}

		void ExecuteBlock()
		{
			MidiOut->PrepareBlock();
			if (LiveDown.Num() != 128)
			{
				LiveDown.Init(false, 128);
			}

			ResolveSession();
			if (!Session.IsValid())
			{
				return;
			}
			SyncNotes();
			SyncPedals();
			SyncBeats();
			const uint8 Hands = Session->UserHands.load(std::memory_order_relaxed);
			const bool bListen = Session->bPlayUserHands.load(std::memory_order_relaxed);
			if (Hands != GroupsHands)
			{
				GroupsHands = Hands;
				RebuildGroups();
			}

			// 1. The player's keys and seeks, in order, at the start of the block so keys sound immediately.
			const bool bEcho = Session->bEchoLive.load(std::memory_order_relaxed);
			FPianoLiveEvent Live;
			while (Session->PopLive(Live))
			{
				if (Live.SeekMs >= 0.0)
				{
					SeekTo(Live.SeekMs);
					continue;
				}
				if (Live.Pedal >= 0)
				{
					// The instrument does the damper: it holds released notes and plays the pedal noises.
					EmitControl(LiveVoices, 0, 64, Live.Pedal != 0 ? 127 : 0);
					continue;
				}
				Live.Pitch &= 127;
				if (Live.bNoteOn)
				{
					if (bEcho)
					{
						if (LiveDown[Live.Pitch])
						{
							EmitNoteOff(LiveVoices, 0, Live.Pitch); // re-strike
						}
						EmitNoteOn(LiveVoices, 0, Live.Pitch, FMath::Max<int32>(Live.Velocity, 1));
						LiveDown[Live.Pitch] = true;
					}
					if (!bListen)
					{
						HandlePress(Live.Pitch, Live.Velocity);
					}
				}
				else if (LiveDown[Live.Pitch])
				{
					LiveDown[Live.Pitch] = false;
					EmitNoteOff(LiveVoices, 0, Live.Pitch);
				}
			}
			if (!bEcho)
			{
				ReleaseLiveNotes();
			}

			const double Rate = FMath::Clamp<double>(Session->TempoScale.load(std::memory_order_relaxed), 0.1, 2.0);
			const bool bPlaying = Session->bPlaying.load(std::memory_order_relaxed);
			const bool bCountInRequest = Session->bCountInRequest.exchange(false, std::memory_order_relaxed);
			if (bPlaying && (!bWasPlaying || bCountInRequest))
			{
				if (bWasPlaying && Sounding.Num() > 0)
				{
					AllAccompanimentOff(0); // nothing rings on under the clicks
				}
				CountInTotal = Session->CountInBeats.load(std::memory_order_relaxed);
				CountInElapsedMs = 0.0;
				CountInClicked = 0;
				bCountIn = CountInTotal > 0;
			}
			bWasPlaying = bPlaying;
			if (!bPlaying || !Notes.IsValid())
			{
				if (Sounding.Num() > 0)
				{
					AllAccompanimentOff(0);
				}
				SetScorePedal(0, false); // paused: nothing rings on
				bWaiting = false;
				bCountIn = false;
				Session->CountInLeft.store(0, std::memory_order_relaxed);
				Session->PublishPosition(SongMs, 0.0, false);
				return;
			}
			if (bCountIn)
			{
				RunCountIn(Rate);
				Session->PublishPosition(SongMs, 0.0, false);
				return;
			}

			// 2. Advance the song clock by one block, stopping at the next unplayed chord in wait mode.
			const bool bWait = Session->bWaitMode.load(std::memory_order_relaxed) && !bListen;
			const double BlockMs = 1000.0 * FramesPerBlock / SampleRate;
			double Target = SongMs + BlockMs * Rate;
			if (bWait && Groups.IsValidIndex(CurrentGroup) && Target >= Groups[CurrentGroup].OnMs)
			{
				Target = FMath::Max(SongMs, Groups[CurrentGroup].OnMs);
				if (!bWaiting)
				{
					bWaiting = true;
					Feedback(EPianoFeedback::Wait, INDEX_NONE, 0, 0.f);
				}
			}
			else if (!bWait)
			{
				bWaiting = false;
			}

			const double LoopStart = Session->LoopStartMs.load(std::memory_order_relaxed);
			const double LoopEnd = Session->LoopEndMs.load(std::memory_order_relaxed);
			if (LoopEnd > LoopStart && Target >= LoopEnd && SongMs < LoopEnd)
			{
				const int32 EndFrame = AdvanceTo(LoopEnd, Rate);
				AllAccompanimentOff(EndFrame);
				Feedback(EPianoFeedback::Looped, INDEX_NONE, 0, 0.f);
				SeekTo(LoopStart);
			}
			else
			{
				AdvanceTo(Target, Rate);
			}

			if (!bWait && !bListen)
			{
				SweepMisses();
			}
			if (!bFinishedSent && SongMs >= LastOffMs + FinishPadMs)
			{
				bFinishedSent = true;
				Feedback(EPianoFeedback::Finished, INDEX_NONE, 0, 0.f);
			}

			Session->PublishPosition(SongMs, bWaiting ? 0.0 : Rate, bWaiting);
		}

	private:
		struct FSounding
		{
			int32 Pitch;
			double OffMs;
		};

		struct FGroup
		{
			double OnMs = 0.0;
			TArray<int32, TInlineAllocator<8>> NoteIndices;
		};

		// Clicks before the music starts: one per beat at the playback tempo, the first accented; the music starts one
		// beat after the last click.
		void RunCountIn(double Rate)
		{
			const double BeatMs = FMath::Max(100.0, Session->CountInBeatMs.load(std::memory_order_relaxed) / Rate);
			const double BlockMs = 1000.0 * FramesPerBlock / SampleRate;
			// counted, not recomputed: a click exactly on a block boundary went out in both blocks
			for (; CountInClicked < CountInTotal && CountInClicked * BeatMs < CountInElapsedMs + BlockMs; ++CountInClicked)
			{
				const int32 Frame = FMath::Clamp(int32((CountInClicked * BeatMs - CountInElapsedMs) * SampleRate / 1000.0), 0, FramesPerBlock - 1);
				EmitClick(Frame, CountInClicked == 0, CountInVelocity);
			}
			CountInElapsedMs += BlockMs;
			const int32 BeatsLeft = CountInTotal - FMath::FloorToInt32(CountInElapsedMs / BeatMs);
			bCountIn = BeatsLeft > 0;
			Session->CountInLeft.store(FMath::Max(0, BeatsLeft), std::memory_order_relaxed);
		}

		void EmitClick(int32 Frame, bool bAccent, int32 Velocity)
		{
			FMidiStreamEvent Event(&ClickVoices, FMidiMsg::CreateNoteOn(ClickChannel, bAccent ? ClickAccentPitch : ClickPitch, Velocity));
			Event.BlockSampleFrameIndex = Frame;
			Event.TrackIndex = MidiTrack;
			MidiOut->InsertMidiEvent(Event);
		}

		void SyncBeats()
		{
			if (Session->GetBeatsVersion() != BeatsVersion)
			{
				Beats = Session->GetBeats(BeatsVersion);
				NextBeat = Algo::LowerBoundBy(*Beats, SongMs, [](const FVector2D& B) { return B.X; });
			}
		}

		void ResolveSession()
		{
			const int32 Id = *SessionIdIn;
			if (Id != BoundSessionId || (Id != 0 && !Session.IsValid()))
			{
				BoundSessionId = Id;
				Session = Id != 0 ? FPianoSession::Find(Id) : nullptr;
				NotesVersion = 0;
			}
		}

		void SyncPedals()
		{
			if (Session->GetPedalsVersion() != PedalsVersion)
			{
				Pedals = Session->GetPedals(PedalsVersion);
				NextPedal = Algo::LowerBoundBy(*Pedals, SongMs, [](const FVector2D& P) { return P.X; });
			}
			// the player's own pedal took over (or the setting changed): let the score's go
			if (!Session->bScorePedal.load(std::memory_order_relaxed))
			{
				SetScorePedal(0, false);
			}
		}

		// The score's sustain pedal (CC64) as the app plays, at its marks; the state it should be in at a time.
		void SetScorePedal(int32 Frame, bool bDown)
		{
			if (bDown != bScorePedalDown)
			{
				bScorePedalDown = bDown;
				EmitControl(AccompVoices, Frame, 64, bDown ? 127 : 0);
			}
		}

		bool ScorePedalAt(double Ms) const
		{
			const int32 Index = Pedals.IsValid() ? Algo::UpperBoundBy(*Pedals, Ms, [](const FVector2D& P) { return P.X; }) - 1 : -1;
			return Index >= 0 && (*Pedals)[Index].Y > 0.5;
		}

		void SyncNotes()
		{
			if (Session->GetNotesVersion() == NotesVersion)
			{
				return;
			}
			Notes = Session->GetNotes(NotesVersion);
			Hit.Init(0, Notes->Num());
			LastOffMs = 0.0;
			for (const FPianoSessionNote& N : *Notes)
			{
				LastOffMs = FMath::Max(LastOffMs, N.OffMs);
			}
			RebuildGroups();
			SeekTo(0.0);
		}

		bool IsUserNote(const FPianoSessionNote& N) const
		{
			return (uint8(N.Hand) & GroupsHands) != 0;
		}

		void RebuildGroups()
		{
			Groups.Reset();
			if (!Notes.IsValid())
			{
				return;
			}
			for (int32 Index = 0; Index < Notes->Num(); ++Index)
			{
				const FPianoSessionNote& N = (*Notes)[Index];
				if (!IsUserNote(N) || N.bTied || N.bGrace)
				{
					continue;
				}
				if (Groups.Num() == 0 || N.OnMs - Groups.Last().OnMs > ChordToleranceMs)
				{
					Groups.AddDefaulted_GetRef().OnMs = N.OnMs;
				}
				Groups.Last().NoteIndices.Add(Index);
			}
			CurrentGroup = FirstGroupAtOrAfter(SongMs);
		}

		int32 FirstGroupAtOrAfter(double Ms) const
		{
			int32 Index = 0;
			while (Index < Groups.Num() && Groups[Index].OnMs < Ms - 0.5)
			{
				++Index;
			}
			return Index;
		}

		void SeekTo(double Ms)
		{
			AllAccompanimentOff(0);
			SongMs = FMath::Max(0.0, Ms);
			bWaiting = false;
			bFinishedSent = false;
			if (!Notes.IsValid())
			{
				return;
			}
			NextOnIndex = Algo::LowerBoundBy(*Notes, SongMs - 0.5, &FPianoSessionNote::OnMs);
			MissSweepIndex = NextOnIndex;
			if (Pedals.IsValid())
			{
				NextPedal = Algo::LowerBoundBy(*Pedals, SongMs, [](const FVector2D& P) { return P.X; });
				SetScorePedal(0, Session->bScorePedal.load(std::memory_order_relaxed) && ScorePedalAt(SongMs));
			}
			if (Beats.IsValid())
			{
				NextBeat = Algo::LowerBoundBy(*Beats, SongMs - 0.5, [](const FVector2D& B) { return B.X; }); // a beat right here clicks
			}
			for (int32 Index = NextOnIndex; Index < Hit.Num(); ++Index)
			{
				Hit[Index] = 0;
			}
			CurrentGroup = FirstGroupAtOrAfter(SongMs);
		}

		// Emits score events in [SongMs, TargetMs) and moves the clock there. Returns the block frame of TargetMs.
		int32 AdvanceTo(double TargetMs, double Rate)
		{
			const double FromMs = SongMs;
			const double FramesPerMs = SampleRate / (1000.0 * Rate);
			auto FrameOf = [&](double Ms)
			{
				return FMath::Clamp(int32((Ms - FromMs) * FramesPerMs), 0, FramesPerBlock - 1);
			};

			for (int32 Index = Sounding.Num() - 1; Index >= 0; --Index)
			{
				if (Sounding[Index].OffMs < TargetMs)
				{
					EmitNoteOff(AccompVoices, FrameOf(Sounding[Index].OffMs), Sounding[Index].Pitch);
					Sounding.RemoveAtSwap(Index);
				}
			}

			// the score's pedal marks, before the notes they hold
			const bool bScorePedal = Session->bScorePedal.load(std::memory_order_relaxed);
			while (Pedals.IsValid() && NextPedal < Pedals->Num() && (*Pedals)[NextPedal].X < TargetMs)
			{
				if (bScorePedal)
				{
					SetScorePedal(FrameOf((*Pedals)[NextPedal].X), (*Pedals)[NextPedal].Y > 0.5);
				}
				++NextPedal;
			}

			// the metronome: a light click on each beat the music passes (none while wait mode holds the clock)
			const bool bMetronome = Session->bMetronome.load(std::memory_order_relaxed);
			while (Beats.IsValid() && NextBeat < Beats->Num() && (*Beats)[NextBeat].X < TargetMs)
			{
				if (bMetronome && (*Beats)[NextBeat].X >= FromMs - 0.5)
				{
					EmitClick(FrameOf((*Beats)[NextBeat].X), (*Beats)[NextBeat].Y > 0.5, MetronomeVelocity);
				}
				++NextBeat;
			}

			const bool bListen = Session->bPlayUserHands.load(std::memory_order_relaxed);
			while (NextOnIndex < Notes->Num() && (*Notes)[NextOnIndex].OnMs < TargetMs)
			{
				const FPianoSessionNote& N = (*Notes)[NextOnIndex];
				// listening: the chosen hand(s); practising: the other hand, for the player
				if (!N.bTied && (bListen ? IsUserNote(N) : !IsUserNote(N)))
				{
					const int32 Frame = FrameOf(N.OnMs);
					for (int32 Index = Sounding.Num() - 1; Index >= 0; --Index)
					{
						if (Sounding[Index].Pitch == N.Pitch)
						{
							EmitNoteOff(AccompVoices, Frame, N.Pitch);
							Sounding.RemoveAtSwap(Index);
						}
					}
					EmitNoteOn(AccompVoices, Frame, N.Pitch, N.bAccent ? FMath::Min(127, N.Velocity + 30) : N.Velocity);
					Sounding.Add({ N.Pitch, N.OffMs });
				}
				++NextOnIndex;
			}

			SongMs = TargetMs;
			return FrameOf(TargetMs);
		}

		double JudgedMs() const
		{
			// the player reacts to sound and sight that reach them late, and their keyboard reports late
			const double Rate = FMath::Clamp<double>(Session->TempoScale.load(std::memory_order_relaxed), 0.1, 2.0);
			return SongMs - double(Session->InputLatencyMs.load(std::memory_order_relaxed)) * Rate;
		}

		void HandlePress(int32 Pitch, uint8 Velocity)
		{
			if (!Notes.IsValid())
			{
				return;
			}
			const bool bWait = Session->bWaitMode.load(std::memory_order_relaxed);
			if (bWait)
			{
				// Any key of the next expected chord counts, however early: the music only waits, it never rushes.
				if (Groups.IsValidIndex(CurrentGroup))
				{
					FGroup& Group = Groups[CurrentGroup];
					for (int32 Index : Group.NoteIndices)
					{
						if (!Hit[Index] && (*Notes)[Index].Pitch == Pitch)
						{
							Hit[Index] = 1;
							Feedback(EPianoFeedback::Hit, Index, Pitch, float(SongMs - Group.OnMs), Velocity);
							bool bDone = true;
							for (int32 Other : Group.NoteIndices)
							{
								bDone &= Hit[Other] != 0;
							}
							if (bDone)
							{
								++CurrentGroup;
								if (bWaiting)
								{
									bWaiting = false;
									Feedback(EPianoFeedback::Resume, INDEX_NONE, 0, 0.f);
								}
							}
							return;
						}
					}
				}
				Feedback(EPianoFeedback::Wrong, INDEX_NONE, Pitch, 0.f, Velocity);
				return;
			}

			// Play-along: the closest unplayed note of this pitch within the window.
			int32 Best = INDEX_NONE;
			double BestError = PlayAlongWindowMs;
			const double PressMs = JudgedMs();
			const int32 Start = Algo::LowerBoundBy(*Notes, PressMs - PlayAlongWindowMs, &FPianoSessionNote::OnMs);
			for (int32 Index = Start; Index < Notes->Num() && (*Notes)[Index].OnMs <= PressMs + PlayAlongWindowMs; ++Index)
			{
				const FPianoSessionNote& N = (*Notes)[Index];
				const double Error = FMath::Abs(PressMs - N.OnMs);
				if (!Hit[Index] && !N.bTied && N.Pitch == Pitch && IsUserNote(N) && Error <= BestError)
				{
					Best = Index;
					BestError = Error;
				}
			}
			if (Best != INDEX_NONE)
			{
				Hit[Best] = 1;
				Feedback(EPianoFeedback::Hit, Best, Pitch, float(PressMs - (*Notes)[Best].OnMs), Velocity);
			}
			else
			{
				Feedback(EPianoFeedback::Wrong, INDEX_NONE, Pitch, 0.f, Velocity);
			}
		}

		void SweepMisses()
		{
			const double PressMs = JudgedMs();
			while (MissSweepIndex < Notes->Num() && (*Notes)[MissSweepIndex].OnMs < PressMs - PlayAlongWindowMs)
			{
				const FPianoSessionNote& N = (*Notes)[MissSweepIndex];
				if (IsUserNote(N) && !N.bTied && !N.bGrace && !Hit[MissSweepIndex])
				{
					Feedback(EPianoFeedback::Missed, MissSweepIndex, N.Pitch, 0.f);
				}
				++MissSweepIndex;
			}
		}

		void Feedback(EPianoFeedback Type, int32 NoteIndex, int32 Pitch, float ErrorMs, uint8 Velocity = 0)
		{
			FPianoFeedbackEvent Event;
			Event.Type = Type;
			// the game thread knows notes by their score index (drills re-time and re-sort the played list)
			Event.NoteIndex = NoteIndex != INDEX_NONE && Notes.IsValid() && Notes->IsValidIndex(NoteIndex) && (*Notes)[NoteIndex].SourceIndex != INDEX_NONE
				? (*Notes)[NoteIndex].SourceIndex : NoteIndex;
			Event.Velocity = Velocity;
			Event.Pitch = Pitch;
			Event.SongMs = SongMs;
			Event.ErrorMs = ErrorMs;
			Session->PushFeedback(Event);
		}

		void EmitNoteOn(const FMidiVoiceGeneratorBase& Generator, int32 Frame, int32 Pitch, int32 Velocity)
		{
			FMidiStreamEvent Event(&Generator, FMidiMsg::CreateNoteOn(0, Pitch, Velocity));
			Event.BlockSampleFrameIndex = Frame;
			Event.TrackIndex = MidiTrack;
			MidiOut->InsertMidiEvent(Event);
		}

		void EmitNoteOff(const FMidiVoiceGeneratorBase& Generator, int32 Frame, int32 Pitch)
		{
			FMidiStreamEvent Event(&Generator, FMidiMsg::CreateNoteOff(0, Pitch));
			Event.BlockSampleFrameIndex = Frame;
			Event.TrackIndex = MidiTrack;
			MidiOut->InsertMidiEvent(Event);
		}

		void EmitControl(const FMidiVoiceGeneratorBase& Generator, int32 Frame, int32 Control, int32 Value)
		{
			FMidiStreamEvent Event(&Generator, FMidiMsg::CreateControlChange(0, uint8(Control), uint8(Value)));
			Event.BlockSampleFrameIndex = Frame;
			Event.TrackIndex = MidiTrack;
			MidiOut->InsertMidiEvent(Event);
		}

		void AllAccompanimentOff(int32 Frame)
		{
			for (const FSounding& S : Sounding)
			{
				EmitNoteOff(AccompVoices, Frame, S.Pitch);
			}
			Sounding.Reset();
		}

		void ReleaseLiveNotes()
		{
			for (int32 Pitch = 0; Pitch < LiveDown.Num(); ++Pitch)
			{
				if (LiveDown[Pitch])
				{
					EmitNoteOff(LiveVoices, 0, Pitch);
					LiveDown[Pitch] = false;
				}
			}
		}

		FInt32ReadRef SessionIdIn;
		FMidiStreamWriteRef MidiOut;
		int32 FramesPerBlock = 256;
		float SampleRate = 48000.f;
		const bool bAudioStats = FParse::Param(FCommandLine::Get(), TEXT("audiostats"));
		int32 StatBlocks = 0;
		double StatSumUs = 0.0, StatMaxUs = 0.0;

		TSharedPtr<FPianoSession, ESPMode::ThreadSafe> Session;
		int32 BoundSessionId = 0;
		FPianoSession::FNoteListPtr Notes;
		uint32 NotesVersion = 0;
		FPianoSession::FPedalListPtr Pedals;
		uint32 PedalsVersion = 0;
		int32 NextPedal = 0;
		FPianoSession::FPedalListPtr Beats;
		uint32 BeatsVersion = 0;
		int32 NextBeat = 0;
		bool bScorePedalDown = false;
		double LastOffMs = 0.0;

		double SongMs = 0.0;
		int32 NextOnIndex = 0;
		int32 MissSweepIndex = 0;
		TArray<FSounding> Sounding;
		TArray<uint8> Hit;
		TBitArray<> LiveDown;

		uint8 GroupsHands = 0;
		TArray<FGroup> Groups;
		int32 CurrentGroup = 0;
		bool bWaiting = false;
		bool bFinishedSent = false;

		FMidiVoiceGeneratorBase AccompVoices;
		FMidiVoiceGeneratorBase LiveVoices;
		FMidiVoiceGeneratorBase ClickVoices;
		bool bWasPlaying = false;
		bool bCountIn = false;
		int32 CountInTotal = 0;
		double CountInElapsedMs = 0.0;
		int32 CountInClicked = 0;
	};

	using FPianoSequencerNode = Metasound::TNodeFacade<FPianoSequencerOperator>;
	METASOUND_REGISTER_NODE(FPianoSequencerNode)
}

#undef LOCTEXT_NAMESPACE
