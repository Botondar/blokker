#include "Player.hpp"

//
// Player
//

camera GetCamera(const player* Player)
{
    vec3 CamPDelta = { 0.0f, 0.0f, Player->CurrentHeadBobValue };
    camera Camera =
    {
        .P = Player->P + CamPDelta,
        .Yaw = Player->Yaw,
        .Pitch = Player->Pitch,
        .FieldOfView = Player->CurrentFov,
        .Near = 0.01f,
        .Far = 1000.0f,
    };

    return Camera;
}

aabb GetAABB(const player* Player)
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

aabb GetVerticalAABB(const player* Player)
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

void GetHorizontalAxes(const player* Player, vec3& Forward, vec3& Right)
{   
    f32 SinYaw = Sin(Player->Yaw);
    f32 CosYaw = Cos(Player->Yaw);
    Right = { CosYaw, SinYaw, 0.0f };
    Forward = { -SinYaw, CosYaw, 0.0f };
}

vec3 GetForwardVector(const player* Player)
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

void HandleInput(player* Player, game_io* IO)
{
    constexpr f32 MouseTurnSpeed = 2.5e-3f;

    Player->Control.PrimaryAction = false;
    Player->Control.SecondaryAction = false;
    // Disable mouse input when there's a cursor on the screen
    if (!IO->IsCursorEnabled)
    {
        Player->Yaw -= IO->MouseDelta.x * MouseTurnSpeed;
        Player->Pitch -= IO->MouseDelta.y * MouseTurnSpeed;

        Player->Control.PrimaryAction = IO->MouseButtons[MOUSE_LEFT];
        Player->Control.SecondaryAction = IO->MouseButtons[MOUSE_RIGHT];
    }
    constexpr f32 CameraClamp = 0.5f * PI - 1e-3f;
    Player->Pitch = Clamp(Player->Pitch, -CameraClamp, CameraClamp);

    Player->Control.DesiredMoveDirection = { 0.0f, 0.0f };
    if (IO->Forward) Player->Control.DesiredMoveDirection.x += 1.0f;
    if (IO->Back) Player->Control.DesiredMoveDirection.x -= 1.0f;
    if (IO->Right) Player->Control.DesiredMoveDirection.y += 1.0f;
    if (IO->Left) Player->Control.DesiredMoveDirection.y -= 1.0f;

    Player->Control.IsJumping = IO->Space;
    Player->Control.IsRunning = IO->LeftShift;
}

