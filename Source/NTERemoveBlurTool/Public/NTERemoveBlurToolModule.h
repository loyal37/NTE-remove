#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FNTERemoveBlurToolModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void OpenPluginWindow();

private:
	TSharedRef<class SDockTab> SpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);
	void RegisterMenus();

	static const FName ToolTabName;
};
