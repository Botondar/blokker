#include "Player.hpp"

#include <Profiler.hpp>
#include <World.hpp>

// Only for game_input, should be moved
#include <Game.hpp>

//
// Player
//

camera Player_GetCamera(const player* Player)
{
    vec3 CamPDelta = { 0.0f, 0.0f, Player->BobAmplitude * Sin(2.0f * PI * Player->HeadBob) };
    camera Camera =
    {
        .P = Player->P + CamPDelta,
        .Yaw = Player->Yaw,
        .Pitch = Player->Pitch,
        .FieldOfView = Player->CurrentFov,
    };

    return Camera;
}

aabb Player_GetAABB(const player* Player)
{
    aabb Result = 
    {
        .Min = 
        {
            Player->P.x - 0.5f * Player->Width, 
            Player->P.y - 0.5f * Player->Width, 
            Player->P.z - Player->EyeHeight 
        },
        .Max = 
        {
            Player->P.x + 0.5f * Player->Width, 
            Player->P.y + 0.5f * Player->Width, 
            Player->P.z + (Player->Height - Player->EyeHeight) 
        },
    };
    return Result;
}

aabb Player_GetVerticalAABB(const player* Player)
{
    aabb Result = 
    {
        .Min = 
        {
            Player->P.x - 0.5f * Player->Width, 
            Player->P.y - 0.5f * Player->Width, 
            Player->P.z - Player->EyeHeight + Player->LegHeight,
        },
        .Max = 
        {
            Player->P.x + 0.5f * Player->Width, 
            Player->P.y + 0.5f * Player->Width, 
            Player->P.z + (Player->Height - Player->EyeHeight) 
        },
    };
    return Result;
}

void Player_GetHorizontalAxes(const player* Player, vec3& Forward, vec3& Right)
{   
    f32 SinYaw = Sin(Player->Yaw);
    f32 CosYaw = Cos(Player->Yaw);
    Right = { CosYaw, SinYaw, 0.0f };
    Forward = { -SinYaw, CosYaw, 0.0f };
}

vec3 Player_GetForward(const player* Player)
{
    vec3 Result = {};
    
    f32 SinYaw = Sin(Player->Yaw);
    f32 CosYaw = Cos(Player->Yaw);
    f32 SinPitch = Sin(Player->Pitch);
    f32 CosPitch = Cos(Player->Pitch);

    Result = 
    {
        -SinYaw*CosPitch,
        CosYaw*CosPitch,
        SinPitch,
    };

    return Result;
}

void Player_HandleInput(player* Player, game_input* Input)
{
    constexpr f32 MouseTurnSpeed = 2.5e-3f;

    Player->Control.PrimaryAction = false;
    Player->Control.SecondaryAction = false;
    // Disable mouse input when there's a cursor on the screen
    if (!Input->IsCursorEnabled)
    {
        Player->Yaw -= Input->MouseDelta.x * MouseTurnSpeed;
        Player->Pitch -= Input->MouseDelta.y * MouseTurnSpeed;

        Player->Control.PrimaryAction = Input->MouseButtons[MOUSE_LEFT];
        Player->Control.SecondaryAction = Input->MouseButtons[MOUSE_RIGHT];
    }
    constexpr f32 CameraClamp = 0.5f * PI - 1e-3f;
    Player->Pitch = Clamp(Player->Pitch, -CameraClamp, CameraClamp);

    Player->Control.DesiredMoveDirection = { 0.0f, 0.0f };
    if (Input->Forward) Player->Control.DesiredMoveDirection.x += 1.0f;
    if (Input->Back) Player->Control.DesiredMoveDirection.x -= 1.0f;
    if (Input->Right) Player->Control.DesiredMoveDirection.y += 1.0f;
    if (Input->Left) Player->Control.DesiredMoveDirection.y -= 1.0f;

    Player->Control.IsJumping = Input->Space;
    Player->Control.IsRunning = Input->LeftShift;
}

