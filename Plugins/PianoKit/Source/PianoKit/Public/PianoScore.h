// A score engraved by Verovio as one long horizontal system (flowkey-style scrolling line),
// with every note's pitch, timing, hand and notehead position on that line.
#pragma once

#include "CoreMinimal.h"
#include "PianoSession.h"

namespace lunasvg { class Document; }

struct FPianoScoreNote
{
	FString Id;          // Verovio xml:id, also the SVG element id
	int32 Pitch = 60;    // MIDI note number
	double OnMs = 0.0;   // at the score's written tempo
	double DurMs = 0.0;
	int32 Staff = 1;     // 1-based staff index within the system
	EPianoHand Hand = EPianoHand::Right;
	FBox2f Head = FBox2f(ForceInit); // notehead box in SVG pixels
	// For the later notes of a tie: index of the tie's first note (which carries the whole duration). Else INDEX_NONE.
	int32 TieRoot = INDEX_NONE;
	bool bGrace = false; // ornament: playable and scored when played, but wait mode doesn't stop for it
	uint8 Artic = 0;     // EPianoArtic flags from the score
	uint8 Velocity = 80; // how the app plays it (FPianoExpression: dynamics, voicing, accents)
	bool IsStruck() const { return TieRoot == INDEX_NONE; }
};

enum EPianoArtic : uint8
{
	ArticAccent = 1,
	ArticMarcato = 2,
	ArticStaccato = 4,
	ArticStaccatissimo = 8,
	ArticTenuto = 16,
};

// A dynamic mark: a level (MIDI velocity for pp..fff, 0 for none) and/or a sudden accent (sf, fz, rf).
struct FPianoScoreDynamic
{
	double Ms = 0.0;
	int32 Level = 0;
	bool bAccent = false;
};

// A hairpin, or a written "cresc." / "dim." (to the next dynamic, at most two bars).
struct FPianoScoreHairpin
{
	double StartMs = 0.0, EndMs = 0.0;
	bool bCrescendo = true;
};

struct FPianoScoreMeasure
{
	FString Id;
	double OnMs = 0.0;
	double QStamp = 0.0; // start in quarter notes from the beginning of the piece
	float X = 0.f;      // left edge in SVG pixels
	// bar lines: at its end a double bar, the final bar, the end of a repeat; at its start the start of a repeat
	bool bDoubleBarEnd = false, bFinalEnd = false, bRepeatEnd = false, bRepeatStart = false;
};

// A slur (or phrase mark) from the onset of its first note to the onset of its last; EndOffMs is when that note ends.
struct FPianoScoreSlur
{
	double StartMs = 0.0, EndMs = 0.0, EndOffMs = 0.0;
	int32 Staff = 1;
};

// What the notation says about phrasing, read from Verovio's MEI.
struct FPianoScoreMarks
{
	TArray<FPianoScoreSlur> Slurs;
	TArray<FVector2D> Fermatas; // onset and end (ms) of each note or chord with a fermata
	int32 KeyFifths = 0;        // the first key signature: sharps > 0, flats < 0
	TArray<FPianoScoreDynamic> Dynamics; // by time
	TArray<FPianoScoreHairpin> Hairpins;
	TArray<FVector2D> Pedals;   // sustain pedal marks by time: (ms, 1 down / 0 up)
};

class PIANOKIT_API FPianoScore
{
public:
	FPianoScore();
	~FPianoScore();

	// MusicXML (.musicxml/.xml), compressed MusicXML (.mxl), MEI, Humdrum, ABC; .mscz/.mscx via MuseScore if installed.
	bool LoadFile(const FString& Path, FString& OutError);

	const FString& GetTitle() const { return Title; }
	const FString& GetSourcePath() const { return SourcePath; }
	const TArray<FPianoScoreNote>& GetNotes() const { return Notes; }
	const TArray<FPianoScoreMeasure>& GetMeasures() const { return Measures; }
	const FPianoScoreMarks& GetMarks() const { return Marks; }
	FVector2f GetSize() const { return Size; }
	double GetLengthMs() const { return LengthMs; }
	int32 GetNumStaves() const { return NumStaves; }

	// Song time of every quarter-note beat (from the measures' quarter stamps; tempo is constant within a measure).
	TArray<double> GetBeatTimes() const;
	// Start / end of measure Index (end = the next measure's start, or the end of the piece).
	double GetMeasureStartMs(int32 Index) const;
	double GetMeasureEndMs(int32 Index) const;
	// Measure playing at Ms (0 before the first).
	int32 GetMeasureAt(double Ms) const;

	// Scroll mapping between song time and x on the engraved line (SVG pixels). It moves at an even speed: anchored on
	// each bar's downbeat and smooth between (a monotone cubic), not note by note; at a repeat it jumps back.
	float TimeToX(double Ms) const;
	// The time at x - of the passes over it (repeats), the one nearest NearMs.
	double XToTime(float X, double NearMs = 0.0) const;

	lunasvg::Document* GetDocument() const { return Document.Get(); }
	// What the sequencer plays: one entry per score note (SourceIndex = its index here), sorted by time.
	// Expressive: with the score's dynamics, voicing and articulation (FPianoExpression); else every note alike.
	TArray<FPianoSessionNote> MakeSessionNotes(bool bExpressive = true) const;

	static FString GetVerovioDataDir();
	static FString FindMuseScore();

private:
	bool ConvertWithMuseScore(const FString& InPath, FString& OutPath, FString& OutError) const;
	void BuildScrollMap();

	FString Title;
	FString SourcePath;
	TArray<FPianoScoreNote> Notes;
	TArray<FPianoScoreMeasure> Measures;
	FPianoScoreMarks Marks;
	FVector2f Size = FVector2f::ZeroVector;
	double LengthMs = 0.0;
	int32 NumStaves = 0;

	TArray<double> MapMs;
	TArray<float> MapX;
	TArray<double> MapSlope;    // dx/dms at each anchor
	TArray<bool> MapRunStart;   // the line jumps back to here (a repeat)

	TUniquePtr<lunasvg::Document> Document;
};
