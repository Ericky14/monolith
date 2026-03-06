#include "MonolithIndexModule.h"
#include "MonolithToolRegistry.h"
#include "MonolithSettings.h"
#include "Actions/ProjectSearchAction.h"
#include "Actions/ProjectFindReferencesAction.h"
#include "Actions/ProjectFindByTypeAction.h"
#include "Actions/ProjectGetStatsAction.h"
#include "Actions/ProjectGetAssetDetailsAction.h"

#define LOCTEXT_NAMESPACE "FMonolithIndexModule"

void FMonolithIndexModule::StartupModule()
{
	if (!GetDefault<UMonolithSettings>()->bEnableIndex) return;

	UE_LOG(LogTemp, Log, TEXT("Monolith -- Index module loaded (5 actions, SQLite+FTS5)"));

	FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();

	Registry.RegisterAction(TEXT("project"), FProjectSearchAction::GetName(),
		FProjectSearchAction::GetDescription(),
		FMonolithActionHandler::CreateStatic(&FProjectSearchAction::Execute),
		FProjectSearchAction::GetSchema());

	Registry.RegisterAction(TEXT("project"), FProjectFindReferencesAction::GetName(),
		FProjectFindReferencesAction::GetDescription(),
		FMonolithActionHandler::CreateStatic(&FProjectFindReferencesAction::Execute),
		FProjectFindReferencesAction::GetSchema());

	Registry.RegisterAction(TEXT("project"), FProjectFindByTypeAction::GetName(),
		FProjectFindByTypeAction::GetDescription(),
		FMonolithActionHandler::CreateStatic(&FProjectFindByTypeAction::Execute),
		FProjectFindByTypeAction::GetSchema());

	Registry.RegisterAction(TEXT("project"), FProjectGetStatsAction::GetName(),
		FProjectGetStatsAction::GetDescription(),
		FMonolithActionHandler::CreateStatic(&FProjectGetStatsAction::Execute),
		FProjectGetStatsAction::GetSchema());

	Registry.RegisterAction(TEXT("project"), FProjectGetAssetDetailsAction::GetName(),
		FProjectGetAssetDetailsAction::GetDescription(),
		FMonolithActionHandler::CreateStatic(&FProjectGetAssetDetailsAction::Execute),
		FProjectGetAssetDetailsAction::GetSchema());
}

void FMonolithIndexModule::ShutdownModule()
{
	FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("project"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithIndexModule, MonolithIndex)
