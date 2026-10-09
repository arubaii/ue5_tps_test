// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetShapePresets.h"

#include "NameTBD/Widgets/Shapes/ShapeWidget.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetShapePresets, Log, All);

#define LOCTEXT_NAMESPACE "WidgetShapePresets"

namespace WidgetShapePresetsImpl
{
	/** A preset file: this header, a tab and the shape class's path; then one line per setting - its name, a tab, its value. */
	const TCHAR* Header = TEXT("NameTBD.WidgetShapePreset.v1");
	const TCHAR* Extension = TEXT(".wshape");

	bool Read(const FString& File, const UClass* ShapeClass, WidgetShapePresets::FPreset& OutPreset)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			return false;
		}

		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		if (Lines.Num() == 0)
		{
			return false;
		}

		TArray<FString> HeaderFields;
		Lines[0].TrimStartAndEnd().ParseIntoArray(HeaderFields, TEXT("\t"));
		if (HeaderFields.Num() < 2 || HeaderFields[0] != Header || HeaderFields[1] != ShapeClass->GetPathName())
		{
			return false;
		}

		for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString Line = Lines[LineIndex].TrimStart();
			int32 Tab = INDEX_NONE;
			if (Line.IsEmpty() || !Line.FindChar(TEXT('\t'), Tab))
			{
				continue;
			}
			OutPreset.Values.Add(TPair<FName, FString>(FName(*Line.Left(Tab)), Line.Mid(Tab + 1)));
		}
		return true;
	}
}

namespace WidgetShapePresets
{
	FText GetKindName(const UClass* ShapeClass)
	{
		return ShapeClass ? ShapeClass->GetDisplayNameText() : FText::GetEmpty();
	}

	FString GetFolder(const UClass* ShapeClass, bool bCreate)
	{
		// The class's own name, not its display name, so renaming how a shape is shown doesn't lose its presets.
		const FString Kind = ShapeClass ? ShapeClass->GetName() : FString(TEXT("Shape"));
		const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("WidgetShapePresets") / Kind);
		if (bCreate && !IFileManager::Get().DirectoryExists(*Folder))
		{
			IFileManager::Get().MakeDirectory(*Folder, /*Tree=*/true);
		}
		return Folder;
	}

	FString GetPresetFile(const UClass* ShapeClass, const FString& Name)
	{
		const FString FileName = FPaths::MakeValidFileName(Name.TrimStartAndEnd(), TEXT('_'));
		return FileName.IsEmpty() ? FString() : GetFolder(ShapeClass) / (FileName + WidgetShapePresetsImpl::Extension);
	}

	void GetPresets(const UClass* ShapeClass, TArray<FPreset>& OutPresets)
	{
		OutPresets.Reset();
		if (!ShapeClass)
		{
			return;
		}

		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(GetFolder(ShapeClass) / (FString(TEXT("*")) + WidgetShapePresetsImpl::Extension)), /*Files=*/true, /*Directories=*/false);
		for (const FString& FileName : Files)
		{
			FPreset Preset;
			Preset.File = GetFolder(ShapeClass) / FileName;
			Preset.Name = FPaths::GetBaseFilename(FileName);
			if (WidgetShapePresetsImpl::Read(Preset.File, ShapeClass, Preset))
			{
				OutPresets.Add(MoveTemp(Preset));
			}
			else
			{
				UE_LOG(LogWidgetShapePresets, Warning, TEXT("Shape preset %s couldn't be read, so it isn't listed."), *Preset.File);
			}
		}
		OutPresets.Sort([](const FPreset& A, const FPreset& B) { return A.Name < B.Name; });
	}

	bool IsLookProperty(const FProperty* Property)
	{
		static const FName Look[] =
		{
			GET_MEMBER_NAME_CHECKED(UShapeWidget, bFill),
			GET_MEMBER_NAME_CHECKED(UShapeWidget, Fill),
			GET_MEMBER_NAME_CHECKED(UShapeWidget, Stroke),
			GET_MEMBER_NAME_CHECKED(UShapeWidget, Trim)
		};
		for (const FName& Name : Look)
		{
			if (Property && Property->GetFName() == Name)
			{
				return true;
			}
		}
		return false;
	}

	void GetPresetProperties(const UClass* ShapeClass, bool bOutlineOnly, TArray<FProperty*>& OutProperties)
	{
		OutProperties.Reset();
		if (!ShapeClass)
		{
			return;
		}
		const FName DesiredSize = GET_MEMBER_NAME_CHECKED(UShapeWidget, DesiredSize);
		for (TFieldIterator<FProperty> It(ShapeClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			const UClass* Owner = Property->GetOwnerClass();
			if (!Owner || !Owner->IsChildOf(UShapeWidget::StaticClass()))
			{
				continue;
			}
			if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
			{
				continue;
			}
			if (Property->GetFName() == DesiredSize || UShapeWidget::IsContentProperty(Property) || (bOutlineOnly && IsLookProperty(Property)))
			{
				continue;
			}
			OutProperties.Add(Property);
		}
	}

	void Capture(const UObject* Shape, TArray<TPair<FName, FString>>& OutValues)
	{
		OutValues.Reset();
		if (!Shape)
		{
			return;
		}
		TArray<FProperty*> Properties;
		GetPresetProperties(Shape->GetClass(), /*bOutlineOnly=*/false, Properties);
		for (const FProperty* Property : Properties)
		{
			FString Value;
			Property->ExportTextItem_InContainer(Value, Shape, nullptr, nullptr, PPF_None);
			OutValues.Add(TPair<FName, FString>(Property->GetFName(), Value));
		}
	}

	bool Save(const UClass* ShapeClass, const FString& Name, const TArray<TPair<FName, FString>>& Values, FText& OutError)
	{
		if (!ShapeClass || Values.Num() == 0)
		{
			OutError = LOCTEXT("NothingToSave", "There is no shape selected to save as a preset.");
			return false;
		}
		const FString File = GetPresetFile(ShapeClass, Name);
		if (File.IsEmpty())
		{
			OutError = LOCTEXT("NoName", "Type a name for the preset first.");
			return false;
		}
		GetFolder(ShapeClass, /*bCreate=*/true);

		FString Text = FString::Printf(TEXT("%s\t%s\n"), WidgetShapePresetsImpl::Header, *ShapeClass->GetPathName());
		for (const TPair<FName, FString>& Value : Values)
		{
			// Exported text is one line; a stray line break would cut a value in two, so none is kept.
			FString OneLine = Value.Value;
			OneLine.ReplaceInline(TEXT("\r"), TEXT(" "));
			OneLine.ReplaceInline(TEXT("\n"), TEXT(" "));
			Text += Value.Key.ToString() + TEXT("\t") + OneLine + TEXT("\n");
		}

		if (!FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FText::Format(LOCTEXT("WriteFailed", "Couldn't write {0}. Is it read-only, or checked in?"), FText::FromString(File));
			return false;
		}
		return true;
	}

	bool Delete(const FPreset& Preset)
	{
		return IFileManager::Get().Delete(*Preset.File, /*RequireExists=*/false, /*EvenReadOnly=*/true);
	}
}

#undef LOCTEXT_NAMESPACE
