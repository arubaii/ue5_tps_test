// Copyright AR GameSystems 2026. All rights reserved.

#include "NameTBDAITools.h"

#include "FileHelpers.h"
#include "NameTBDAIJson.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "NameTBDAI"

// =====================================================================================================================
// Results and change sets
// =====================================================================================================================

FNameTBDAIToolResult FNameTBDAIToolResult::Ok(const TSharedPtr<FJsonObject>& InData)
{
	FNameTBDAIToolResult Result;
	Result.Data = InData.IsValid() ? InData : MakeShared<FJsonObject>();
	return Result;
}

FNameTBDAIToolResult FNameTBDAIToolResult::Fail(const FString& InError)
{
	FNameTBDAIToolResult Result;
	Result.bOk = false;
	Result.Error = InError;
	return Result;
}

FString FNameTBDAIToolResult::ToText() const
{
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	if (Data.IsValid())
	{
		Out->Values = Data->Values;
	}
	if (!bOk)
	{
		Out->SetStringField(TEXT("error"), Error);
	}
	if (Warnings.Num() > 0)
	{
		Out->SetField(TEXT("warnings"), NameTBDAIJson::Arr(Warnings));
	}
	if (Images.Num() > 0)
	{
		Out->SetNumberField(TEXT("images"), Images.Num());
	}
	if (bOk && Out->Values.Num() == 0)
	{
		Out->SetBoolField(TEXT("ok"), true);
	}
	return NameTBDAIJson::ToString(Out);
}

void FNameTBDAIChangeSet::NoteCreated(UObject* Asset, const FString& Line)
{
	if (!Asset)
	{
		return;
	}
	CreatedAssets.AddUnique(Asset);
	TouchedPackages.AddUnique(Asset->GetOutermost());
	if (!Line.IsEmpty())
	{
		Lines.Add(Line);
	}
	NameTBDAITools::SetLastAsset(Asset);
}

void FNameTBDAIChangeSet::NoteTouched(UObject* Object, const FString& Line)
{
	if (!Object)
	{
		return;
	}
	UPackage* Package = Object->GetOutermost();
	if (Package && Package != GetTransientPackage())
	{
		TouchedPackages.AddUnique(Package);
	}
	if (!Line.IsEmpty() && Lines.Num() < 200)
	{
		Lines.Add(Line);
	}
}

void FNameTBDAIChangeSet::Reset()
{
	CreatedAssets.Reset();
	TouchedPackages.Reset();
	Lines.Reset();
}

int32 FNameTBDAIChangeSet::SaveAll() const
{
	TArray<UPackage*> Packages;
	for (const TWeakObjectPtr<UPackage>& Weak : TouchedPackages)
	{
		if (UPackage* Package = Weak.Get(); Package && Package->IsDirty() && Package != GetTransientPackage())
		{
			Packages.Add(Package);
		}
	}
	if (Packages.Num() == 0)
	{
		return 0;
	}
	UEditorLoadingAndSavingUtils::SavePackages(Packages, /*bOnlyDirty=*/true);
	return Packages.Num();
}

// =====================================================================================================================
// Schemas
// =====================================================================================================================

namespace
{
	TSharedPtr<FJsonObject> MakeField(const FString& Type, const FString& Description)
	{
		TSharedPtr<FJsonObject> Field = MakeShared<FJsonObject>();
		Field->SetStringField(TEXT("type"), Type);
		Field->SetStringField(TEXT("description"), Description);
		return Field;
	}
}

FNameTBDAISchema& FNameTBDAISchema::Str(const FString& Name, const FString& Description, bool bRequired, const TArray<FString>& Enum)
{
	TSharedPtr<FJsonObject> Field = MakeField(TEXT("string"), Description);
	if (Enum.Num() > 0)
	{
		Field->SetField(TEXT("enum"), NameTBDAIJson::Arr(Enum));
	}
	Properties->SetObjectField(Name, Field);
	if (bRequired) Required.Add(Name);
	return *this;
}

FNameTBDAISchema& FNameTBDAISchema::Num(const FString& Name, const FString& Description, bool bRequired)
{
	Properties->SetObjectField(Name, MakeField(TEXT("number"), Description));
	if (bRequired) Required.Add(Name);
	return *this;
}

FNameTBDAISchema& FNameTBDAISchema::Int(const FString& Name, const FString& Description, bool bRequired)
{
	Properties->SetObjectField(Name, MakeField(TEXT("integer"), Description));
	if (bRequired) Required.Add(Name);
	return *this;
}

FNameTBDAISchema& FNameTBDAISchema::Bool(const FString& Name, const FString& Description, bool bRequired)
{
	Properties->SetObjectField(Name, MakeField(TEXT("boolean"), Description));
	if (bRequired) Required.Add(Name);
	return *this;
}

