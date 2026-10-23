// Falling notes rendered by the engine above the 3D keyboard: each note is a glowing bar over its key that falls
// towards a landing line at the back of the keys and arrives when it is to be played. Bars glow in the hand's colour
// (dimmer for the hand the app plays), turn green when played and red when missed; sparks burst where notes land.
// Measure lines and octave lanes run along the highway. The highway lies in the camera's view plane, so it reads like
// a flat waterfall, and reaches up to the top of the stage rectangle the UI gives.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PianoNoteHighway.generated.h"

class APianoKeyboard3D;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UPianoPlayer;

UCLASS()
class APianoNoteHighway : public AActor
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* GlowMaterialPath = TEXT("/PianoKit/Materials/M_NoteGlow.M_NoteGlow");

	APianoNoteHighway();
	void Init(UPianoPlayer* InPlayer, APianoKeyboard3D* InKeyboard);

	// Top of the stage as a window fraction (0 = top): the highway reaches up to there.
	void SetStageTop(float InTop) { StageTop = InTop; }
	void SetShown(bool bShow);
	bool IsShown() const { return bShown; }
	// Song time the highway shows ahead of now, at normal speed (scaled with the tempo).
	void SetLookaheadMs(float Ms) { LookaheadMs = FMath::Clamp(Ms, 800.f, 8000.f); }
	float GetLookaheadMs() const { return LookaheadMs; }
	void SetSparks(bool bEnable) { bSparks = bEnable; }
	bool GetSparks() const { return bSparks; }
	void SetColorByHand(bool bEnable) { bColorByHand = bEnable; }
	bool GetColorByHand() const { return bColorByHand; }

	virtual void Tick(float DeltaSeconds) override;

private:
	struct FSpark
	{
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float Age = 0.f;
		float Life = 0.5f;
		FLinearColor Color = FLinearColor::White;
	};

	struct FInstance
	{
		FTransform Transform;
		FLinearColor Color;
		float Intensity = 1.f;
	};

	UInstancedStaticMeshComponent* MakeLayer(const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material);
	void Upload(UInstancedStaticMeshComponent* Layer, const TArray<FInstance>& Instances, int32& Capacity);
	bool ComputeFrame(FVector& OutOrigin, FVector& OutUp, FVector& OutNormal, float& OutLength) const;
	// At: (towards the camera, along the keyboard, up the highway) from the landing line's centre.
	void SpawnSparks(const FVector& At, const FLinearColor& Color, int32 Count);

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Notes;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Effects;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Backdrop;

	TWeakObjectPtr<UPianoPlayer> Player;
	TWeakObjectPtr<APianoKeyboard3D> Keyboard;
	TArray<FSpark> Sparks;
	TBitArray<> Landed;      // per played note: sparks already spawned
	int32 LandedSerial = -1; // the play-note list Landed refers to
	double LastNowMs = 0.0;
	int32 NotesCapacity = 0;
	int32 EffectsCapacity = 0;
	float StageTop = 0.1f;
	float LookaheadMs = 2600.f;
	bool bShown = true;
	bool bSparks = true;
	bool bColorByHand = true;
	FRandomStream Random{ 7 };
};
