// Developed by Neko Creative Technologies

#include "WindowingPlugin.h"
#include "WpWindowLifetimeService.h"

FWindowingPluginModule::FWindowingPluginModule() = default;
FWindowingPluginModule::~FWindowingPluginModule() = default;

void FWindowingPluginModule::StartupModule()
{
	Service = MakeUnique<FWpWindowLifetimeService>();
}

void FWindowingPluginModule::ShutdownModule()
{
	if (Service)
	{
		Service->Shutdown();
		Service.Reset();
	}
}

IMPLEMENT_MODULE(FWindowingPluginModule, WindowingPlugin)