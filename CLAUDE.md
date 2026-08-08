# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

`ShipGame` is an Unreal Engine **5.7** C++ project (not a git repository). It started from Epic's standard Third Person template — `DefaultGame.ini` still calls it "Third Person Game Template" and the source tree carries the template's original variant sample content (`Variant_Combat`, `Variant_Platforming`, `Variant_SideScrolling`) largely unmodified. The actual game being built on top of that scaffolding is a multiplayer **ship** game: players spawn aboard a moving ship (`AShipActor`), can walk around on its deck (it's a moving platform via `UCharacterMovementComponent`'s standard support), and one player at a time can take the helm (`ASteeringWheel`) to steer it. Despite starting from the third person template, player characters now use the first person character and setup. 

Engine install: `C:\Program Files\Epic Games\UE_5.7`.

## Build commands

There's no cross-platform build script — build via UnrealBuildTool directly, or through the generated Visual Studio solution (`ShipGame.sln`).

Editor build (what you want after any C++ change, run from repo root):
```
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" ShipGameEditor Win64 Development -project="F:\program files2\UNreal\ShipGame\ShipGame.uproject" -waitmutex
```

Game (non-editor) build, e.g. for a packaged/standalone test:
```
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" ShipGame Win64 Development -project="F:\program files2\UNreal\ShipGame\ShipGame.uproject" -waitmutex
```

