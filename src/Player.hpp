#pragma once

#include <Common.hpp>
#include <Math.hpp>
#include <Shapes.hpp>
#include <Camera.hpp>

struct world;

struct entity
{
    vec3 P;
    f32 Yaw, Pitch;
};

//
// Player
//

struct player_control
{
    vec2 DesiredMoveDirection; // Local space, unnormalized
    bool IsJumping;
    bool IsRunning;
    bool PrimaryAction;
    bool SecondaryAction;
};

struct player : public entity
{
    vec3 Velocity;
    bool WasGroundedLastFrame;

    static constexpr f32 Height = 1.8f;
    static constexpr f32 EyeHeight = 1.75f;
    static constexpr f32 LegHeight = 0.51f;
    static constexpr f32 Width = 0.6f;

    f32 HeadBob;
    f32 BobAmplitude = 0.05f;
    f32 BobFrequency = 2.0f;
    f32 TargetHeadBobValue;
    f32 CurrentHeadBobValue;

    static constexpr f32 WalkBobAmplitude = 0.025f;
    static constexpr f32 WalkBobFrequency = 2.0f;

    static constexpr f32 RunBobAmplitude = 0.075f;
    static constexpr f32 RunBobFrequency = 2.5f;

    static constexpr f32 DefaultFov = ToRadians(80.0f);

    f32 FieldOfView;
    f32 CurrentFov;
    f32 TargetFov;

    static constexpr f32 BlockBreakTime = 0.5f;
    bool HasTargetBlock;
    vec3i TargetBlock;
    direction TargetDirection;
    f32 BreakTime;

    static constexpr f32 MaxBlockPlacementFrequency = 0.2f;
    f32 TimeSinceLastBlockPlacement;

    player_control Control;
};

camera GetCamera(const player* Player);
aabb GetAABB(const player* Player);
aabb GetVerticalAABB(const player* Player); // Excludes legs
void GetHorizontalAxes(const player* Player, vec3& Forward, vec3& Right);
vec3 GetForwardVector(const player* Player);

void HandleInput(player* Player, game_io* IO);
void UpdatePlayer(game_state* Game, world* World, game_io* IO, player* Player, render_frame* Frame);