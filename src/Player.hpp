#pragma once

#include <Common.hpp>
#include <Math.hpp>
#include <Shapes.hpp>
#include <Camera.hpp>

struct world;
struct game_input;

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

struct player
{
    vec3 P;
    vec3 Velocity;
    f32 Yaw, Pitch;
    bool WasGroundedLastFrame;

    static constexpr f32 Height = 1.8f;
    static constexpr f32 EyeHeight = 1.75f;
    static constexpr f32 LegHeight = 0.51f;
    static constexpr f32 Width = 0.6f;

    f32 HeadBob;
    f32 BobAmplitude = 0.05f;
    f32 BobFrequency = 2.0f;

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

camera Player_GetCamera(const player* Player);
aabb Player_GetAABB(const player* Player);
aabb Player_GetVerticalAABB(const player* Player); // Excludes legs
void Player_GetHorizontalAxes(const player* Player, vec3& Forward, vec3& Right);
vec3 Player_GetForward(const player* Player);

void Player_HandleInput(player* Player, game_input* Input);
void Player_Update(player* Player, world* World, f32 DeltaTime);