Regenerate the Visual Studio project files (needed after adding/removing/renaming source files, since there's no CMake/other generator):
```
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="F:\program files2\UNreal\ShipGame\ShipGame.uproject" -game -engine
```

There is no automated test suite in this repo — verify changes by building and running the editor/game.

## Architecture

### Module layout

Single primary game module `ShipGame` (`Source/ShipGame/ShipGame.Build.cs`), depending on `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`, `AIModule`, `StateTreeModule`, `GameplayStateTreeModule`, `UMG`, `Slate`. Two build targets: `ShipGame` (Game) and `ShipGameEditor` (Editor), both `Unreal5_7` include-order.

The default game mode is set via `Config/DefaultEngine.ini` (`GlobalDefaultGameMode=/Game/Blueprints/BP_ShipGameMode`), and the startup/default map is `Content/ThirdPerson/Lvl_ThirdPerson`. Most gameplay classes are C++ base classes (many marked `abstract`) with a matching Blueprint subclass in `Content/Blueprints/` (e.g. `AShipActor` → `BP_ShipActor`, `AShipGameMode` → `BP_ShipGameMode`) that fills in meshes, input assets, and per-instance tuning. When investigating gameplay behavior, always check whether the relevant Blueprint overrides a C++ default — the C++ headers document intent extensively in comments, but numeric defaults and asset references are often only real in the Blueprint.

### Ship game (the actual game — `Source/ShipGame/*.h/.cpp` at the top level)

- **`AShipActor`** (`ShipActor.h`) — the ship itself. Moves forward at a constant `ForwardSpeed`; helm input only ever affects yaw/vertical, never throttle. Deliberately **does not** rely on Unreal's built-in actor transform replication (`bReplicateMovement = false`) because that produces visible jitter/teleporting for a non-physics actor with characters standing on it. Instead every machine (server and clients) runs the same `MoveShip()` integration each tick from a small set of replicated helm-input fields (`bHelmControlled`, `HelmYawInput`, `HelmVerticalInput`), and the server separately replicates its authoritative transform (`ServerLocation`/`ServerRotation`) purely so clients can gently drift-correct (`CorrectDriftFromServer`) toward it without snapping. This pattern (locally-simulated motion + replicated inputs + separate soft drift-correction) is the key architectural idea in this file — read the class-level comment in `ShipActor.h` before changing anything about how the ship moves or replicates. The root component is `HazardBox` (a `UBoxComponent`) rather than the visible mesh, specifically so it has something to sweep for collision each tick; everything else (hull mesh, spawn points, deck props) is attached beneath it and rides along for free.
- **`ASteeringWheel`** (`SteeringWheel.h`) — a stationary interactable `Pawn` on deck. A character interacts with it to have their `PlayerController` possess it (`TryEngage`, server-only, enforces single-occupant); while possessed it feeds A/D → `AShipActor::SetHelmYawInput` and W/S → `SetHelmVerticalInput`. Releasing it (interact again or disconnect) hands control back to the previous pawn.
- **`AShipCharacter`** (`ShipCharacter.h`) — the player pawn, Enhanced Input based (Move/Look/Jump/Interact actions bound via `AShipGamePlayerController`'s mapping contexts, not on the character itself). `Interact()` finds the nearest unoccupied `ASteeringWheel` in range via a server RPC (`ServerTryEngageHelm`) so the range/facing check is authoritative.
- **`AShipGameMode`** (`ShipGameMode.h`) — overrides `HandleStartingNewPlayer_Implementation` (instead of the default `PostLogin`/`RestartPlayer` flow) specifically to skip Unreal's default `PlayerStart` lookup and always spawn new players directly on the ship, round-robining through the ship's spawn points.
- **`AShipGamePlayerController`** — applies the `UInputMappingContext`s and (optionally) spawns mobile touch controls; this is the abstract base Blueprinted as `BP_ShipPlayerController`.

### Template variant sample content (`Source/ShipGame/Variant_Combat`, `Variant_Platforming`, `Variant_SideScrolling`)

These three folders are Epic's stock Third Person template variant gameplay samples, included by the template and largely untouched — not part of the ship game's active gameplay loop (the default game mode/map is the ship game, not any of these variants). Each is self-contained:
- **Variant_Combat** — melee combat sample: `ACombatCharacter` (combo/charged attacks, HP, ragdoll death+respawn) implementing `ICombatAttacker`/`ICombatDamageable` interfaces (`Interfaces/`), StateTree-driven AI (`AI/CombatAIController`, `AI/CombatStateTreeUtility`, EQS contexts), and supporting gameplay actors (`Gameplay/CombatDamageableBox`, `CombatLavaFloor`, etc.) plus a UMG life bar (`UI/CombatLifeBar`).
- **Variant_Platforming** — `APlatformingCharacter`/`APlatformingGameMode` with a dash move (`Animation/AnimNotify_EndDash`).
- **Variant_SideScrolling** — 2.5D side-scroller sample: fixed-axis `ASideScrollingCharacter`, `ASideScrollingCameraManager`, StateTree AI NPCs (`AI/`), interactables via `Interfaces/SideScrollingInteractable` (jump pads, moving/soft platforms, pickups in `Gameplay/`), and a UMG HUD (`UI/SideScrollingUI`).

### Content structure

Mirrors the source split: `Content/Blueprints` holds the ship game's Blueprint layer (`BP_ShipActor`, `BP_ShipCharacter`, `BP_ShipGameMode`, `BP_ShipPlayerController`, `BP_SteeringWheel`, `BP_FloatingRock`), while `Content/Variant_Combat`, `Content/Variant_Platforming`, `Content/Variant_SideScrolling` hold each sample's assets/Blueprints/anims/input. `Content/ThirdPerson` and `Content/FirstPerson` are template-provided character assets/levels; `Content/Weapons` and `Content/LevelPrototyping` are shared art/prototyping assets. Enhanced Input assets live in `Content/Input` (top-level, shared) plus per-variant `Input/` subfolders.

Networking note: since the ship game is built around multiplayer replication (see `AShipActor` above), when adding new ship-related gameplay state, follow the existing pattern — replicate only the minimal authoritative inputs/state needed, run the actual simulation locally on every machine from those replicated inputs, and use a separate soft drift-correction if you also need to guard against server/client divergence, rather than turning on `bReplicateMovement`/relying on transform replication directly.
