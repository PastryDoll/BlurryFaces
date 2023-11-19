#include "../externals/raylib/lib/raylib.h"
#include "../externals/raylib/lib/rlgl.h"

#define RAYGUI_IMPLEMENTATION
#include "../externals/raygui/raygui.h"

#undef RAYGUI_IMPLEMENTATION            // Avoid including raygui implementation again
#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "../externals/raygui/gui_window_file_dialog.h"

#include "blurryfaces_types.h"
#include "rlib_blurryfaces_renderer.h"

#include "ffmpeg_blurryfaces_decoder.cpp"
#include "rlib_blurryfaces_renderer_memory.cpp"
#include "rlib_blurryfaces_controller.cpp"

enum uistate 
{
    NOVIDEO,
    VIDEOSELECTED
};

char fileNameToLoad[512] = {0};
uistate UIState = NOVIDEO; 
video_decoder *VideoDecoder;
thread_manager ThreadList;
bool VideoStop = false;
double CurrVideoTime;
double CurrFrameTime = 0;

inline
void DoRendering(frame_work_queue_memory *FrameQueueMemory, frames_memory *RlFramesMemory, GuiWindowFileDialogState *fileDialogState)
{

    // Video Selection - CleanOld - Initialize New - Grab First Frame
    if (fileDialogState->SelectFilePressed)
        {
            if (IsFileExtension(fileDialogState->fileNameText, ".mp4") ||
                IsFileExtension(fileDialogState->fileNameText, ".mov"))
            {

                if(UIState == VIDEOSELECTED)
                {
                    CleanUpVideo(VideoDecoder);
                } 

                strcpy(fileNameToLoad, TextFormat("%s" PATH_SEPERATOR "%s", fileDialogState->dirPathText, fileDialogState->fileNameText));
                VideoDecoder = InitializeVideo(fileNameToLoad, FrameQueueMemory);
                int64_t totalFrames = (VideoDecoder->pFormatContext->streams[VideoDecoder->VideoStreamIndex]->duration * VideoDecoder->pFormatContext->streams[VideoDecoder->VideoStreamIndex]->time_base.den) / (VideoDecoder->pFormatContext->streams[VideoDecoder->VideoStreamIndex]->time_base.num);
                printf("Total Frames: %lld\n", totalFrames);
                printf("%s %s\n",fileDialogState->dirPathText,fileDialogState->fileNameText);
                UIState = VIDEOSELECTED;
                fileDialogState->SelectFilePressed = false;
                bool GetFirstFrame = true;
                while(GetFirstFrame)
                {
                    if (VideoDecoder->FrameToRender < VideoDecoder->FrameToGrab)
                    {
                        printf("oi\n");
                        RlFramesMemory->TempFramePtr = GetFrame(VideoDecoder);
                        GetFirstFrame = false;
                    }
                }
                // stop = true;
            }
        }
    if(UIState == VIDEOSELECTED)
    {
        Controller(VideoDecoder, &RlFramesMemory->TempFramePtr,&VideoStop);
        float currTime = GetFrameTime();
        CurrFrameTime += currTime; 

        if (!VideoStop)
            {
                // printf("CurrFrameTime %f, Outro %f\n", CurrFrameTime,1/(float)VideoDecoder->Fps*2);
                printf("Real Video Time %lf\n",CurrVideoTime);
                if (CurrFrameTime >= 1/(float)VideoDecoder->Fps || VideoDecoder->FrameToConvert == 0)
                {  
                    if (VideoDecoder->FrameToRender < VideoDecoder->FrameToGrab)
                    {
                        CurrFrameTime = 0;
                        RlFramesMemory->TempFramePtr = GetFrame(VideoDecoder);
                    }
                }
            }
        UpdateTexture(RlFramesMemory->DisplayTexture, RlFramesMemory->TempFramePtr);
    }
    
    BeginDrawing(); 
        ClearBackground(BLACK);
        DrawTexturePro(RlFramesMemory->DisplayTexture, (Rectangle){0, 0, (float)RlFramesMemory->DisplayTexture.width, (float)RlFramesMemory->DisplayTexture.height},
            (Rectangle){500, 0, 500,500}, (Vector2){0, 0}, 0, WHITE);
        if (fileDialogState->windowActive) GuiLock();
        if (GuiButton((Rectangle){ 0, 0, 140, 30 }, GuiIconText(ICON_FILE_OPEN, "Open Image"))) fileDialogState->windowActive = true;
        GuiUnlock();
        GuiWindowFileDialog(fileDialogState);
    EndDrawing();   

}