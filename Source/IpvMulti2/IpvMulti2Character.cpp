// Copyright Epic Games, Inc. All Rights Reserved.

#include "IpvMulti2Character.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"
#include "Engine/Engine.h"
#include "Components/SkeletalMeshComponent.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Online/OnlineSessionNames.h"


DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// AIpvMulti2Character

AIpvMulti2Character::AIpvMulti2Character():
CreateSessionCompleteDelegate(FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnCreateSessionComplete)),
FindSessionsCompleteDelegate(FOnFindSessionsCompleteDelegate::CreateUObject(this, &AIpvMulti2Character::OnFindSessionsComplete))
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f); // ...at this rotation rate

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)

	//Initialize the player's Health
	MaxHealth = 100.0f;
	CurrentHealth = MaxHealth;

	//Initialize ammo
	MaxAmmo = 5;
	CurrentAmmo = MaxAmmo;

	IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	if (OnlineSessionInterface)
	{
		OnlineSessionInterface = OnlineSubsystem->GetSessionInterface();
		if (GEngine)
		{
			FString subsystemtest = FString::Printf(TEXT("Found Online Subsystem %s"), *OnlineSubsystem->GetSubsystemName().ToString());
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Purple, subsystemtest);
		}
	}
}

void AIpvMulti2Character::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    
	DOREPLIFETIME(AIpvMulti2Character, CurrentHealth);
	DOREPLIFETIME(AIpvMulti2Character, CurrentAmmo);
	DOREPLIFETIME(AIpvMulti2Character, bIsRagdoll);
}

//////////////////////////////////////////////////////////////////////////
// Input

void AIpvMulti2Character::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

void AIpvMulti2Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AIpvMulti2Character::Move);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AIpvMulti2Character::Look);
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AIpvMulti2Character::SetCurrentHealth(float healthValue)
{
    if (GetLocalRole() == ROLE_Authority)
    {
        CurrentHealth = FMath::Clamp(healthValue, 0.f, MaxHealth);
        OnHealthUpdate();
    }
}

float AIpvMulti2Character::TakeDamage(float DamageTaken, struct FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    float damageApplied = CurrentHealth - DamageTaken;
    SetCurrentHealth(damageApplied);
    return damageApplied;
}

void AIpvMulti2Character::OnRep_CurrentHealth()
{
    OnHealthUpdate();
}

void AIpvMulti2Character::OnRep_CurrentAmmo()
{
    OnAmmoUpdated();
}

void AIpvMulti2Character::AddAmmo(int32 Amount)
{
    if (GetLocalRole() == ROLE_Authority)
    {
        CurrentAmmo = MaxAmmo;
        OnAmmoUpdated();
    }
}

void AIpvMulti2Character::OnHealthUpdate_Implementation()
{
    bReplicates = true;
    // Client-specific functionality
    if (IsLocallyControlled())
    {
        FString healthMessage = FString::Printf(TEXT("You now have %f health remaining."), CurrentHealth);
        GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Blue, healthMessage);
     
        if (CurrentHealth <= 0)
        {
            FString deathMessage = FString::Printf(TEXT("You have been killed."));
            GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, deathMessage);
            
            StartRagdoll();
            HideUI();
        }
    }
    
    // Server-specific functionality
    if (GetLocalRole() == ROLE_Authority)
    {
        FString healthMessage = FString::Printf(TEXT("%s now has %f health remaining."), *GetFName().ToString(), CurrentHealth);
        GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Blue, healthMessage);

        if (CurrentHealth <= 0)
        {
            DisableInput(nullptr);
            DisableCharacterCollision();
        }
    }
}

void AIpvMulti2Character::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	
		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void AIpvMulti2Character::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void AIpvMulti2Character::StartRagdoll()
{
    // On server, call the server RPC
    if (GetLocalRole() == ROLE_Authority)
    {
        bIsRagdoll = true;
        OnRep_IsRagdoll(); // Call locally on server
    }
    else // On client, ask server to activate ragdoll
    {
        ServerStartRagdoll();
    }
}

void AIpvMulti2Character::DisableCharacterCollision()
{
    bReplicates = true;
    // Disable capsule collision
    UCapsuleComponent* CapsuleComp = GetCapsuleComponent();
    if (CapsuleComp)
    {
        CapsuleComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        CapsuleComp->SetCollisionResponseToAllChannels(ECR_Ignore);
    }

    // Disable mesh collision (except for physics)
    USkeletalMeshComponent* MeshComp = GetMesh();
    if (MeshComp)
    {
        MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
        MeshComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
        MeshComp->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
        MeshComp->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    }

    // Disable character movement
    UCharacterMovementComponent* MovementComp = GetCharacterMovement();
    if (MovementComp)
    {
        MovementComp->StopMovementImmediately();
        MovementComp->DisableMovement();
    }
}

void AIpvMulti2Character::ServerStartRagdoll_Implementation()
{
    bIsRagdoll = true;
}

void AIpvMulti2Character::OnRep_IsRagdoll()
{
    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp) return;
    
    if (bIsRagdoll)
    {
        // Enable physics simulation on the mesh
        MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        MeshComp->SetSimulatePhysics(true);
        MeshComp->SetAllBodiesSimulatePhysics(true);
        MeshComp->WakeAllRigidBodies();
        
        DisableCharacterCollision();
    }
    else
    {
        // Disable physics simulation
        MeshComp->SetSimulatePhysics(false);
        MeshComp->SetAllBodiesSimulatePhysics(false);
        MeshComp->PutAllRigidBodiesToSleep();
        
        // Reset mesh position relative to capsule
        MeshComp->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetIncludingScale);
        MeshComp->SetRelativeLocation(FVector(0, 0, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
        MeshComp->SetRelativeRotation(FRotator(0, -90.f, 0));
        
        // Force physics state update
        MeshComp->SetPhysicsLinearVelocity(FVector::ZeroVector);
        MeshComp->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    }
}