FNameTBDAISchema& FNameTBDAISchema::Obj(const FString& Name, const FString& Description, bool bRequired)
{
	Properties->SetObjectField(Name, MakeField(TEXT("object"), Description));
	if (bRequired) Required.Add(Name);
	return *this;
}

FNameTBDAISchema& FNameTBDAISchema::Arr(const FString& Name, const FString& Description, const FString& ItemType, bool bRequired)
{
	TSharedPtr<FJsonObject> Field = MakeField(TEXT("array"), Description);
	TSharedPtr<FJsonObject> Items = MakeShared<FJsonObject>();
	Items->SetStringField(TEXT("type"), ItemType);
	Field->SetObjectField(TEXT("items"), Items);
	Properties->SetObjectField(Name, Field);
	if (bRequired) Required.Add(Name);
	return *this;
}

TSharedPtr<FJsonObject> FNameTBDAISchema::Build() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Properties);
	if (Required.Num() > 0)
	{
		Schema->SetField(TEXT("required"), NameTBDAIJson::Arr(Required));
	}
	return Schema;
}

// =====================================================================================================================
// The registry
// =====================================================================================================================

namespace NameTBDAITools
{
	namespace
	{
		TArray<FNameTBDAITool>& Registry()
		{
			static TArray<FNameTBDAITool> Tools;
			return Tools;
		}

		TWeakObjectPtr<UObject> LastAsset;

		/** Makes Args match the schema where a model was loose; returns what can't be fixed. */
		FString CheckArgs(const FNameTBDAITool& Tool, const TSharedPtr<FJsonObject>& Args)
		{
			const TSharedPtr<FJsonObject> Properties = NameTBDAIJson::GetObject(Tool.InputSchema, TEXT("properties"));
			TArray<FString> Problems;

			for (const FString& RequiredName : NameTBDAIJson::GetStringArray(Tool.InputSchema, TEXT("required")))
			{
				if (!NameTBDAIJson::Has(Args, RequiredName))
				{
					Problems.Add(FString::Printf(TEXT("'%s' is required"), *RequiredName));
				}
			}

			if (Properties.IsValid())
			{
				for (auto& Pair : Args->Values)
				{
					const TSharedPtr<FJsonObject> Field = NameTBDAIJson::GetObject(Properties, Pair.Key);
					if (!Field.IsValid())
					{
						continue; // Extra fields are ignored rather than refused.
					}
					const FString Type = NameTBDAIJson::GetString(Field, TEXT("type"));
					TSharedPtr<FJsonValue>& Value = Pair.Value;
					if (!Value.IsValid() || Value->IsNull())
					{
						continue;
					}

					if (Type == TEXT("object") && Value->Type == EJson::String)
					{
						// Some models (and Gemini's schema subset) send objects as JSON text.
						if (TSharedPtr<FJsonObject> Parsed = NameTBDAIJson::ParseObject(Value->AsString()))
						{
							Value = MakeShared<FJsonValueObject>(Parsed);
						}
						else
						{
							Problems.Add(FString::Printf(TEXT("'%s' must be an object"), *Pair.Key));
						}
					}
					else if (Type == TEXT("array") && Value->Type == EJson::String)
					{
						TSharedPtr<FJsonValue> Parsed = NameTBDAIJson::ParseValue(Value->AsString());
						if (Parsed.IsValid() && Parsed->Type == EJson::Array)
						{
							Value = Parsed;
						}
						else
						{
							Value = MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ Value });
						}
					}
					else if (Type == TEXT("array") && Value->Type != EJson::Array)
					{
						Value = MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ Value });
					}

					// Lists of objects some models (and Gemini's schema subset) send as JSON text, item by item.
					if (Type == TEXT("array") && Value->Type == EJson::Array
						&& NameTBDAIJson::GetString(NameTBDAIJson::GetObject(Field, TEXT("items")), TEXT("type")) == TEXT("object"))
					{
						TArray<TSharedPtr<FJsonValue>> Items = Value->AsArray();
						for (TSharedPtr<FJsonValue>& Item : Items)
						{
							if (Item.IsValid() && Item->Type == EJson::String)
							{
								if (TSharedPtr<FJsonObject> Parsed = NameTBDAIJson::ParseObject(Item->AsString()))
								{
									Item = MakeShared<FJsonValueObject>(Parsed);
								}
							}
						}
						Value = MakeShared<FJsonValueArray>(Items);
					}
					else if ((Type == TEXT("number") || Type == TEXT("integer")) && Value->Type == EJson::String)
					{
						double Number = 0.0;
						if (LexTryParseString(Number, *Value->AsString()))
						{
							Value = MakeShared<FJsonValueNumber>(Number);
						}
						else
						{
							Problems.Add(FString::Printf(TEXT("'%s' must be a number"), *Pair.Key));
						}
					}
					else if (Type == TEXT("boolean") && Value->Type == EJson::String)
					{
						const FString Text = Value->AsString().ToLower();
						Value = MakeShared<FJsonValueBoolean>(Text == TEXT("true") || Text == TEXT("1") || Text == TEXT("yes"));
					}
					else if (Type == TEXT("string") && (Value->Type == EJson::Number || Value->Type == EJson::Boolean))
					{
						Value = MakeShared<FJsonValueString>(Value->Type == EJson::Number ? FString::SanitizeFloat(Value->AsNumber()) : (Value->AsBool() ? TEXT("true") : TEXT("false")));
					}

