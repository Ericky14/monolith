#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Dom/JsonObject.h"
#include "Containers/Ticker.h"
#include "Misc/CoreDelegates.h"
#include "MonolithUpdateSubsystem.generated.h"

struct FMonolithVersionInfo
{
	FString Current;
	FString Pending;
	bool bStaging = false;

	static FMonolithVersionInfo LoadFromDisk();
	void SaveToDisk() const;
	static FString GetVersionFilePath();
};

UCLASS()
class MONOLITHCORE_API UMonolithUpdateSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Check GitHub for a newer release. Fires notification if found. */
	void CheckForUpdate();

	/** Download and stage an update from the given URL. */
	void DownloadUpdate(const FString& ZipUrl, const FString& Version);

	/** Get the current version info. */
	const FMonolithVersionInfo& GetVersionInfo() const { return VersionInfo; }

	/** Register a pre-exit hook to swap the staged update when the editor closes. */
	void ApplyStagedUpdate();

	// --- Version Helpers ---

	/** Parse semver from a tag like "v1.2.0" → "1.2.0" */
	static FString ParseVersionFromTag(const FString& Tag);

	/** Compare two semver strings. Returns >0 if Remote is newer, 0 if equal, <0 if Current is newer. */
	static int32 CompareVersions(const FString& Current, const FString& Remote);

private:
	/** Bind to FCoreDelegates::OnPreExit so we can swap files after the editor shuts down. */
	void RegisterPreExitSwap();

	/** Called by FCoreDelegates::OnPreExit — writes swap script, updates version.json, launches script. */
	void OnPreExit();

	/** Generate a platform-specific swap script that replaces the plugin directory. Returns true on success. */
	bool WriteSwapScript(const FString& StagingDir, const FString& PluginDir);

	FMonolithVersionInfo VersionInfo;
	FDelegateHandle PreExitHandle;
	FString PendingStagingDir;

	/** Show an editor notification with an Update button and release notes. */
	void ShowUpdateNotification(const FString& NewVersion, const FString& ZipUrl, const FString& ReleaseNotes);

	/** Called when the update zip download completes. */
	void OnDownloadComplete(const FString& Version, bool bSuccess, const TArray<uint8>& Data);

	/** Extract a zip file to a destination directory (platform-appropriate: PowerShell on Windows, tar/unzip on Mac/Linux). */
	bool ExtractZipToDirectory(const FString& ZipPath, const FString& DestDir);

	/** Get the staging directory path. */
	static FString GetStagingPath();

	/** Get the plugin install path. */
	static FString GetPluginPath();

	bool bUpdateInProgress = false;
	FTSTicker::FDelegateHandle UpdateCheckTickerHandle;
};