void AIpvMulti2Character::HideUI()
{
    if (!IsLocallyControlled()) return;
}

void AIpvMulti2Character::CreateGameSession()
{
    if (!OnlineSessionInterface.IsValid()) return;
    FNamedOnlineSession* ExistingSession = OnlineSessionInterface -> GetNamedSession(NAME_GameSession);
    if (ExistingSession)
    {
        OnlineSessionInterface->DestroySession(NAME_GameSession);
    }
    //Delegate-List
    OnlineSessionInterface->AddOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegate);
    //CreateSession
    TSharedPtr<FOnlineSessionSettings> SessionSettings = MakeShareable(new FOnlineSessionSettings());
    SessionSettings->bIsLANMatch = false;
    SessionSettings->NumPublicConnections = 4;
    SessionSettings->bAllowJoinInProgress = true;
    SessionSettings->bAllowJoinViaPresence = true;
    SessionSettings->bShouldAdvertise = true;
    SessionSettings->bUsesPresence = true;
    SessionSettings->bUseLobbiesIfAvailable = false;
    SessionSettings->Set(FName("MatchType"),FString("FreeForAll"),EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    const ULocalPlayer* LocalPlayer= GetWorld()->GetFirstLocalPlayerFromController();

    OnlineSessionInterface->CreateSession(*LocalPlayer->GetPreferredUniqueNetId(), NAME_GameSession, *SessionSettings);
}

void AIpvMulti2Character::OnFindSessionsComplete(bool bWasSuccess)
{
    for (auto Result:SessionSearch->SearchResults)
    {
        FString Id = Result.GetSessionIdStr();
        FString User = Result.Session.OwningUserName;
        FString MatchType;
        Result.Session.SessionSettings.Get(FName("MatchType"), MatchType);
        
        if(GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                -1,
                15.f,
                FColor::Orange,
                FString::Printf(TEXT("Id: %s, User: %s"), *Id, *User)
            );
        }
        if (MatchType == FString("FreeForAll"))
        {
            if(GEngine)
            {
                GEngine->AddOnScreenDebugMessage(
                    -1,
                    15.f,
                    FColor::Orange,
                    FString::Printf(TEXT("Joining Match Type: %s"), *MatchType)
                );
            }
        }
    }
}

void AIpvMulti2Character::OnCreateSessionComplete(FName SessionName, bool bWasSuccess)
{
    if(bWasSuccess)
    {
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                -1,
                15.f,
                FColor::Blue,
                FString::Printf(TEXT("Created Session %s"), *SessionName.ToString())
            );
        }
        UWorld* World = GetWorld();
        if (!World) return;
        //World->ServerlTravel("/Game/Scenes/MainGame");
    }
    else
    {
        GEngine->AddOnScreenDebugMessage(
            -1,
            15.f,
            FColor::Red,
            FString(TEXT("Create Session Failed"))
        );
    }
}

void AIpvMulti2Character::JoinGameSession()
{
    if (!OnlineSessionInterface.IsValid()) return;
    SessionSearch = MakeShareable(new FOnlineSessionSearch());
    //delegate list
    OnlineSessionInterface->AddOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegate);
    //find session
    SessionSearch->MaxSearchResults = 10000;
    SessionSearch->bIsLanQuery = false;
    SessionSearch->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);

    const ULocalPlayer* LocalPlayer=GetWorld()->GetFirstLocalPlayerFromController();

    OnlineSessionInterface->FindSessions(*LocalPlayer->GetPreferredUniqueNetId(),SessionSearch.ToSharedRef());
}

void AIpvMulti2Character::ServerRespawn_Implementation()
{
    // Reset health
    CurrentHealth = MaxHealth;
    OnHealthUpdate();
    
    // Reset ammo
    CurrentAmmo = MaxAmmo;
    OnAmmoUpdated();
    
    // Reset ragdoll state FIRST
    bIsRagdoll = false;
    OnRep_IsRagdoll(); // Force immediate update
    
    // Reset physics state before enabling collision
    USkeletalMeshComponent* MeshComp = GetMesh();
    if (MeshComp)
    {
        MeshComp->SetSimulatePhysics(false);
        MeshComp->SetAllBodiesSimulatePhysics(false);
        MeshComp->PutAllRigidBodiesToSleep();
    }
    
    // Enable input and movement
    EnableInput(Cast<APlayerController>(GetController()));
    
    // Re-enable collision
    UCapsuleComponent* CapsuleComp = GetCapsuleComponent();
    if (CapsuleComp)
    {
        CapsuleComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        CapsuleComp->SetCollisionResponseToAllChannels(ECR_Block);
    }
    
    // Re-enable character movement
    UCharacterMovementComponent* MovementComp = GetCharacterMovement();
    if (MovementComp)
    {
        MovementComp->SetMovementMode(EMovementMode::MOVE_Walking);
        MovementComp->StopMovementImmediately();
        MovementComp->ClearAccumulatedForces();
    }
    
    // Reset position to spawn point
    SetActorLocation(GetActorLocation());
    
    // Force network update
    ForceNetUpdate();
}
