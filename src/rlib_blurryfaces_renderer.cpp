#include "../externals/raylib/lib/raylib.h"
#include "../externals/raylib/lib/rlgl.h"

#define RAYGUI_IMPLEMENTATION
#include "../externals/raygui/raygui.h"

#undef RAYGUI_IMPLEMENTATION            // Avoid including raygui implementation again
#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "../externals/raygui/gui_window_file_dialog.h"

#include "blurryfaces_types.h"
#include "rlib_blurryfaces_renderer.h"
#include "rlib_blurryfaces_faces.h"


#include "ffmpeg_blurryfaces_decoder.cpp"
#include "rlib_blurryfaces_renderer_memory.cpp"
#include "rlib_blurryfaces_controller.cpp"

enum uistate 
{
    NOVIDEO,
    VIDEOSELECTED
};

#define SCREEN_WIDTH 1000
#define SCREEN_HEIGHT 600

char fileNameToLoad[512] = {0};
uistate UIState = NOVIDEO; 
video_decoder *VideoDecoder;
thread_manager ThreadList;
bool VideoStop = false;
double CurrVideoTime;
double CurrFrameTime = 0;

inline
void DoRendering(frame_work_queue_memory *FrameQueueMemory, frames_memory *RlFramesMemory, face *Face, GuiWindowFileDialogState *fileDialogState)
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
                // Present the first frame of the video
                RlFramesMemory->TempFramePtr = GetForcedFrame(VideoDecoder,RlFramesMemory->FrameImage.data, &VideoStop);
            }
        }
    if(UIState == VIDEOSELECTED)
    {
        Controller(VideoDecoder, RlFramesMemory->FrameImage.data, Face, &VideoStop);
        float currTime = GetFrameTime();
        CurrFrameTime += currTime; 

        if (!VideoStop)
        {
            // printf("FrameToFill: %llu, FrameToRender: %llu, -FrameToGrab: %llu, FrameToConvert %llu\n", VideoDecoder->FrameToFill, VideoDecoder->FrameToRender,VideoDecoder->FrameToGrab,(VideoDecoder->FrameToConvert-1));
            // printf("CurrFrameTime %f, Outro %f\n", CurrFrameTime,1/(float)VideoDecoder->Fps*2);
            // printf("Real Video Time %lf\n",VideoDecoder->CurrVideoTime);
            if (CurrFrameTime >= 1/(float)VideoDecoder->Fps || VideoDecoder->FrameToConvert == 0)
            {  
                if (VideoDecoder->FrameToRender < VideoDecoder->FrameToGrab)
                {
                    CurrFrameTime = 0;
                    RlFramesMemory->TempFramePtr = GetFrame(VideoDecoder);
                    memcpy(RlFramesMemory->FrameImage.data,RlFramesMemory->TempFramePtr, TARGET_WIDTH*TARGET_HEIGHT*3);
                }
            }
        }


        float x = ((Face->Box.x - 500)/500)*TARGET_WIDTH;
        float y = (Face->Box.y/500)*TARGET_HEIGHT;
        float w = (Face->Box.width/500)*TARGET_WIDTH;
        float h = (Face->Box.height/500)*TARGET_HEIGHT;
        // ImageCrop(&VideoDecoder->image, (Rectangle){x,y,w,h}); 
        RlFramesMemory->FrameTempImage = ImageFromImage(RlFramesMemory->FrameImage, (Rectangle){x,y,w,h});
        ImageBlurGaussian(&RlFramesMemory->FrameTempImage,15);
        ImageDraw(&RlFramesMemory->FrameImage, RlFramesMemory->FrameTempImage, (Rectangle){0, 0, (float) RlFramesMemory->FrameTempImage.width, (float) RlFramesMemory->FrameTempImage.height},(Rectangle){x , y, w, h}, WHITE);
        UnloadImage(RlFramesMemory->FrameTempImage); //big memory leak :p... for now. In the future we should
        UpdateTexture(RlFramesMemory->DisplayTexture, RlFramesMemory->FrameImage.data);
    }
    
    BeginDrawing(); 
        ClearBackground(BLACK);
        DrawTexturePro(RlFramesMemory->DisplayTexture, (Rectangle){0, 0, (float)RlFramesMemory->DisplayTexture.width, (float)RlFramesMemory->DisplayTexture.height},
            (Rectangle){500, 0, 500,500}, (Vector2){0, 0}, 0, WHITE);

        if (UIState == VIDEOSELECTED)
        {
            DrawText(TextFormat("Time: %.2lf / %.2f", VideoDecoder->CurrVideoTime, VideoDecoder->Duration), SCREEN_WIDTH-200, 100, 20, WHITE);
            // DrawText(TextFormat("Real FPS: %f / %d", (float)videoState->frameCountShow/videoState->currRealTime, videoState->FPS), 0, 150, 20, WHITE);
            // DrawText(TextFormat("Max Curr FPS: %lf / %d", 1/videoState->TimePerFrame, videoState->FPS), 0, 20, 20, WHITE);
            DrawText(TextFormat("TOTAL FRAMES: %lld",VideoDecoder->FrameToRender), SCREEN_WIDTH-215, 40, 20, WHITE);
        }
        if (fileDialogState->windowActive) GuiLock();
        if (GuiButton((Rectangle){ 0, 0, 140, 30 }, GuiIconText(ICON_FILE_OPEN, "Open Image"))) fileDialogState->windowActive = true;
        GuiUnlock();
        GuiWindowFileDialog(fileDialogState);
    EndDrawing();   

}