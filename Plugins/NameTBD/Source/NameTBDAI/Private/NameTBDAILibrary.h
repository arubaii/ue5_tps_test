// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UMaterial;
class UMaterialFunction;
class UMaterialInterface;
class UNiagaraSystem;
struct FNameTBDAIToolContext;

/**
 * The library the AI builds with: master UI materials (aurora, soft glow, shimmer, gradient, noise fog, vignette, ring
 * pulse, dissolve, soft dot), Material Functions (soft circle, rounded-rect SDF, panning noise, color ramp) and Niagara UI
 * templates (rising bubbles, sparkles, confetti, embers, glow ring, trail, rising sparkle loop).
 *
 * Each is defined as data in the plugin's Resources/AI (Materials/*.json - node graphs; Niagara/Templates.json - recipes),
 * and built into a real asset the first time it is needed: looked for first in the plugin's own content (/NameTBD/AI/...,
 * where a release can ship them built), then in the project's Library Folder, else built there. Materials and functions are
 * built node by node with the material editing library (the same graph format create_ui_material takes), so the shipped
 * definitions and the AI's own materials go through one path.
 */
namespace NameTBDAILibrary
{
	/** Names and one-line descriptions, with each master's parameters. */
	TSharedPtr<FJsonObject> DescribeMasters();
	TSharedPtr<FJsonObject> DescribeFunctions();
	TSharedPtr<FJsonObject> DescribeNiagaraTemplates();

	/** A master material by name ("Aurora", "M_UI_Aurora"), built if needed. Null with OutError. */
	UMaterialInterface* GetMaster(const FString& Name, FString& OutError);

	/** A library Material Function by name, built if needed. */
	UMaterialFunction* GetFunction(const FString& Name, FString& OutError);

	/** A Niagara UI template system by name, built if needed. */
	UNiagaraSystem* GetNiagaraTemplate(const FString& Name, FString& OutError, TArray<FString>& OutNotes);

	/** The template's recipe from Templates.json, or null. */
	TSharedPtr<FJsonObject> FindNiagaraRecipe(const FString& Name);

	/** Builds every library asset that isn't there yet. One line per asset built or failed. */
	void BuildAll(TArray<FString>& OutReport);

	/**
	 * Builds a UI material from a graph (see create_ui_material's description for the format) into Material, which is emptied
	 * first. Compile errors and graph problems go to OutProblems; true when it compiled.
	 */
	bool BuildMaterialGraph(UMaterial* Material, const TSharedPtr<FJsonObject>& Graph, TArray<FString>& OutProblems);

	/** The same for a Material Function: "inputs" / "outputs" become Function Input / Output nodes. */
	bool BuildFunctionGraph(UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Graph, TArray<FString>& OutProblems);

	/** Writes module constants ("SpawnRate.SpawnRate": 12) into a system's scripts; notes which took. True if any did. */
	bool ApplyNiagaraConstants(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Constants, TArray<FString>& OutNotes);

	/** CPU simulation, local space, Material on sprite renderers that don't draw in UI. */
	void MakeUIReady(UNiagaraSystem* System, UMaterialInterface* Material, TArray<FString>& OutNotes);

	/** The UI soft-dot material particles draw with when theirs isn't a UI material. */
	UMaterialInterface* GetSoftDotMaterial(FString& OutError);
}
