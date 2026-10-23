// Rasterises a score's engraved line (SVG) into fixed-width texture tiles at the display height.
// Tiles are rendered on a background task (one at a time, lunasvg documents are not thread-safe) and uploaded
// as textures on the game thread, so scrolling never waits for the ~60 ms a long piece's tile can take.
#pragma once

#include "CoreMinimal.h"
#include "Tasks/Task.h"
#include "UObject/GCObject.h"

class FPianoScore;
class UTexture2D;

class PIANOKIT_API FPianoSheetRenderer : public FGCObject
{
public:
	static constexpr int32 TileWidth = 2048;

	~FPianoSheetRenderer();

	void SetScore(const TSharedPtr<FPianoScore>& InScore);
	// Re-rasterises at this many pixels of height for the whole line (drops cached tiles if it changed).
	void SetPixelHeight(int32 InPixelHeight);

	int32 GetPixelHeight() const { return PixelHeight; }
	float GetScale() const { return Scale; } // texture pixels per SVG pixel
	int32 GetNumTiles() const;
	// The tile if it is ready; otherwise starts rendering it and returns nullptr (draw nothing there for now).
	UTexture2D* GetTile(int32 Index);
	// Drops tiles further than KeepRadius tiles from Center.
	void Trim(int32 Center, int32 KeepRadius = 3);

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FPianoSheetRenderer"); }

private:
	struct FPendingTile
	{
		UE::Tasks::TTask<TArray<uint8>> Task;
		uint32 Generation = 0;
	};

	void Invalidate();
	UTexture2D* MakeTexture(int32 Index, const TArray<uint8>& Pixels) const;

	TSharedPtr<FPianoScore> Score;
	int32 PixelHeight = 0;
	float Scale = 1.f;
	uint32 Generation = 0; // bumped when the score or height changes; older results are dropped
	TMap<int32, TObjectPtr<UTexture2D>> Tiles;
	TMap<int32, FPendingTile> Pending;
	UE::Tasks::TTask<TArray<uint8>> LastTask; // each render waits for the previous one
};
