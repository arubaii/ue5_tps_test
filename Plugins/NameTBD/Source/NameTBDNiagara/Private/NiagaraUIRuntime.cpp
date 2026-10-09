// Copyright AR GameSystems 2026. All rights reserved.

#include "NiagaraUIRuntime.h"

#include "NiagaraUILog.h"
#include "NiagaraUIWidget.h"

#include "Containers/Ticker.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Styling/SlateBrush.h"
#include "TextureResource.h"
#include "UObject/GCObject.h"

namespace NiagaraUIRuntime
{
	namespace
	{
		TAutoConsoleVariable<int32> CVarEnable(
			TEXT("NiagaraUI.Enable"),
			1,
			TEXT("0 stops every Niagara Particles widget simulating and drawing, for profiling. 1 (the default) runs them."));

		TAutoConsoleVariable<int32> CVarLog(
			TEXT("NiagaraUI.Log"),
			0,
			TEXT("1 logs the Niagara Particles widgets' simulations as they are made, let go, paused and resumed (LogNiagaraUI)."));

		/** Seconds a brush nothing has drawn with is kept before it is let go - and its material with it. */
		constexpr double BrushKeepSeconds = 10.0;

		/** Edge of the soft dot texture, in pixels. Big enough to stay soft at the sizes particles are drawn in UI. */
		constexpr int32 DotSize = 64;

