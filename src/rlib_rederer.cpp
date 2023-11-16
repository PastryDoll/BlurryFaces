#include "ffmpeg_decoder.cpp"

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

GuiWindowFileDialogState fileDialogState = InitGuiWindowFileDialog(GetWorkingDirectory());
char fileNameToLoad[512] = {0};
uistate UIState = NOVIDEO; 
video_decoder *VideoDecoder;
thread_manager ThreadList;
bool stop = false;
float CurrVideoTime;

void *DoRendering(frame_work_queue_memory *FrameQueueMemory)
{

    if (fileDialogState.SelectFilePressed)
        {
            if (IsFileExtension(fileDialogState.fileNameText, ".mp4") ||
                IsFileExtension(fileDialogState.fileNameText, ".mov"))
            {

                if(UIState == VIDEOSELECTED)
                {
                    CleanUpVideo(VideoDecoder);
                } 

                strcpy(fileNameToLoad, TextFormat("%s" PATH_SEPERATOR "%s", fileDialogState.dirPathText, fileDialogState.fileNameText));
                VideoDecoder = InitializeVideo(fileNameToLoad, ThreadList, FrameQueueMemory);

                int64_t totalFrames = (VideoDecoder->pFormatContext->streams[VideoDecoder->VideoStreamIndex]->duration * VideoDecoder->pFormatContext->streams[VideoDecoder->VideoStreamIndex]->time_base.den) / (VideoDecoder->pFormatContext->streams[VideoDecoder->VideoStreamIndex]->time_base.num);
                printf("Total Frames: %lld\n", totalFrames);

                printf("%s %s\n",fileDialogState.dirPathText,fileDialogState.fileNameText);
                UIState = VIDEOSELECTED;
                fileDialogState.SelectFilePressed = false;
                bool GetFirstFrame = true;
                while(GetFirstFrame)
                {
                    if (VideoDecoder->FrameToRender < VideoDecoder->FrameToGrab)
                    {
                        // u8 *TempFrame = GetFrame(VideoDecoder,&CurrVideoTime);
                        GetFirstFrame = false;
                    }
                }
                stop = true;
            }
        }
    if(UIState == VIDEOSELECTED)
    {
    }

    BeginDrawing(); 
        ClearBackground(BLACK);
        if (fileDialogState.windowActive) GuiLock();
        if (GuiButton((Rectangle){ 0, 0, 140, 30 }, GuiIconText(ICON_FILE_OPEN, "Open Image"))) fileDialogState.windowActive = true;
        GuiUnlock();
        GuiWindowFileDialog(&fileDialogState);
    EndDrawing();   

}