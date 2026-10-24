// How the app plays a score, instead of every note alike (which is what MIDI from notation gives):
//  - the dynamic level where the note is (pp..fff), moving through hairpins and written cresc. / dim. towards the next
//    dynamic (or a step up or down when none follows);
//  - voicing: the melody - the top line of the right hand, and notes held clearly longer than the ones around them -
//    sounds above the inner voices; the bass of the left hand a little above its inner notes;
//  - accents, marcato, sforzandi (+), tenuto (a little), staccato (a little lighter; it is also shortened);
//  - a little weight on downbeats and beats; phrase endings taper;
//  - a small, fixed variation per note, so that nothing sounds machine-flat.
#pragma once

#include "CoreMinimal.h"

class FPianoScore;

struct PIANOKIT_API FPianoExpression
{
	// A velocity (1..127) for every note of the score, in the score's note order.
	static TArray<uint8> Velocities(const FPianoScore& Score);
};
