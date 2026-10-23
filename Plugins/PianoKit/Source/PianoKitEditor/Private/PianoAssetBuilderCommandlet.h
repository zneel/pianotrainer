#pragma once

#include "Commandlets/Commandlet.h"
#include "PianoAssetBuilderCommandlet.generated.h"

// Creates the assets the 3D piano needs that can't be made at runtime (materials need the shader compiler):
//   UnrealEditor-Cmd PianoTrainer.uproject -run=PianoAssetBuilder
// /PianoKit/Materials/M_PianoSurface: lit, two-sided; parameters Color, Roughness, Specular, ClearCoat,
// ClearCoatRoughness, Emissive. Keys, lacquer and felt are instances of it with different values.
UCLASS()
class UPianoAssetBuilderCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UPianoAssetBuilderCommandlet();
	virtual int32 Main(const FString& Params) override;
};
