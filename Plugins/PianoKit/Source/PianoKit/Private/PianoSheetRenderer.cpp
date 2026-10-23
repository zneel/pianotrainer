#include "PianoSheetRenderer.h"

#include "PianoKitLog.h"
#include "PianoScore.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"

#include "lunasvg.h"

FPianoSheetRenderer::~FPianoSheetRenderer()
{
	// tasks hold their own reference to the score, but don't leave work running past the renderer
	if (LastTask.IsValid())
	{
		LastTask.Wait();
	}
}

void FPianoSheetRenderer::Invalidate()
{
	++Generation;
	Tiles.Reset();
	Pending.Reset();
}

void FPianoSheetRenderer::SetScore(const TSharedPtr<FPianoScore>& InScore)
{
	Score = InScore;
	Invalidate();
	SetPixelHeight(PixelHeight);
}

void FPianoSheetRenderer::SetPixelHeight(int32 InPixelHeight)
{
	const float NewScale = (Score.IsValid() && Score->GetSize().Y > 0.f) ? float(FMath::Max(InPixelHeight, 1)) / Score->GetSize().Y : 1.f;
	if (InPixelHeight != PixelHeight || !FMath::IsNearlyEqual(NewScale, Scale))
	{
		PixelHeight = InPixelHeight;
		Scale = NewScale;
		Invalidate();
	}
}

int32 FPianoSheetRenderer::GetNumTiles() const
{
	if (!Score.IsValid() || PixelHeight <= 0)
	{
		return 0;
	}
	return FMath::DivideAndRoundUp(FMath::CeilToInt(Score->GetSize().X * Scale), TileWidth);
}

UTexture2D* FPianoSheetRenderer::GetTile(int32 Index)
{
	if (Index < 0 || Index >= GetNumTiles() || !Score.IsValid() || !Score->GetDocument())
	{
		return nullptr;
	}
	if (TObjectPtr<UTexture2D>* Found = Tiles.Find(Index))
	{
		return *Found;
	}

	if (FPendingTile* InFlight = Pending.Find(Index))
	{
		if (!InFlight->Task.IsCompleted())
		{
			return nullptr;
		}
		UTexture2D* Texture = InFlight->Generation == Generation ? MakeTexture(Index, InFlight->Task.GetResult()) : nullptr;
		Pending.Remove(Index);
		if (Texture)
		{
			Tiles.Add(Index, Texture);
		}
		return Texture;
	}

	// Render on a background task. It captures what it needs by value (score kept alive by the shared pointer).
	const TSharedPtr<FPianoScore> TileScore = Score;
	const int32 Height = PixelHeight;
	const float TileScale = Scale;
	auto Render = [TileScore, Height, TileScale, Index]() -> TArray<uint8>
	{
		const double StartSeconds = FPlatformTime::Seconds();
		lunasvg::Bitmap Bitmap(TileWidth, Height);
		Bitmap.clear(0xFFFFFFFF);
		// scale the whole line, then shift so this tile's slice starts at x = 0
		TileScore->GetDocument()->render(Bitmap, lunasvg::Matrix(TileScale, 0.f, 0.f, TileScale, -float(Index * TileWidth), 0.f));

		// lunasvg bitmaps are premultiplied ARGB32 (B,G,R,A in memory); on an opaque white background that is plain BGRA.
		TArray<uint8> Pixels;
		Pixels.SetNumUninitialized(TileWidth * Height * 4);
		const int32 RowBytes = TileWidth * 4;
		for (int32 Row = 0; Row < Height; ++Row)
		{
			FMemory::Memcpy(Pixels.GetData() + Row * RowBytes, Bitmap.data() + Row * Bitmap.stride(), RowBytes);
		}
		UE_LOG(LogPianoKit, Log, TEXT("Rendered sheet tile %d (%dx%d) in %.1f ms (background)"), Index, TileWidth, Height,
			(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		return Pixels;
	};
	FPendingTile& NewTile = Pending.Add(Index);
	NewTile.Generation = Generation;
	NewTile.Task = LastTask.IsValid()
		? UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(Render), UE::Tasks::Prerequisites(LastTask))
		: UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(Render));
	LastTask = NewTile.Task;
	return nullptr;
}

UTexture2D* FPianoSheetRenderer::MakeTexture(int32 Index, const TArray<uint8>& Pixels) const
{
	if (Pixels.Num() != TileWidth * PixelHeight * 4)
	{
		return nullptr;
	}
	UTexture2D* Texture = UTexture2D::CreateTransient(TileWidth, PixelHeight, PF_B8G8R8A8, *FString::Printf(TEXT("SheetTile_%d"), Index));
	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;
	Texture->LODGroup = TEXTUREGROUP_UI;
	Texture->NeverStream = true;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), Pixels.GetData(), Pixels.Num());
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}

void FPianoSheetRenderer::Trim(int32 Center, int32 KeepRadius)
{
	for (auto It = Tiles.CreateIterator(); It; ++It)
	{
		if (FMath::Abs(It.Key() - Center) > KeepRadius)
		{
			It.RemoveCurrent();
		}
	}
}

void FPianoSheetRenderer::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(Tiles);
}
