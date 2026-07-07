#include "SNTERemoveBlurPanel.h"

#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Misc/ConfigCacheIni.h"
#include "NTERemoveBlurGenerator.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SNTERemoveBlurPanel"

namespace NTERemoveBlurPanel
{
	static const TCHAR* SettingsSection = TEXT("NTERemoveBlurTool.Settings");
	static const TCHAR* AnimBlueprintKey = TEXT("AnimBlueprintPath");
	static const TCHAR* MaterialSlotsKey = TEXT("MaterialSlots");

	static bool IsChecked(const TSharedPtr<SCheckBox>& CheckBox)
	{
		return CheckBox.IsValid() && CheckBox->IsChecked();
	}

	static FString TextBoxString(const TSharedPtr<SEditableTextBox>& TextBox)
	{
		return TextBox.IsValid() ? TextBox->GetText().ToString() : FString();
	}
}

void SNTERemoveBlurPanel::Construct(const FArguments& InArgs)
{
	LoadSettings();

	ChildSlot
	[
		SNew(SBorder)
		.Padding(14)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 12)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PanelTitle", "NTE Remove Blur Tool"))
					.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 12)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(LOCTEXT("PanelHint", "Generate Animation Blueprint nodes that create Dynamic Material Instances for selected material slots and reapply anti-dither scalar parameters."))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
				[
					MakeAnimBlueprintPickerRow()
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
				[
					MakeTextRow(
						LOCTEXT("MaterialSlots", "Material Slot(s)"),
						SAssignNew(MaterialSlotsBox, SEditableTextBox)
							.Text(FText::FromString(TEXT("0")))
							.HintText(LOCTEXT("MaterialSlotsHint", "Single: 3    Multiple: 1,5,6")))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 8, 0, 8)
				[
					SNew(SSeparator)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(LOCTEXT("PresetText", "Preset: FPSCameFade=0, FPSCameraCutHeight=-99999, FPSCameraCutHeightOverride=-99999, PlayerDitherRandom=0, Opacity/Mask values=1, Dissolve values=0."))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0, 0, 18, 0)
					[
						SAssignNew(InitGraphCheckBox, SCheckBox)
						.IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("InitializeGraph", "Initialize graph"))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0, 0, 18, 0)
					[
						SAssignNew(UpdateGraphCheckBox, SCheckBox)
						.IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("UpdateGraph", "Update graph"))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SAssignNew(SaveAssetsCheckBox, SCheckBox)
						.IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("SaveAssets", "Save assets"))
						]
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 8, 0, 10)
				[
					SNew(SButton)
					.Text(LOCTEXT("GenerateButton", "Generate Remove Blur Nodes"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SNTERemoveBlurPanel::OnGenerateClicked)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 4, 0, 0)
				[
					SNew(SBorder)
					.Padding(10)
					.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
					[
						SAssignNew(StatusText, STextBlock)
						.AutoWrapText(true)
						.Text(LOCTEXT("InitialStatus", "Ready."))
					]
				]
			]
		]
	];

	FString SavedSlots;
	if (GConfig->GetString(NTERemoveBlurPanel::SettingsSection, NTERemoveBlurPanel::MaterialSlotsKey, SavedSlots, GEditorPerProjectIni) && MaterialSlotsBox.IsValid())
	{
		MaterialSlotsBox->SetText(FText::FromString(SavedSlots));
	}
}

TSharedRef<SWidget> SNTERemoveBlurPanel::MakeTextRow(const FText& Label, const TSharedRef<SEditableTextBox>& TextBox) const
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(150)
			[
				SNew(STextBlock).Text(Label)
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			TextBox
		];
}

TSharedRef<SWidget> SNTERemoveBlurPanel::MakeAnimBlueprintPickerRow()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(150)
			[
				SNew(STextBlock).Text(LOCTEXT("AnimBlueprint", "Anim Blueprint"))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UBlueprint::StaticClass())
			.AllowClear(false)
			.DisplayThumbnail(false)
			.ObjectPath(TAttribute<FString>(this, &SNTERemoveBlurPanel::GetAnimBlueprintPath))
			.OnObjectChanged(FOnSetObject::CreateSP(this, &SNTERemoveBlurPanel::OnAnimBlueprintChanged))
		];
}

