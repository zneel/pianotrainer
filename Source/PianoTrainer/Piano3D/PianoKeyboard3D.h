// A real-size 3D piano keyboard rendered by the engine: 88 modelled keys (C..B shapes with notches for the black
// keys, bevelled tops) on a lacquered key bed with cheek blocks, fallboard and felt, lit by a sky cubemap and a key
// light, seen from the player's seat. Keys go down when they sound (you or the accompaniment) and glow in the hand
// colour when they are next to play. The camera frames the keyboard into a band at the bottom of the window.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PianoKeyboard3D.generated.h"

class UCameraComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPianoPlayer;
class UProceduralMeshComponent;
struct FPianoKeyState;

UCLASS()
class APianoKeyboard3D : public AActor
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* SurfaceMaterialPath = TEXT("/PianoKit/Materials/M_PianoSurface.M_PianoSurface");

	APianoKeyboard3D();

	void Init(UPianoPlayer* InPlayer);
	// Part of the window (fractions: 0 = left / top, 1 = right / bottom) the keyboard should fill, centred.
	void SetScreenRect(float InLeft, float InTop, float InRight, float InBottom) { BandLeft = InLeft; BandTop = InTop; BandRight = InRight; BandBottom = InBottom; }
	// Key under a viewport pixel position, or INDEX_NONE.
	int32 PitchAtViewportPosition(const FVector2D& Pixel) const;
	UCameraComponent* GetCamera() const { return Camera; }

	// Keyboard geometry (cm, actor space; see the .cpp): the key's left/right edge in Y (its playing surface).
	static FVector2f KeySpan(int32 Pitch);
	static bool IsBlackKey(int32 Pitch);
	// Where falling notes land: the top front edge of the fallboard behind the keys.
	static FVector NoteLandingPoint();
	static float GetKeyboardWidth();

	virtual void Tick(float DeltaSeconds) override;

private:
	struct FKey
	{
		int32 Pitch = 0;
		bool bBlack = false;
		float Angle = 0.f;
		uint32 LookHash = ~0u;
		TObjectPtr<UProceduralMeshComponent> Mesh;
		TObjectPtr<UMaterialInstanceDynamic> Material;
	};

	void BuildKeys(UMaterialInterface* Surface);
	void BuildBody(UMaterialInterface* Surface);
	void BuildLights();
	void UpdateCamera();
	void UpdateKeys(float DeltaSeconds);
	void ApplyLook(FKey& Key, const FPianoKeyState& State);

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY()
	TArray<TObjectPtr<UObject>> Owned; // components and materials created at runtime

	TArray<FKey> Keys; // index = pitch - 21
	TWeakObjectPtr<UPianoPlayer> Player;
	float BandLeft = 0.f;
	float BandTop = 0.64f;
	float BandRight = 1.f;
	float BandBottom = 1.f;
};
