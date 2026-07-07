#include "NTERemoveBlurToolModule.h"

#include "SNTERemoveBlurPanel.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FNTERemoveBlurToolModule"

const FName FNTERemoveBlurToolModule::ToolTabName(TEXT("NTERemoveBlurTool"));

void FNTERemoveBlurToolModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ToolTabName,
		FOnSpawnTab::CreateRaw(this, &FNTERemoveBlurToolModule::SpawnPluginTab))
		.SetDisplayName(LOCTEXT("TabTitle", "NTE Remove Blur Tool"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FNTERemoveBlurToolModule::RegisterMenus));
}

void FNTERemoveBlurToolModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ToolTabName);
}

void FNTERemoveBlurToolModule::OpenPluginWindow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(ToolTabName);
}

TSharedRef<SDockTab> FNTERemoveBlurToolModule::SpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SNTERemoveBlurPanel)
		];
}

void FNTERemoveBlurToolModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = Menu->FindOrAddSection("NTE");
	Section.AddMenuEntry(
		"OpenNTERemoveBlurTool",
		LOCTEXT("MenuEntryTitle", "NTE Remove Blur Tool"),
		LOCTEXT("MenuEntryTooltip", "Generate Animation Blueprint nodes that remove material dither fade from selected slots."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FNTERemoveBlurToolModule::OpenPluginWindow)));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNTERemoveBlurToolModule, NTERemoveBlurTool)