		/**
		 * The shared state. An FGCObject: it keeps the materials it has brushes for, and the dot texture, from being
		 * collected while a brush may still draw with them.
		 */
		class FRuntime : public FGCObject
		{
		public:
			FRuntime()
			{
				TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FRuntime::Tick));
				WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FRuntime::HandleWorldCleanup);
			}

			virtual ~FRuntime() override
			{
				FTSTicker::RemoveTicker(TickerHandle);
				FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
			}

			virtual void AddReferencedObjects(FReferenceCollector& Collector) override
			{
				for (TPair<const UMaterialInterface*, FBrushEntry>& Pair : MaterialBrushes)
				{
					Collector.AddReferencedObject(Pair.Value.Material);
				}
				Collector.AddReferencedObject(DotTexture);
			}

			virtual FString GetReferencerName() const override
			{
				return TEXT("NiagaraUIRuntime");
			}

			void Track(UNiagaraUIWidget* Widget)
			{
				Tracked.AddUnique(Widget);
			}

			void Untrack(UNiagaraUIWidget* Widget)
			{
				Tracked.RemoveAllSwap([Widget](const TWeakObjectPtr<UNiagaraUIWidget>& Entry)
				{
					return !Entry.IsValid() || Entry.Get() == Widget;
				});
			}

			void WarnOnce(const FString& Message)
			{
				bool bAlreadyWarned = false;
				Warned.Add(Message, &bAlreadyWarned);
				if (!bAlreadyWarned)
				{
					UE_LOG(LogNiagaraUI, Warning, TEXT("%s"), *Message);
				}
			}

			TSharedPtr<FSlateBrush> GetMaterialBrush(UMaterialInterface* Material)
			{
				FBrushEntry& Entry = MaterialBrushes.FindOrAdd(Material);
				if (!Entry.Brush.IsValid() || Entry.Material != Material)
				{
					Entry.Material = Material;
					Entry.Brush = MakeShared<FSlateBrush>();
					Entry.Brush->SetResourceObject(Material);
					Entry.Brush->ImageSize = FVector2D(DotSize, DotSize);
				}
				Entry.LastUsed = FApp::GetCurrentTime();
				return Entry.Brush;
			}

			TSharedPtr<FSlateBrush> GetDefaultParticleBrush()
			{
				if (!DotBrush.IsValid() || !DotTexture)
				{
					DotTexture = MakeDotTexture();
					DotBrush = MakeShared<FSlateBrush>();
					// Without a texture (no RHI) the brush draws plain squares, which is still something.
					DotBrush->SetResourceObject(DotTexture);
					DotBrush->ImageSize = FVector2D(DotSize, DotSize);
				}
				return DotBrush;
			}

		private:
			struct FBrushEntry
			{
				TObjectPtr<UMaterialInterface> Material = nullptr;
				TSharedPtr<FSlateBrush> Brush;
				double LastUsed = 0.0;
			};

			bool Tick(float /*DeltaTime*/)
			{
				// Backwards: a widget gone is dropped as it is met.
				const uint64 Frame = GFrameCounter;
				for (int32 Index = Tracked.Num() - 1; Index >= 0; --Index)
				{
					UNiagaraUIWidget* Widget = Tracked[Index].Get();
					if (!Widget)
					{
						Tracked.RemoveAtSwap(Index);
						continue;
					}
					Widget->TickRuntime(Frame);
				}

				const double Now = FApp::GetCurrentTime();
				if (Now >= NextPrune)
				{
					NextPrune = Now + 2.0;
					PruneBrushes(Now);
				}
				return true;
			}

			/** Lets go of brushes no batch holds any more and nothing has drawn with lately, and of deleted materials'. */
			void PruneBrushes(double Now)
			{
				for (auto It = MaterialBrushes.CreateIterator(); It; ++It)
				{
					const FBrushEntry& Entry = It.Value();
					const bool bHeldElsewhere = Entry.Brush.IsValid() && Entry.Brush.GetSharedReferenceCount() > 1;
					if (!Entry.Material || (!bHeldElsewhere && Now - Entry.LastUsed > BrushKeepSeconds))
					{
						It.RemoveCurrent();
					}
				}
			}

			void HandleWorldCleanup(UWorld* World, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
			{
				// A copy: letting go of a widget's simulations takes it off the list.
				const TArray<TWeakObjectPtr<UNiagaraUIWidget>> Widgets = Tracked;
				for (const TWeakObjectPtr<UNiagaraUIWidget>& Entry : Widgets)
				{
					if (UNiagaraUIWidget* Widget = Entry.Get())
					{
						Widget->ReleaseSimulationsIn(World);
					}
				}
			}

			/** A white dot, opaque in the middle and fading smoothly to nothing at its edge. */
			static UTexture2D* MakeDotTexture()
			{
				UTexture2D* Texture = UTexture2D::CreateTransient(DotSize, DotSize, PF_B8G8R8A8, TEXT("NiagaraUISoftDot"));
				if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
				{
					return Texture;
				}

				Texture->SRGB = true;
				Texture->Filter = TF_Bilinear;
				Texture->AddressX = TA_Clamp;
				Texture->AddressY = TA_Clamp;

				FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
				if (uint8* Pixels = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE)))
				{
					const float Radius = DotSize * 0.5f;
					for (int32 Y = 0; Y < DotSize; ++Y)
					{
						for (int32 X = 0; X < DotSize; ++X)
						{
							const float DeltaX = (X + 0.5f - Radius) / Radius;
							const float DeltaY = (Y + 0.5f - Radius) / Radius;
							const float Inside = FMath::Clamp(1.f - FMath::Sqrt(DeltaX * DeltaX + DeltaY * DeltaY), 0.f, 1.f);
							const float Alpha = Inside * Inside * (3.f - 2.f * Inside);

							uint8* Pixel = Pixels + (Y * DotSize + X) * 4;
							Pixel[0] = 255;
							Pixel[1] = 255;
							Pixel[2] = 255;
							Pixel[3] = static_cast<uint8>(FMath::RoundToInt(Alpha * 255.f));
						}
					}
					Mip.BulkData.Unlock();
				}
				Texture->UpdateResource();
				return Texture;
			}

			TArray<TWeakObjectPtr<UNiagaraUIWidget>> Tracked;
			TMap<const UMaterialInterface*, FBrushEntry> MaterialBrushes;
			TObjectPtr<UTexture2D> DotTexture = nullptr;
			TSharedPtr<FSlateBrush> DotBrush;
			TSet<FString> Warned;
			double NextPrune = 0.0;
			FTSTicker::FDelegateHandle TickerHandle;
			FDelegateHandle WorldCleanupHandle;
		};

		TUniquePtr<FRuntime>& GetRuntime()
		{
			static TUniquePtr<FRuntime> Runtime;
			return Runtime;
		}
	}

	void Startup()
	{
		GetRuntime() = MakeUnique<FRuntime>();
	}

	void Shutdown()
	{
		GetRuntime().Reset();
	}

	void Track(UNiagaraUIWidget* Widget)
	{
		if (Widget && GetRuntime())
		{
			GetRuntime()->Track(Widget);
		}
	}

	void Untrack(UNiagaraUIWidget* Widget)
	{
		if (GetRuntime())
		{
			GetRuntime()->Untrack(Widget);
		}
	}

	bool IsEnabled()
	{
		return CVarEnable.GetValueOnGameThread() != 0;
	}

	bool IsLogging()
	{
		return CVarLog.GetValueOnGameThread() != 0;
	}

	void WarnOnce(const FString& Message)
	{
		if (GetRuntime())
		{
			GetRuntime()->WarnOnce(Message);
		}
	}

	bool IsUIMaterial(const UMaterialInterface* Material)
	{
		const UMaterial* BaseMaterial = Material ? Material->GetMaterial() : nullptr;
		return BaseMaterial && BaseMaterial->MaterialDomain == MD_UI;
	}

	TSharedPtr<FSlateBrush> GetMaterialBrush(UMaterialInterface* Material)
	{
		if (!GetRuntime() || !Material)
		{
			return nullptr;
		}
		return GetRuntime()->GetMaterialBrush(Material);
	}

	TSharedPtr<FSlateBrush> GetDefaultParticleBrush()
	{
		if (!GetRuntime())
		{
			return nullptr;
		}
		return GetRuntime()->GetDefaultParticleBrush();
	}
}
