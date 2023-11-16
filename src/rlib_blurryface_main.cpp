#include "../externals/raylib/lib/raylib.h"
#include "rlib_rederer.cpp"

#define SCREEN_WIDTH 1000
#define SCREEN_HEIGHT 600

int main(void)
{   
    frame_work_queue_memory *FrameQueueMemory = AllocFrameQueue();

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "BlurryFaces");
    SetTargetFPS(120);

    while (!WindowShouldClose())    // Detect window close button or ESC key
    {   
        DoRendering(FrameQueueMemory);
    }

    CloseWindow();                  // Close window and OpenGL context
    return 0;

}