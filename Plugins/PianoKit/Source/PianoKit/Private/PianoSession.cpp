#include "PianoSession.h"

#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

namespace PianoSessionRegistry
{
	static FCriticalSection Lock;
	static TMap<int32, TSharedPtr<FPianoSession, ESPMode::ThreadSafe>> Sessions;
	static int32 NextId = 1;
}

int32 FPianoSession::Register(const TSharedRef<FPianoSession, ESPMode::ThreadSafe>& Session)
{
	FScopeLock Guard(&PianoSessionRegistry::Lock);
	const int32 Id = PianoSessionRegistry::NextId++;
	PianoSessionRegistry::Sessions.Add(Id, Session);
	return Id;
}

void FPianoSession::Unregister(int32 Id)
{
	FScopeLock Guard(&PianoSessionRegistry::Lock);
	PianoSessionRegistry::Sessions.Remove(Id);
}

TSharedPtr<FPianoSession, ESPMode::ThreadSafe> FPianoSession::Find(int32 Id)
{
	FScopeLock Guard(&PianoSessionRegistry::Lock);
	const TSharedPtr<FPianoSession, ESPMode::ThreadSafe>* Found = PianoSessionRegistry::Sessions.Find(Id);
	return Found ? *Found : nullptr;
}

void FPianoSession::SetNotes(FNoteList&& InNotes)
{
	FNoteListPtr NewNotes = MakeShared<const FNoteList, ESPMode::ThreadSafe>(MoveTemp(InNotes));
	{
		FScopeLock Guard(&NotesLock);
		Notes = NewNotes;
	}
	NotesVersion.fetch_add(1, std::memory_order_acq_rel);
}

FPianoSession::FNoteListPtr FPianoSession::GetNotes(uint32& OutVersion) const
{
	FScopeLock Guard(&NotesLock);
	OutVersion = NotesVersion.load(std::memory_order_acquire);
	return Notes;
}

void FPianoSession::SetPedals(FPedalList&& InPedals)
{
	FPedalListPtr NewPedals = MakeShared<const FPedalList, ESPMode::ThreadSafe>(MoveTemp(InPedals));
	{
		FScopeLock Guard(&NotesLock);
		Pedals = NewPedals;
	}
	PedalsVersion.fetch_add(1, std::memory_order_acq_rel);
}

FPianoSession::FPedalListPtr FPianoSession::GetPedals(uint32& OutVersion) const
{
	FScopeLock Guard(&NotesLock);
	OutVersion = PedalsVersion.load(std::memory_order_acquire);
	return Pedals;
}

void FPianoSession::SetSynth(FSynthPtr InSynth)
{
	{
		FScopeLock Guard(&NotesLock);
		Synth = MoveTemp(InSynth);
	}
	SynthVersion.fetch_add(1, std::memory_order_acq_rel);
}

FPianoSession::FSynthPtr FPianoSession::GetSynth(uint32& OutVersion) const
{
	FScopeLock Guard(&NotesLock);
	OutVersion = SynthVersion.load(std::memory_order_acquire);
	return Synth;
}

void FPianoSession::PublishPosition(double SongMs, double Rate, bool bWaiting)
{
	PosSeq.fetch_add(1, std::memory_order_acq_rel); // odd: write in progress
	PosSongMs.store(SongMs, std::memory_order_relaxed);
	PosRate.store(Rate, std::memory_order_relaxed);
	PosWallSeconds.store(FPlatformTime::Seconds(), std::memory_order_relaxed);
	PosWaiting.store(bWaiting, std::memory_order_relaxed);
	PosSeq.fetch_add(1, std::memory_order_acq_rel); // even: consistent
}

double FPianoSession::GetDisplaySongMs(bool* OutWaiting, double* OutRate) const
{
	double SongMs = 0.0, Rate = 0.0, Wall = 0.0;
	bool bWaiting = false;
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		const uint32 Before = PosSeq.load(std::memory_order_acquire);
		if (Before & 1u)
		{
			continue;
		}
		SongMs = PosSongMs.load(std::memory_order_relaxed);
		Rate = PosRate.load(std::memory_order_relaxed);
		Wall = PosWallSeconds.load(std::memory_order_relaxed);
		bWaiting = PosWaiting.load(std::memory_order_relaxed);
		if (PosSeq.load(std::memory_order_acquire) == Before)
		{
			break;
		}
	}
	if (OutWaiting)
	{
		*OutWaiting = bWaiting;
	}
	if (OutRate)
	{
		*OutRate = Rate;
	}
	// The audio thread publishes once per block; extrapolate between blocks, but never by more than ~2 blocks
	// so a stalled audio device can't run the display away.
	const double Elapsed = FMath::Clamp(FPlatformTime::Seconds() - Wall, 0.0, 0.05);
	return SongMs + Elapsed * 1000.0 * Rate;
}
