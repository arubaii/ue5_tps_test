// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimTrackPresets.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimTrackPresets, Log, All);

#define LOCTEXT_NAMESPACE "WidgetAnimTrackPresets"

namespace WidgetAnimTrackPresets
{
	namespace
	{
		/**
		 * A preset file: this header, a tab and "Collapsed" when it came from a collapsed track; then one line per track - its
		 * start in authored seconds after the first's, a tab, and the track as exported text.
		 */
		const TCHAR* Header = TEXT("NameTBD.WidgetAnimTrackPreset.v2");
		const TCHAR* CollapsedFlag = TEXT("Collapsed");
		const TCHAR* Extension = TEXT(".watrack");

		/** Layer with everything that says where it is, and how the panel shows it, put back to a new track's - its name kept. */
		FWidgetAnimLayer WithoutPlacement(const FWidgetAnimLayer& Layer)
		{
			FWidgetAnimLayer Out = Layer;
			const FWidgetAnimLayer Fresh;
			Out.Start = Fresh.Start;
			Out.Delay = Fresh.Delay;
			Out.LayerGroup = Fresh.LayerGroup;
			Out.TargetWidget = Fresh.TargetWidget;
			Out.InsertedAnimation = nullptr;
			Out.InsertedGroups.Reset();
#if WITH_EDITORONLY_DATA
			Out.bLockedInEditor = Fresh.bLockedInEditor;
			Out.EditorLine = Fresh.EditorLine;
			Out.bHiddenInEditor = Fresh.bHiddenInEditor;
			Out.bHiddenByFocus = Fresh.bHiddenByFocus;
			Out.InsertedFrom = Fresh.InsertedFrom;
			Out.bPinnedInEditor = Fresh.bPinnedInEditor;
			Out.CollapsedTrack = Fresh.CollapsedTrack;
			Out.bMutedByEditorScope = Fresh.bMutedByEditorScope;
#endif
			return Out;
		}

