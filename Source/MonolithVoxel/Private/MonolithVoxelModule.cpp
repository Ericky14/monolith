#include "MonolithVoxelModule.h"
#include "MonolithVoxelActions.h"
#include "MonolithToolRegistry.h"
#include "MonolithJsonUtils.h"
#include "MonolithSettings.h"

#define LOCTEXT_NAMESPACE "FMonolithVoxelModule"

void FMonolithVoxelModule::StartupModule()
{
    if (!GetDefault<UMonolithSettings>()->bEnableVoxel)
        return;

    FMonolithVoxelActions::RegisterActions();
    UE_LOG(LogMonolith, Log, TEXT("Monolith — Voxel module loaded (7 actions)"));
}

void FMonolithVoxelModule::ShutdownModule()
{
    FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("voxel"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithVoxelModule, MonolithVoxel)