					const TArray<FString> Enum = NameTBDAIJson::GetStringArray(Field, TEXT("enum"));
					if (Enum.Num() > 0 && Value->Type == EJson::String)
					{
						const FString Given = NameTBDAIJson::Normalize(Value->AsString());
						const FString* Match = Enum.FindByPredicate([&Given](const FString& Option) { return NameTBDAIJson::Normalize(Option) == Given; });
						if (Match)
						{
							Value = MakeShared<FJsonValueString>(*Match);
						}
						else
						{
							Problems.Add(FString::Printf(TEXT("'%s' must be one of %s"), *Pair.Key, *FString::Join(Enum, TEXT(", "))));
						}
					}
				}
			}
			return FString::Join(Problems, TEXT("; "));
		}
	}

	void Register(FNameTBDAITool&& Tool)
	{
		check(!Find(Tool.Name));
		Registry().Add(MoveTemp(Tool));
	}

	const TArray<FNameTBDAITool>& GetTools()
	{
		return Registry();
	}

	const FNameTBDAITool* Find(const FString& Name)
	{
		return Registry().FindByPredicate([&Name](const FNameTBDAITool& Tool) { return Tool.Name == Name; });
	}

	void RegisterAll()
	{
		if (Registry().Num() > 0)
		{
			return;
		}
		RegisterReadTools();
		RegisterWidgetTools();
		RegisterAnimationTools();
		RegisterMaterialTools();
		RegisterNiagaraTools();
		RegisterDataTools();
		RegisterCaptureTools();
	}

	FNameTBDAIToolResult Execute(const FString& Name, const TSharedPtr<FJsonObject>& InArgs, FNameTBDAIToolContext& Context)
	{
		check(IsInGameThread());
		const FNameTBDAITool* Tool = Find(Name);
		if (!Tool)
		{
			return FNameTBDAIToolResult::Fail(FString::Printf(TEXT("No tool '%s'."), *Name));
		}
		if (Tool->bWrites && !Context.bAllowWrites)
		{
			return FNameTBDAIToolResult::Fail(TEXT("Changes are turned off here (MCP Server > Allow Writes in Editor Preferences > Plugins > NameTBD AI)."));
		}
		if (Tool->bOptIn && !Context.bAllowCapture)
		{
			return FNameTBDAIToolResult::Fail(TEXT("Preview capture is off (Editor Preferences > Plugins > NameTBD AI > Allow Preview Capture). Describe the widget instead."));
		}

		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		if (InArgs.IsValid())
		{
			Args->Values = InArgs->Values;
		}
		const FString Problems = CheckArgs(*Tool, Args);
		if (!Problems.IsEmpty())
		{
			return FNameTBDAIToolResult::Fail(FString::Printf(TEXT("Bad input for %s: %s."), *Name, *Problems));
		}

		FNameTBDAIChangeSet LocalChanges;
		if (!Context.Changes)
		{
			Context.Changes = &LocalChanges;
		}

		FNameTBDAIToolResult Result;
		if (Tool->bWrites)
		{
			// One undo step - or, inside a chat request's own open transaction, part of the request's one step.
			const FScopedTransaction Transaction(FText::Format(LOCTEXT("ToolTransaction", "NameTBD AI: {0}"), FText::FromString(Name)));
			Result = Tool->Run(Args, Context);
		}
		else
		{
			Result = Tool->Run(Args, Context);
		}

		if (Context.Changes == &LocalChanges)
		{
			Context.Changes = nullptr;
		}
		return Result;
	}

	void SetLastAsset(UObject* Asset)
	{
		LastAsset = Asset;
	}

	UObject* GetLastAsset()
	{
		return LastAsset.Get();
	}
}

#undef LOCTEXT_NAMESPACE
