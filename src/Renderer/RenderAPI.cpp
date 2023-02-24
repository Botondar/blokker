#include "RenderAPI.hpp"

render_frame* BeginRenderFrame(renderer* Renderer, bool DoResize)
{
    render_frame* Frame = nullptr;
    return(Frame);
}

void EndRenderFrame(render_frame* Frame)
{
    SubmitCopyCommands;

    RecordChunkDrawCommands;
    RecordImmediateDrawCommands;

    WaitForCopyCommands;
    SubmitChunkAndImmediateDrawCommands;
    SubmitImGuiDrawCommands;
}

void SetCamera(render_frame* Frame, mat4 ViewTransform, mat4 ProjectionTransform)
{
}

vertex_buffer_block* CreateChunkMesh(render_frame* Frame, u32 VertexCount, terrain_vertex* VertexData)
{
    vertex_buffer_block* Block = nullptr;
    return(Block);
}
void FreeChunkMesh(render_frame* Frame, vertex_buffer_block* Block)
{
}

void RenderChunk(render_frame* Frame, vertex_buffer_block* Block, vec2 P)
{
}
void RenderImGui(render_frame* Frame, ImDrawData* DrawData)
{
}

void PushBox(render_frame* Frame, aabb Box, u32 Color)
{
}
void PushBoxOutline(render_frame* Frame, aabb Box, u32 Color, f32 OutlineSize)
{
}
void PushRect(render_frame* Frame, vec2 P0, vec2 P1, u32 Color)
{
}
void PushRectOutline(render_frame* Frame, vec2 P0, vec2 P1, u32 Color, f32 OutlineSize, f32 OutlineType)
{
}