		bool Read(const FString& File, FPreset& OutPreset)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *File))
			{
				return false;
			}

			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines);
			if (Lines.Num() < 2)
			{
				return false;
			}

			TArray<FString> HeaderFields;
			Lines[0].TrimStartAndEnd().ParseIntoArray(HeaderFields, TEXT("\t"));
			if (HeaderFields.Num() == 0 || HeaderFields[0] != Header)
			{
				return false;
			}
			OutPreset.bCollapsed = HeaderFields.Contains(CollapsedFlag);

			for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
			{
				const FString Line = Lines[LineIndex].TrimStartAndEnd();
				int32 Tab = INDEX_NONE;
				if (Line.IsEmpty() || !Line.FindChar(TEXT('\t'), Tab))
				{
					continue;
				}

				FWidgetAnimLayer Layer;
				const FString Exported = Line.Mid(Tab + 1);
				if (!FWidgetAnimLayer::StaticStruct()->ImportText(*Exported, &Layer, nullptr, PPF_Copy, GWarn, TEXT("WidgetAnimTrackPreset")))
				{
					return false;
				}
				OutPreset.Starts.Add(FMath::Max(FCString::Atof(*Line.Left(Tab)), 0.f));
				OutPreset.Layers.Add(MoveTemp(Layer));
			}
			return OutPreset.Layers.Num() > 0;
		}
	}

	FString GetFolder(bool bCreate)
	{
		const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("WidgetAnimTrackPresets"));
		if (bCreate && !IFileManager::Get().DirectoryExists(*Folder))
		{
			IFileManager::Get().MakeDirectory(*Folder, /*Tree=*/true);
		}
		return Folder;
	}

	FString GetPresetFile(const FString& Name)
	{
		const FString FileName = FPaths::MakeValidFileName(Name.TrimStartAndEnd(), TEXT('_'));
		return FileName.IsEmpty() ? FString() : GetFolder() / (FileName + Extension);
	}

	void GetPresets(TArray<FPreset>& OutPresets)
	{
		OutPresets.Reset();

		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(GetFolder() / (FString(TEXT("*")) + Extension)), /*Files=*/true, /*Directories=*/false);
		for (const FString& FileName : Files)
		{
			FPreset Preset;
			Preset.File = GetFolder() / FileName;
			Preset.Name = FPaths::GetBaseFilename(FileName);
			if (Read(Preset.File, Preset))
			{
				OutPresets.Add(MoveTemp(Preset));
			}
			else
			{
				UE_LOG(LogWidgetAnimTrackPresets, Warning, TEXT("Track preset %s couldn't be read, so it isn't listed."), *Preset.File);
			}
		}

		OutPresets.Sort([](const FPreset& A, const FPreset& B) { return A.Name < B.Name; });
	}

	bool CanSave(const FWidgetAnimLayer& Layer)
	{
		return !Layer.IsAnimationTrack();
	}

	bool Save(const FString& Name, const TArray<FWidgetAnimLayer>& Layers, const TArray<float>& Starts, bool bCollapsed, FText& OutError)
	{
		if (Layers.Num() == 0 || Layers.Num() != Starts.Num())
		{
			OutError = LOCTEXT("PresetNoTracks", "There are no tracks to save as a preset.");
			return false;
		}
		for (const FWidgetAnimLayer& Layer : Layers)
		{
			if (!CanSave(Layer))
			{
				OutError = LOCTEXT("AnimationTrackPreset", "A track that plays another animation can't be in a preset: its settings are that animation's.");
				return false;
			}
		}

		const FString File = GetPresetFile(Name);
		if (File.IsEmpty())
		{
			OutError = LOCTEXT("PresetNameEmpty", "Rename the track first: a preset is saved under its name.");
			return false;
		}
		GetFolder(/*bCreate=*/true);

		// Starts after the first's, so the set lands wherever it is added.
		float FirstStart = TNumericLimits<float>::Max();
		for (const float Start : Starts)
		{
			FirstStart = FMath::Min(FirstStart, Start);
		}

		FString Text = Header;
		if (bCollapsed)
		{
			Text += FString(TEXT("\t")) + CollapsedFlag;
		}
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{
			const FWidgetAnimLayer Clean = WithoutPlacement(Layers[Index]);
			FString Exported;
			FWidgetAnimLayer::StaticStruct()->ExportText(Exported, &Clean, nullptr, nullptr, PPF_Copy, nullptr);
			Text += FString::Printf(TEXT("\n%f\t%s"), Starts[Index] - FirstStart, *Exported);
		}
		Text += TEXT("\n");

		if (!FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FText::Format(LOCTEXT("PresetWriteFailed", "Couldn't write {0}. Is it read-only, or checked in?"), FText::FromString(File));
			return false;
		}
		return true;
	}

	bool Delete(const FPreset& Preset)
	{
		return IFileManager::Get().Delete(*Preset.File, /*RequireExists=*/false, /*EvenReadOnly=*/true);
	}

	void ApplyTo(const FWidgetAnimLayer& Preset, FWidgetAnimLayer& Target)
	{
		FWidgetAnimLayer Out = WithoutPlacement(Preset);

		// Where the track is, what it is called and how the panel shows it stay the track's.
		Out.Start = Target.Start;
		Out.Delay = Target.Delay;
		Out.LayerGroup = Target.LayerGroup;
		Out.TargetWidget = Target.TargetWidget;
#if WITH_EDITORONLY_DATA
		Out.bLockedInEditor = Target.bLockedInEditor;
		Out.EditorLine = Target.EditorLine;
		Out.bHiddenInEditor = Target.bHiddenInEditor;
		Out.bHiddenByFocus = Target.bHiddenByFocus;
		Out.InsertedFrom = Target.InsertedFrom;
		Out.bPinnedInEditor = Target.bPinnedInEditor;
		Out.CollapsedTrack = Target.CollapsedTrack;
		Out.TrackName = Target.TrackName;
		Out.bMutedByEditorScope = Target.bMutedByEditorScope;
#endif

		// A value-driven track keeps the Value Name gameplay already sets it by.
		if (!Target.ValueName.IsNone())
		{
			Out.ValueName = Target.ValueName;
		}

		Target = MoveTemp(Out);
	}
}

#undef LOCTEXT_NAMESPACE
