// Copyright tumourlove. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class FMonolithToolRegistry;

namespace MonolithUI
{
    /**
     * FPieWidgetInspectorActions — read the LIVE widget tree in PIE, with values resolved.
     *
     * WHY THIS EXISTS. Every other UI action in Monolith inspects or edits the widget BLUEPRINT: the
     * graph, the authored tree, the asset defaults. None of them can answer the only question that
     * matters when a HUD looks wrong — "what is actually on screen right now?" The gap is not
     * academic: a slot can hold perfectly correct backing data and still render blank because the
     * brush it resolved to was null, and inspecting the data or the graph shows nothing amiss. That
     * failure was diagnosed twice from the data side and reported as working, both times, before
     * anyone looked at the rendered result.
     *
     * So this walks the instantiated UUserWidget tree in the PIE world and reports each widget's
     * RESOLVED state — the texture a UImage's brush actually points at, the string a UTextBlock
     * actually holds, the percent a UProgressBar actually shows — alongside visibility, which is the
     * other half of "why can't I see it".
     */
    class FPieWidgetInspectorActions
    {
    public:
        static void Register(FMonolithToolRegistry& Registry);
    };
}
