// Copyright Epic Games, Inc. All Rights Reserved.

#include "AInimatePlugin.h"
#include "AInimatePluginStyle.h"
#include "AInimatePluginCommands.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "ToolMenus.h"

static const FName AInimatePluginTabName("AInimatePlugin");

#define LOCTEXT_NAMESPACE "FAInimatePluginModule"

void FAInimatePluginModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FAInimatePluginStyle::Initialize();
	FAInimatePluginStyle::ReloadTextures();

	FAInimatePluginCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FAInimatePluginCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FAInimatePluginModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAInimatePluginModule::RegisterMenus));
	
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(AInimatePluginTabName, FOnSpawnTab::CreateRaw(this, &FAInimatePluginModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FAInimatePluginTabTitle", "AInimatePlugin"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FAInimatePluginModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FAInimatePluginStyle::Shutdown();

	FAInimatePluginCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AInimatePluginTabName);
}

TSharedRef<SDockTab> FAInimatePluginModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	FText WidgetText = FText::Format(
		LOCTEXT("WindowWidgetText", "Add code to {0} in {1} to override this window's contents"),
		FText::FromString(TEXT("FAInimatePluginModule::OnSpawnPluginTab")),
		FText::FromString(TEXT("AInimatePlugin.cpp"))
		);

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			// Put your tab content here!
			SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(WidgetText)
			]
		];
}

void FAInimatePluginModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(AInimatePluginTabName);
}

void FAInimatePluginModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FAInimatePluginCommands::Get().OpenPluginWindow, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FAInimatePluginCommands::Get().OpenPluginWindow));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FAInimatePluginModule, AInimatePlugin)