void UpdatePlayer(game_state* Game, world* World, game_io* IO, player* Player, render_frame* Frame)
{
    TIMED_FUNCTION();
    
    f32 dt = IO->DeltaTime;

    // Crosshair
    vec2 ScreenExtent = { (f32)Frame->RenderExtent.x, (f32)Frame->RenderExtent.y };
    vec2 ScreenCenterP = 0.5f * ScreenExtent;
    constexpr f32 CrosshairRadius = 20.0f;
    constexpr f32 CrosshairWidth = 1.0f;
    ImRect2D(Frame, 
             ScreenCenterP - vec2{ CrosshairRadius, CrosshairWidth },
             ScreenCenterP + vec2{ CrosshairRadius, CrosshairWidth },
             PackColor(0xFF, 0xFF, 0xFF));
    ImRect2D(Frame, 
             ScreenCenterP - vec2{ CrosshairWidth, CrosshairRadius },
             ScreenCenterP + vec2{ CrosshairWidth, CrosshairRadius },
             PackColor(0xFF, 0xFF, 0xFF));

    if (World->Debug.IsHitboxEnabled)
    {
        ImBoxOutline(Frame, GetAABB(Player), PackColor(0xFF, 0x00, 0x00), 0.0025f);
        ImBoxOutline(Frame, GetVerticalAABB(Player), PackColor(0xFF, 0xFF, 0x00), 0.0025f);
    }

    // Block breaking
    {
        vec3 Forward = GetForwardVector(Player);

        constexpr f32 PlayerReach = 8.0f;

        vec3i OldTargetBlock = Player->TargetBlock;
        Player->HasTargetBlock = RayCast(World, Player->P, Forward, PlayerReach, &Player->TargetBlock, &Player->TargetDirection);

        if (Player->HasTargetBlock)
        {
            ImBoxOutline(Frame,
                         MakeAABB((vec3)Player->TargetBlock, (vec3)Player->TargetBlock + vec3{ 1.0, 1.0 , 1.0 }),
                         PackColor(0x00, 0x00, 0x00),
                         0.0025f);

            // Breaking
            if ((OldTargetBlock == Player->TargetBlock) && Player->Control.PrimaryAction)
            {
                // HUD
                {
                    constexpr f32 OutlineSize = 1.0f;
                    constexpr f32 Height = 20.0f;
                    constexpr f32 Width = 100.0f;
                    constexpr f32 OffsetY = 200.0f;

                    vec2 P0 = { ScreenCenterP.x - 0.5f * Width, ScreenExtent.y - OffsetY - 0.5f * Height }; // Upper-left
                    vec2 P1 = P0 + vec2{Width, Height}; // Lower-right

                    ImRectOutline2D(Frame, P0, P1, PackColor(0xFF, 0xFF, 0xFF), OutlineSize, outline_type::Outer);

                    // Center
                    f32 FillRatio = World->Player.BreakTime / World->Player.BlockBreakTime;
                    f32 EndX = P0.x + FillRatio * Width;

                    ImRect2D(Frame, P0, vec2{ EndX, P1.y }, PackColor(0xFF, 0x00, 0x00));
                }

                u16 VoxelType = GetVoxelTypeAt(World, Player->TargetBlock);
                const voxel_desc* VoxelDesc = &VoxelDescs[VoxelType];
                if (VoxelDesc->Flags & VOXEL_FLAGS_SOLID)
                {
                    Player->BreakTime += dt;
                    if (Player->BreakTime >= Player->BlockBreakTime)
                    {
                        SetVoxelTypeAt(World, Player->TargetBlock, VOXEL_AIR);
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
                u16 VoxelType = GetVoxelTypeAt(World, PlacementP);
                if (VoxelType == VOXEL_AIR)
                {
#if 1
                    PlantStructure(World, World->Generator.TreeStructure, PlacementP);
                    Player->TimeSinceLastBlockPlacement = 0.0f;
#else
                    aabb PlayerBox = GetAABB(Player);
                    vec3i BoxP = PlacementP;
                    aabb BlockBox = MakeAABB((vec3)BoxP, (vec3)(BoxP + vec3i{1, 1, 1}));

                    vec3 Overlap;
                    int MinCoord;
                    if (!Intersect(PlayerBox, BlockBox, Overlap, MinCoord))
                    {
                        SetVoxelTypeAt(World, PlacementP, VOXEL_LEAVES);
                        Player->TimeSinceLastBlockPlacement = 0.0f;
                    }
#endif
                }
            }
        }
    }

    vec3 Forward, Right;
    GetHorizontalAxes(Player, Forward, Right);
    vec3 Up = { 0.0f, 0.0f, 1.0f };

    vec3 DesiredMoveDirection = NOZ(Player->Control.DesiredMoveDirection.x * Forward + Player->Control.DesiredMoveDirection.y * Right);

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
#if 1
            constexpr f32 JumpVelocity = 7.7459667f; // sqrt(2 * Gravity * DesiredJumpHeight)
#else
            constexpr f32 JumpVelocity = 25.0f;
#endif
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
    }
    else
    {
        // No head bob when not on the ground
        Player->BobAmplitude = 0.0f;

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
        vec3 UnitVelocity = NOZ(Player->Velocity);
            
        f32 Area = Lerp(SideArea, BottomArea, Abs(Dot(UnitVelocity, vec3{0.0f, 0.0f, 1.0f})));
        vec3 DragForce = (-0.5f * AirDensity * DragCoeff * Area) * Player->Velocity * Speed;
            
        constexpr f32 PlayerMass = 60.0f;
        Acceleration += DragForce * (1.0f / PlayerMass);
    }

    constexpr f32 Gravity = 25.0f;
    
    Acceleration += vec3{ 0.0f, 0.0f, -Gravity };
    Player->Velocity += Acceleration * dt;

    // Update head-bob state based on the amplitude and frequency calculated in the on-ground controls
    Player->HeadBob += Player->BobFrequency*dt;
    if (Player->HeadBob >= 1.0f)
    {
        Player->HeadBob -= 1.0f;
    }
    Player->TargetHeadBobValue = Player->BobAmplitude * Sin(2.0f * PI * Player->HeadBob);

    Player->CurrentHeadBobValue = Lerp(Player->CurrentHeadBobValue, Player->TargetHeadBobValue, 1.0f - Exp(-20.0f * dt));

    // Fov animation
#if 1
    Player->CurrentFov = Player->TargetFov = Player->DefaultFov;
#else
    {
        if (Player->WasGroundedLastFrame)
        {
            f32 PlayerSpeed = Length(Player->Velocity);
            f32 Mul = Max(Dot(Forward, DesiredMoveDirection), 0.0f);

            f32 t = Mul * Clamp((PlayerSpeed - WalkSpeed) / (RunSpeed - WalkSpeed), 0.0f, 1.0f);
            Player->TargetFov = Lerp(Player->DefaultFov, ToRadians(95.0f), t);
        }
        else
        {
            Player->TargetFov = Player->DefaultFov;
        }

        Player->CurrentFov = Lerp(Player->CurrentFov, Player->TargetFov, Clamp(1.0f - Exp(-5.0f*dt), 0.0f, 1.0f));
    }
#endif

    // Apply movement
    {
        bool WasGrounded = Player->WasGroundedLastFrame;
        vec3 dP = (Player->Velocity + 0.5f * Acceleration * dt) * dt;
        Player->WasGroundedLastFrame = false;

        vec3 Displacement = MoveEntityBy(World, Player, GetAABB(Player), dP);
        
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
                if (!WasGrounded)
                {
                    PlaySound(&Game->AudioState, Game->HitSound);
                }
            }
        }
    }
}