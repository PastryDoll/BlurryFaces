#include "rlib_rederer.cpp"

#define SCREEN_WIDTH 1000
#define SCREEN_HEIGHT 600

int main(void)
{   
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "BlurryFaces");
    
    frame_work_queue_memory *FrameQueueMemory = InitializeFrameQueueMemory();
    frames_memory *RlFramesMemory = InitializeFramesMemory();

    SetTargetFPS(120);
    GuiWindowFileDialogState fileDialogState = InitGuiWindowFileDialog(GetWorkingDirectory());

    while (!WindowShouldClose())    // Detect window close button or ESC key
    {   
        DoRendering(FrameQueueMemory, RlFramesMemory, &fileDialogState);

    }

    CloseWindow();                  // Close window and OpenGL context
    return 0;

}