// Copyright tumourlove. All Rights Reserved.
#include "Actions/Hoisted/PieWidgetInspectorActions.h"

// Monolith registry
#include "MonolithToolRegistry.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// UMG runtime
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Border.h"
#include "Components/NamedSlot.h"

// Engine / editor
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

namespace MonolithUI::PieWidgetInspectorInternal
{
    /** The running PIE world, or null when PIE is not active. */
    static UWorld* FindPieWorld()
    {
        if (!GEditor)
        {
            return nullptr;
        }

        for (const FWorldContext& Context : GEditor->GetWorldContexts())
        {
            if (Context.WorldType == EWorldType::PIE && Context.World())
            {
                return Context.World();
            }
        }
        return nullptr;
    }

    static FString VisibilityToString(const ESlateVisibility Vis)
    {
        switch (Vis)
        {
        case ESlateVisibility::Visible:               return TEXT("Visible");
        case ESlateVisibility::Collapsed:             return TEXT("Collapsed");
        case ESlateVisibility::Hidden:                return TEXT("Hidden");
        case ESlateVisibility::HitTestInvisible:      return TEXT("HitTestInvisible");
        case ESlateVisibility::SelfHitTestInvisible:  return TEXT("SelfHitTestInvisible");
        default:                                      return TEXT("Unknown");
        }
    }

    /**
     * Would this widget actually draw? Collapsed/Hidden do not, and NEITHER DOES a visible widget
     * inside a collapsed parent — which is the case that makes "the data is right but I see nothing"
     * so hard to spot from a single widget's own properties.
     */
    static bool IsEffectivelyVisible(const UWidget* Widget)
    {
        for (const UWidget* W = Widget; W; W = W->GetParent())
        {
            const ESlateVisibility Vis = W->GetVisibility();
            if (Vis == ESlateVisibility::Collapsed || Vis == ESlateVisibility::Hidden)
            {
                return false;
            }
        }
        return true;
    }

    /** Type-specific RESOLVED values — the part a graph dump cannot tell you. */
    static void AppendResolvedState(const UWidget* Widget, const TSharedPtr<FJsonObject>& Out)
    {
        if (const UImage* Image = Cast<UImage>(Widget))
        {
            const UObject* Resource = Image->GetBrush().GetResourceObject();

            // The single most valuable field in this whole action: a null brush resource is exactly
            // how a slot renders empty while its backing data looks perfect.
            Out->SetStringField(TEXT("brush_resource"),
                Resource ? Resource->GetName() : TEXT("None"));
            Out->SetStringField(TEXT("brush_tint"), Image->GetBrush().TintColor.GetSpecifiedColor().ToString());
            return;
        }

        if (const UTextBlock* Text = Cast<UTextBlock>(Widget))
        {
            Out->SetStringField(TEXT("text"), Text->GetText().ToString());
            return;
        }

        if (const UProgressBar* Bar = Cast<UProgressBar>(Widget))
        {
            Out->SetNumberField(TEXT("percent"), Bar->GetPercent());
            return;
        }

        if (const UBorder* Border = Cast<UBorder>(Widget))
        {
            Out->SetStringField(TEXT("brush_color"), Border->GetBrushColor().ToString());
        }
    }

    static void WalkWidget(
        UWidget* Widget,
        const int32 Depth,
        const int32 MaxDepth,
        const FString& Filter,
        TArray<TSharedPtr<FJsonValue>>& OutRows);

    /** A UUserWidget owns its own WidgetTree, so recursion has to hop into it explicitly. */
    static void WalkUserWidget(
        UUserWidget* User,
        const int32 Depth,
        const int32 MaxDepth,
        const FString& Filter,
        TArray<TSharedPtr<FJsonValue>>& OutRows)
    {
        if (!User || !User->WidgetTree || Depth > MaxDepth)
        {
            return;
        }

        if (UWidget* Root = User->WidgetTree->RootWidget)
        {
            WalkWidget(Root, Depth, MaxDepth, Filter, OutRows);
        }
    }

