#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SCheckBox;
class SEditableTextBox;
class STextBlock;

class SNTERemoveBlurPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNTERemoveBlurPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnGenerateClicked();
	void OnAnimBlueprintChanged(const struct FAssetData& AssetData);
	FString GetAnimBlueprintPath() const;

	TSharedRef<SWidget> MakeTextRow(const FText& Label, const TSharedRef<SEditableTextBox>& TextBox) const;
	TSharedRef<SWidget> MakeAnimBlueprintPickerRow();
	void LoadSettings();
	void SaveSettings() const;

	static TArray<int32> ParseMaterialSlots(const FString& Text, TArray<FString>& OutErrors);

	FString AnimBlueprintPath;
	TSharedPtr<SEditableTextBox> MaterialSlotsBox;
	TSharedPtr<SCheckBox> InitGraphCheckBox;
	TSharedPtr<SCheckBox> UpdateGraphCheckBox;
	TSharedPtr<SCheckBox> SaveAssetsCheckBox;
	TSharedPtr<STextBlock> StatusText;
};
