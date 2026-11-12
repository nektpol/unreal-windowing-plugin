// Developed by Neko Creative Technologies

#pragma once

#include "Modules/ModuleManager.h"
#include "Templates/UniquePtr.h"

class FWpWindowLifetimeService;

class FWindowingPluginModule : public IModuleInterface
{
public:
	FWindowingPluginModule();
	~FWindowingPluginModule() override;
	void StartupModule() override;
	void ShutdownModule() override;
	bool SupportsDynamicReloading() override { return false; }

	FWpWindowLifetimeService* GetWindowLifetimeService() const
	{
		return Service.Get();
	}

private:
	TUniquePtr<FWpWindowLifetimeService> Service;
};
