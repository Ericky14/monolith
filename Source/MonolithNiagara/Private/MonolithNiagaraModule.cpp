#include "MonolithNiagaraModule.h"
#include "MonolithJsonUtils.h"
#include "MonolithNiagaraActions.h"
#include "MonolithToolRegistry.h"

#define LOCTEXT_NAMESPACE "FMonolithNiagaraModule"

void FMonolithNiagaraModule::StartupModule()
{
	FMonolithNiagaraActions::RegisterActions(FMonolithToolRegistry::Get());
	UE_LOG(LogMonolith, Verbose, TEXT("Monolith — Niagara module loaded (41 actions)"));
}

void FMonolithNiagaraModule::ShutdownModule()
{
	FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("niagara"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithNiagaraModule, MonolithNiagara)