    static void WalkWidget(
        UWidget* Widget,
        const int32 Depth,
        const int32 MaxDepth,
        const FString& Filter,
        TArray<TSharedPtr<FJsonValue>>& OutRows)
    {
        if (!Widget || Depth > MaxDepth)
        {
            return;
        }

        const FString Name = Widget->GetName();
        const bool bPassesFilter = Filter.IsEmpty() || Name.Contains(Filter);

        if (bPassesFilter)
        {
            TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
            Row->SetNumberField(TEXT("depth"), Depth);
            Row->SetStringField(TEXT("name"), Name);
            Row->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
            Row->SetStringField(TEXT("visibility"), VisibilityToString(Widget->GetVisibility()));

            // Reported separately from `visibility` on purpose: a widget can be Visible and still
            // draw nothing because an ancestor is Collapsed.
            Row->SetBoolField(TEXT("effectively_visible"), IsEffectivelyVisible(Widget));

            AppendResolvedState(Widget, Row);
            OutRows.Add(MakeShared<FJsonValueObject>(Row));
        }

        // Descend. Both branches matter: panels hold children directly, while a nested UserWidget
        // hides its subtree behind its own WidgetTree.
        if (UUserWidget* AsUser = Cast<UUserWidget>(Widget))
        {
            WalkUserWidget(AsUser, Depth + 1, MaxDepth, Filter, OutRows);
            return;
        }

        if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
        {
            const int32 Count = Panel->GetChildrenCount();
            for (int32 i = 0; i < Count; ++i)
            {
                WalkWidget(Panel->GetChildAt(i), Depth + 1, MaxDepth, Filter, OutRows);
            }
        }
    }
}

static FMonolithActionResult HandleDumpPieWidgetState(const TSharedPtr<FJsonObject>& Params)
{
    using namespace MonolithUI::PieWidgetInspectorInternal;

    UWorld* World = FindPieWorld();
    if (!World)
    {
        return FMonolithActionResult::Error(
            TEXT("No PIE world. Start PIE first — this action reads LIVE widgets, not the asset."));
    }

    FString RootFilter;
    Params->TryGetStringField(TEXT("root"), RootFilter);

    FString Filter;
    Params->TryGetStringField(TEXT("filter"), Filter);

    int32 MaxDepth = 8;
    if (double DepthNum = 0.0; Params->TryGetNumberField(TEXT("depth"), DepthNum))
    {
        MaxDepth = FMath::Clamp(static_cast<int32>(DepthNum), 1, 32);
    }

    // Roots = user widgets actually added to the viewport in THIS world. Anything else is either a
    // sub-widget (reached by recursion) or a CDO/editor-preview object that would only add noise.
    TArray<UUserWidget*> Roots;
    for (TObjectIterator<UUserWidget> It; It; ++It)
    {
        UUserWidget* Candidate = *It;
        if (!Candidate || Candidate->HasAnyFlags(RF_ClassDefaultObject))
        {
            continue;
        }
        if (Candidate->GetWorld() != World || !Candidate->IsInViewport())
        {
            continue;
        }
        if (!RootFilter.IsEmpty() &&
            !Candidate->GetName().Contains(RootFilter) &&
            !Candidate->GetClass()->GetName().Contains(RootFilter))
        {
            continue;
        }
        Roots.Add(Candidate);
    }

    TArray<TSharedPtr<FJsonValue>> Widgets;
    TArray<TSharedPtr<FJsonValue>> RootNames;
    for (UUserWidget* Root : Roots)
    {
        RootNames.Add(MakeShared<FJsonValueString>(
            FString::Printf(TEXT("%s (%s) [%s]"),
                *Root->GetName(),
                *Root->GetClass()->GetName(),
                *VisibilityToString(Root->GetVisibility()))));

        // The root itself is a widget worth reporting — it is very often the thing that is collapsed.
        WalkWidget(Root, 0, MaxDepth, Filter, Widgets);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("world"), World->GetName());
    Result->SetNumberField(TEXT("root_count"), Roots.Num());
    Result->SetArrayField(TEXT("roots"), RootNames);
    Result->SetNumberField(TEXT("widget_count"), Widgets.Num());
    Result->SetArrayField(TEXT("widgets"), Widgets);

    if (Roots.Num() == 0)
    {
        Result->SetStringField(TEXT("hint"),
            TEXT("No viewport widgets matched. Drop the 'root' filter to list everything, and note "
                 "that a widget only counts as a root here once AddToViewport has run."));
    }

    return FMonolithActionResult::Success(Result);
}

void MonolithUI::FPieWidgetInspectorActions::Register(FMonolithToolRegistry& Registry)
{
    Registry.RegisterAction(
        TEXT("ui"),
        TEXT("dump_pie_widget_state"),
        TEXT("Walk the LIVE widget tree in PIE and report each widget's RESOLVED state — the texture "
             "a UImage's brush actually points at (brush_resource, 'None' when null), the string in a "
             "UTextBlock, a UProgressBar's percent, a UBorder's colour — plus visibility AND "
             "effectively_visible (false when any ancestor is Collapsed/Hidden). Use this when the "
             "backing data looks correct but the HUD does not: inspecting the widget Blueprint or the "
             "source data cannot show a null brush or a collapsed parent. Requires PIE to be running. "
             "Params: root (string, optional — substring of a viewport widget's name or class), "
             "filter (string, optional — substring of a widget name), depth (int, optional, default 8)."),
        FMonolithActionHandler::CreateStatic(&HandleDumpPieWidgetState));
}
