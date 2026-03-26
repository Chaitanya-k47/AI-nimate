// Copyright Epic Games, Inc. All Rights Reserved.

#include "AInimatePluginCommands.h"

#define LOCTEXT_NAMESPACE "FAInimatePluginModule"

void FAInimatePluginCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "AInimatePlugin", "Bring up AInimatePlugin window", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