void SNTERemoveBlurPanel::LoadSettings()
{
	FString SavedAnimBlueprintPath;
	if (GConfig->GetString(NTERemoveBlurPanel::SettingsSection, NTERemoveBlurPanel::AnimBlueprintKey, SavedAnimBlueprintPath, GEditorPerProjectIni))
	{
		AnimBlueprintPath = MoveTemp(SavedAnimBlueprintPath);
	}
}

void SNTERemoveBlurPanel::SaveSettings() const
{
	GConfig->SetString(NTERemoveBlurPanel::SettingsSection, NTERemoveBlurPanel::AnimBlueprintKey, *AnimBlueprintPath, GEditorPerProjectIni);
	const FString MaterialSlots = NTERemoveBlurPanel::TextBoxString(MaterialSlotsBox);
	GConfig->SetString(NTERemoveBlurPanel::SettingsSection, NTERemoveBlurPanel::MaterialSlotsKey, *MaterialSlots, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SNTERemoveBlurPanel::OnAnimBlueprintChanged(const FAssetData& AssetData)
{
	AnimBlueprintPath = AssetData.IsValid() ? AssetData.GetSoftObjectPath().ToString() : FString();
	SaveSettings();
}

FString SNTERemoveBlurPanel::GetAnimBlueprintPath() const
{
	return AnimBlueprintPath;
}

TArray<int32> SNTERemoveBlurPanel::ParseMaterialSlots(const FString& Text, TArray<FString>& OutErrors)
{
	FString Normalized = Text;
	Normalized.ReplaceInline(TEXT("\n"), TEXT(","));
	Normalized.ReplaceInline(TEXT("\r"), TEXT(","));
	Normalized.ReplaceInline(TEXT(";"), TEXT(","));
	Normalized.ReplaceInline(TEXT("\uFF1B"), TEXT(","));
	Normalized.ReplaceInline(TEXT("\uFF0C"), TEXT(","));
	Normalized.ReplaceInline(TEXT(" "), TEXT(","));

	TArray<FString> Tokens;
	Normalized.ParseIntoArray(Tokens, TEXT(","), true);

	TArray<int32> Values;
	for (FString Token : Tokens)
	{
		Token.TrimStartAndEndInline();
		if (Token.IsEmpty())
		{
			continue;
		}

		if (!Token.IsNumeric())
		{
			OutErrors.Add(FString::Printf(TEXT("Invalid Material Slot: %s"), *Token));
			continue;
		}

		int32 Value = FCString::Atoi(*Token);
		if (Value < 0)
		{
			OutErrors.Add(FString::Printf(TEXT("Material Slot must be zero or greater: %s"), *Token));
			continue;
		}
		Values.AddUnique(Value);
	}

	Values.Sort();
	if (Values.Num() == 0)
	{
		OutErrors.Add(TEXT("Enter at least one Material Slot."));
	}
	return Values;
}

FReply SNTERemoveBlurPanel::OnGenerateClicked()
{
	TArray<FString> Errors;
	TArray<int32> MaterialSlots = ParseMaterialSlots(NTERemoveBlurPanel::TextBoxString(MaterialSlotsBox), Errors);
	if (AnimBlueprintPath.IsEmpty())
	{
		Errors.Add(TEXT("Choose an Animation Blueprint."));
	}

	if (Errors.Num() > 0)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(FText::FromString(FString::Join(Errors, TEXT("\n"))));
		}
		return FReply::Handled();
	}

	FNTERemoveBlurGeneratorParams Params;
	Params.AnimBlueprintPath = AnimBlueprintPath;
	Params.MaterialElementIndices = MoveTemp(MaterialSlots);
	Params.ScalarOverrides = FNTERemoveBlurGenerator::MakeDefaultScalarOverrides();
	Params.bGenerateInitializeGraph = NTERemoveBlurPanel::IsChecked(InitGraphCheckBox);
	Params.bGenerateUpdateGraph = NTERemoveBlurPanel::IsChecked(UpdateGraphCheckBox);
	Params.bSaveAssets = NTERemoveBlurPanel::IsChecked(SaveAssetsCheckBox);

	FNTERemoveBlurGeneratorResult Result = FNTERemoveBlurGenerator::Generate(Params);
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(Result.ToDisplayString()));
	}
	SaveSettings();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
