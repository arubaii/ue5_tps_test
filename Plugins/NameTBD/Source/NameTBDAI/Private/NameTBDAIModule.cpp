// Copyright AR GameSystems 2026. All rights reserved.

#include "NameTBDAIModule.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "NameTBDAIChatSession.h"
#include "NameTBDAIMcpServer.h"
#include "NameTBDAISettings.h"
#include "NameTBDAITools.h"
#include "SNameTBDAIChat.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "NameTBDAI"

namespace
{
	const FName ChatTabName(TEXT("NameTBDAIChat"));
	TSharedPtr<FNameTBDAIChatSession> Session;
}

TSharedRef<FNameTBDAIChatSession> NameTBDAIModule::GetSession()
{
	if (!Session.IsValid())
	{
		Session = MakeShared<FNameTBDAIChatSession>();
	}
	return Session.ToSharedRef();
}

/**
 * The AI Assistant's editor module: registers the tools, the chat tab (Window > NameTBD AI) and, when the setting is on,
 * starts the MCP server. Nothing is sent anywhere until the user sends a chat message with their own key, or connects an
 * MCP client of their own.
 */
class FNameTBDAIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		NameTBDAITools::RegisterAll();

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ChatTabName, FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
		{
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab)
				[
					SNew(SNameTBDAIChat)
				];
		}))
		.SetDisplayName(LOCTEXT("ChatTab", "NameTBD AI"))
		.SetTooltipText(LOCTEXT("ChatTabTip", "Build and edit UI by describing it - widgets, animations, materials, particles - with your own AI provider."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help"));

		if (UNameTBDAISettings::Get().bStartMcpServer)
		{
			FString Error;
			if (!FNameTBDAIMcpServer::Get().Start(UNameTBDAISettings::Get().McpPort, Error))
			{
				UE_LOG(LogTemp, Warning, TEXT("%s"), *Error);
			}
		}
	}

	virtual void ShutdownModule() override
	{
		FNameTBDAIMcpServer::Get().Stop();
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ChatTabName);
		}
		Session.Reset();
	}
};

IMPLEMENT_MODULE(FNameTBDAIModule, NameTBDAI)

#undef LOCTEXT_NAMESPACE
