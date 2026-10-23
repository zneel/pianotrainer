#include "PianoStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

namespace PianoStyleImpl
{
	// Smooth value noise in [0, 1], periodic over PeriodX x PeriodY lattice cells (so the texture tiles).
	static float Noise(float X, float Y, int32 PeriodX, int32 PeriodY)
	{
		auto Hash = [](int32 IX, int32 IY)
		{
			uint32 H = uint32(IX) * 374761393u + uint32(IY) * 668265263u;
			H = (H ^ (H >> 13)) * 1274126177u;
			return float(H & 0xFFFF) / 65535.f;
		};
		const int32 IX = FMath::FloorToInt32(X), IY = FMath::FloorToInt32(Y);
		const float FX = X - IX, FY = Y - IY;
		const float SX = FX * FX * (3.f - 2.f * FX), SY = FY * FY * (3.f - 2.f * FY);
		const int32 X0 = ((IX % PeriodX) + PeriodX) % PeriodX, X1 = (X0 + 1) % PeriodX;
		const int32 Y0 = ((IY % PeriodY) + PeriodY) % PeriodY, Y1 = (Y0 + 1) % PeriodY;
		const float A = FMath::Lerp(Hash(X0, Y0), Hash(X1, Y0), SX);
		const float B = FMath::Lerp(Hash(X0, Y1), Hash(X1, Y1), SX);
		return FMath::Lerp(A, B, SY);
	}

	// Walnut: long grain along Y, growth rings bent by low-frequency noise, fine pores, warm dark tones.
	static UTexture2D* MakeWoodTexture()
	{
		constexpr int32 W = 256, H = 1024;
		UTexture2D* Texture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, TEXT("PianoWood"));
		if (!Texture)
		{
			return nullptr;
		}
		Texture->SRGB = true;
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Wrap;
		Texture->AddressY = TA_Wrap;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		const FLinearColor Dark = PianoStyle::Hex(TEXT("1E0F07")), Mid = PianoStyle::Hex(TEXT("3E2112")), Light = PianoStyle::Hex(TEXT("5E3519"));
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const float U = float(X) / W, V = float(Y) / H;
				const float Warp = 2.2f * Noise(U * 4.f, V * 3.f, 4, 3) + 0.6f * Noise(U * 16.f, V * 12.f, 16, 12);
				const float Rings = 0.5f + 0.5f * FMath::Sin((U * 9.f + Warp) * 2.f * PI);
				const float Fine = Noise(U * 120.f, V * 6.f, 120, 6);
				const float Pores = Noise(U * 256.f, V * 64.f, 256, 64) > 0.82f ? 0.6f : 1.f;
				const float T = FMath::Clamp(0.15f + 0.55f * Rings * Rings + 0.3f * Fine, 0.f, 1.f);
				FLinearColor C = T < 0.5f ? FMath::Lerp(Dark, Mid, T * 2.f) : FMath::Lerp(Mid, Light, (T - 0.5f) * 2.f);
				C *= Pores;
				Pixels[Y * W + X] = C.ToFColor(true);
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		Texture->AddToRoot(); // lives as long as the app
		return Texture;
	}
}

const FSlateBrush* PianoStyle::Rounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline, float OutlineWidth)
{
	static TMap<uint32, TUniquePtr<FSlateRoundedBoxBrush>> Cache;
	const uint32 Key = HashCombine(HashCombine(GetTypeHash(Fill.ToFColor(false)), GetTypeHash(Outline.ToFColor(false))),
		HashCombine(GetTypeHash(int32(Radius * 10.f)), GetTypeHash(int32(OutlineWidth * 10.f))));
	TUniquePtr<FSlateRoundedBoxBrush>& Brush = Cache.FindOrAdd(Key);
	if (!Brush)
	{
		Brush = OutlineWidth > 0.f ? MakeUnique<FSlateRoundedBoxBrush>(Fill, Radius, Outline, OutlineWidth) : MakeUnique<FSlateRoundedBoxBrush>(Fill, Radius);
	}
	return Brush.Get();
}

const FSlateBrush* PianoStyle::Wood()
{
	static TUniquePtr<FSlateImageBrush> Brush;
	if (!Brush)
	{
		UTexture2D* Texture = PianoStyleImpl::MakeWoodTexture();
		Brush = MakeUnique<FSlateImageBrush>(Texture, FVector2D(256.0, 1024.0));
		Brush->Tiling = ESlateBrushTileType::Both;
	}
	return Brush.Get();
}

const FSlateBrush* PianoStyle::White()
{
	return FCoreStyle::Get().GetBrush("GenericWhiteBox");
}
