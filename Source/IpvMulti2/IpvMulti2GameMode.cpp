// Copyright Epic Games, Inc. All Rights Reserved.

#include "IpvMulti2GameMode.h"
#include "IpvMulti2Character.h"
#include "UObject/ConstructorHelpers.h"

AIpvMulti2GameMode::AIpvMulti2GameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
