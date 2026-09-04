// AETHERKIN — MonolithDataflow module.

#include "Modules/ModuleManager.h"
#include "MonolithDataflow/MonolithAppendMeshNode.h"

class FMonolithDataflowModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Node factory registration must happen at module startup — Dataflow builds
		// its creation menu from the factory, so a late registration means the node
		// never appears in the right-click list.
		UE::MonolithDataflow::RegisterMonolithDataflowNodes();
	}
};

IMPLEMENT_MODULE(FMonolithDataflowModule, MonolithDataflow)