void Player_Update(player* Player, world* World, f32 dt)
{
    TIMED_FUNCTION();
    
    // Block breaking
    {
        vec3 Forward = Player_GetForward(Player);

        chunk* PlayerChunk = World_FindPlayerChunk(World);
        constexpr f32 PlayerReach = 8.0f;

        vec3i OldTargetBlock = Player->TargetBlock;
        Player->HasTargetBlock = World_RayCast(World, Player->P, Forward, PlayerReach, &Player->TargetBlock, &Player->TargetDirection);

        if (Player->HasTargetBlock)
        {
            // Breaking
            if ((OldTargetBlock == Player->TargetBlock) && Player->Control.PrimaryAction)
            {
                u16 VoxelType = World_GetVoxelType(World, Player->TargetBlock);
                const voxel_desc* VoxelDesc = &VoxelDescs[VoxelType];
                if (VoxelDesc->Flags & VOXEL_FLAGS_SOLID)
                {
                    Player->BreakTime += dt;
                    if (Player->BreakTime >= Player->BlockBreakTime)
                    {
                        World_SetVoxelType(World, Player->TargetBlock, VOXEL_AIR);
                    }
                }
            }
            else
            {
                Player->BreakTime = 0.0f;
            }

            // Placing
            Player->TimeSinceLastBlockPlacement += dt;
            if (Player->Control.SecondaryAction && (Player->TimeSinceLastBlockPlacement > Player->MaxBlockPlacementFrequency))
            {
                vec3i DeltaP = {};
                switch (Player->TargetDirection)
                {
                    case 0: DeltaP = { +1, 0, 0 }; break;
                    case 1: DeltaP = { -1, 0, 0 }; break;
                    case 2: DeltaP = { 0, +1, 0 }; break;
                    case 3: DeltaP = { 0, -1, 0 }; break;
                    case 4: DeltaP = { 0, 0, +1 }; break;
                    case 5: DeltaP = { 0, 0, -1 }; break;
                    default: assert(!"Invalid code path");
                }

                vec3i PlacementP = Player->TargetBlock + DeltaP;
                u16 VoxelType = World_GetVoxelType(World, PlacementP);
                if (VoxelType == VOXEL_AIR)
                {
                    aabb PlayerBox = Player_GetAABB(Player);
                    vec3i BoxP = PlacementP;
                    aabb BlockBox = MakeAABB((vec3)BoxP, (vec3)(BoxP + vec3i{1, 1, 1}));

                    vec3 Overlap;
                    int MinCoord;
                    if (!AABB_Intersect(PlayerBox, BlockBox, Overlap, MinCoord))
                    {
                        World_SetVoxelType(World, PlacementP, VOXEL_GROUND);
                        Player->TimeSinceLastBlockPlacement = 0.0f;
                    }
                }
            }
        }
    }
    

    vec3 Forward, Right;
    Player_GetHorizontalAxes(Player, Forward, Right);
    vec3 Up = { 0.0f, 0.0f, 1.0f };

    vec3 DesiredMoveDirection = SafeNormalize(Player->Control.DesiredMoveDirection.x * Forward + Player->Control.DesiredMoveDirection.y * Right);

    constexpr f32 WalkSpeed = 4.0f;
    constexpr f32 RunSpeed = 7.75f;
    
    vec3 Acceleration = {};
    if (Player->WasGroundedLastFrame)
    {
        // Walk/Run controls
        f32 DesiredSpeed = Player->Control.IsRunning ? RunSpeed : WalkSpeed;

        vec3 DesiredVelocity = DesiredMoveDirection * DesiredSpeed;
        vec3 VelocityXY = { Player->Velocity.x, Player->Velocity.y, 0.0f };
        
        vec3 DiffV = DesiredVelocity - VelocityXY;

        constexpr f32 Accel = 10.0f;
        Acceleration += Accel * DiffV;

        if (Player->Control.IsJumping)
        {
            constexpr f32 DesiredJumpHeight = 1.2f; // This is here just for reference
            constexpr f32 JumpVelocity = 7.7459667f; // sqrt(2 * Gravity * DesiredJumpHeight)
            Player->Velocity.z += JumpVelocity;
        }

        // Head bobbing
        f32 Speed = Length(Player->Velocity);
        if (Speed < WalkSpeed)
        {
            f32 t = Fade3(Speed / WalkSpeed);
            Player->BobAmplitude = Lerp(0.0f, Player->WalkBobAmplitude, t);
            Player->BobFrequency = Lerp(0.0f, Player->WalkBobFrequency, t);
        }
        else
        {
            f32 t = Fade3((Speed - WalkSpeed) / (RunSpeed - WalkSpeed));
            Player->BobAmplitude = Lerp(Player->WalkBobAmplitude, Player->RunBobAmplitude, t);
            Player->BobFrequency = Lerp(Player->WalkBobFrequency, Player->RunBobFrequency, t);
        }
        Player->HeadBob += Player->BobFrequency*dt;
        if (Player->HeadBob >= 1.0f)
        {
            Player->HeadBob -= 1.0f;
        }
    }
    else
    {
        // Clear velocity if the user is trying to move
        if (Dot(DesiredMoveDirection, DesiredMoveDirection) != 0.0f)
        {
            f32 DesiredSpeed = Player->Control.IsRunning ? RunSpeed : WalkSpeed;

            vec3 DesiredVelocity = DesiredMoveDirection * DesiredSpeed;
            vec3 VelocityXY = { Player->Velocity.x, Player->Velocity.y, 0.0f };
        
            vec3 DiffV = DesiredVelocity - VelocityXY;

            constexpr f32 Accel = 10.0f;
            Acceleration += Accel * DiffV;
        }

        // Drag force = 0.5 * c * rho * v_r^2 * A
        constexpr f32 AirDensity = 1.2041f;
        constexpr f32 DragCoeff = 1.0f;
        constexpr f32 BottomArea = Player->Width * Player->Width;
        constexpr f32 SideArea = Player->Width * Player->Height;
            
        f32 Speed = Length(Player->Velocity);
        vec3 UnitVelocity = SafeNormalize(Player->Velocity);
            
        f32 Area = Lerp(SideArea, BottomArea, Abs(Dot(UnitVelocity, vec3{0.0f, 0.0f, 1.0f})));
        vec3 DragForce = (-0.5f * AirDensity * DragCoeff * Area) * Player->Velocity * Speed;
            
        constexpr f32 PlayerMass = 60.0f;
        Acceleration += DragForce * (1.0f / PlayerMass);
    }

    constexpr f32 Gravity = 25.0f;
    
    Acceleration += vec3{ 0.0f, 0.0f, -Gravity };
    Player->Velocity += Acceleration * dt;

    // Fov animation
    {
        f32 PlayerSpeed = Length(Player->Velocity);
        f32 Mul = Max(Dot(Forward, DesiredMoveDirection), 0.0f);

        f32 t = Mul * Clamp((PlayerSpeed - WalkSpeed) / (RunSpeed - WalkSpeed), 0.0f, 1.0f);
        Player->TargetFov = Lerp(Player->DefaultFov, ToRadians(95.0f), t);
        Player->CurrentFov = Lerp(Player->CurrentFov, Player->TargetFov, Clamp(1.0f - Exp(-15.0f*dt), 0.0f, 1.0f));
    }

    vec3 dP = (Player->Velocity + 0.5f * Acceleration * dt) * dt;

    // Collision
    {
        TIMED_BLOCK("Collision");

        Player->WasGroundedLastFrame = false;

        // Apply movement and resolve collisions separately on the axes
        vec3 Displacement = World_ApplyEntityMovement(World, Player, Player_GetAABB(Player), dP);
        
        if (Displacement.x != 0.0f)
        {
            Player->Velocity.x = 0.0f;
        }
        if (Displacement.y != 0.0f)
        {
            Player->Velocity.y = 0.0f;
        }
        if (Displacement.z != 0.0f)
        {
            Player->Velocity.z = 0.0f;
            if (Displacement.z > 0.0f)
            {
                Player->WasGroundedLastFrame = true;
            }
        }
    }
}