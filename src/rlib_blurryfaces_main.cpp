#include "rlib_blurryfaces_renderer.cpp"
#include "blurryfaces_output.cpp"


int main(void)
{   
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "BlurryFaces");

    frame_work_queue_memory *FrameQueueMemory = InitializeFrameQueueMemory();
    frames_memory *RlFramesMemory = InitializeFramesMemory();
    CreateProject();

    SetTargetFPS(120);
    GuiWindowFileDialogState fileDialogState = InitGuiWindowFileDialog(GetWorkingDirectory());

    while (!WindowShouldClose())    // Detect window close button or ESC key
    {   
        DoRendering(FrameQueueMemory, RlFramesMemory, &fileDialogState);

    }

    CloseWindow();                  // Close window and OpenGL context
    return 0;

}