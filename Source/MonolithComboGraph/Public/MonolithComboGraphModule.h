#pragma once

#include "Modules/ModuleInterface.h"

class FMonolithComboGraphModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
