#pragma once

#include "CoreMinimal.h"

struct FNTERemoveBlurScalarOverride
{
	FName ParameterName;
	float Value = 0.0f;
};

struct FNTERemoveBlurGeneratorParams
{
	FString AnimBlueprintPath;
	TArray<int32> MaterialElementIndices;
	TArray<FNTERemoveBlurScalarOverride> ScalarOverrides;
	bool bGenerateInitializeGraph = true;
	bool bGenerateUpdateGraph = true;
	bool bSaveAssets = true;
};

struct FNTERemoveBlurGeneratorResult
{
	bool bSuccess = false;
	TArray<FString> Messages;
	TArray<FString> Errors;

	FString ToDisplayString() const;
};

class FNTERemoveBlurGenerator
{
public:
	static TArray<FNTERemoveBlurScalarOverride> MakeDefaultScalarOverrides();
	static FNTERemoveBlurGeneratorResult Generate(const FNTERemoveBlurGeneratorParams& Params);
};
