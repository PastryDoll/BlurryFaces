#include "rlib_renderer_memory.cpp"

#define RAYGUI_IMPLEMENTATION
#include "../externals/raygui/raygui.h"

#undef RAYGUI_IMPLEMENTATION            // Avoid including raygui implementation again
#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "../externals/raygui/gui_window_file_dialog.h"

enum uistate 
{
    NOVIDEO,
    VIDEOSELECTED
};

char fileNameToLoad[512] = {0};
uistate UIState = NOVIDEO; 
video_decoder *VideoDecoder;
thread_manager ThreadList;
bool stop = false;
float CurrVideoTime = 0;

void DoRendering(frame_work_queue_memory *FrameQueueMemory, frames_memory *RlFramesMemory, GuiWindowFileDialogState *fileDialogState)
{

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
                        RlFramesMemory->TempFramePtr = GetFrame(VideoDecoder);//,&CurrVideoTime);
                        GetFirstFrame = false;
                    }
                }
                // stop = true;
            }
        }
    if(UIState == VIDEOSELECTED)
    {
        float currTime = GetFrameTime();
        CurrVideoTime += currTime; 

        if (!stop)
            {
                // videoState->currRealTime += currTime;
                
                printf("CurrVideoTime %f, Outro %f\n", CurrVideoTime,1/(float)VideoDecoder->Fps*2);
                if (CurrVideoTime >= 1/(float)VideoDecoder->Fps || VideoDecoder->FrameToConvert == 0)
                {  
                    if (VideoDecoder->FrameToRender < VideoDecoder->FrameToGrab)
                    {
                        CurrVideoTime = 0;
                        RlFramesMemory->TempFramePtr = GetFrame(VideoDecoder);//,&CurrVideoTime);
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