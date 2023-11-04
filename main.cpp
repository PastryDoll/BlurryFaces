
#include "math.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>         // Required for: malloc() and free()
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <opencv2/opencv.hpp>
#include <semaphore.h>
#include <stdlib.h>
#include <dispatch/dispatch.h>


extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
}

#include "raylib.h"
#include "rlgl.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#undef RAYGUI_IMPLEMENTATION            // Avoid including raygui implementation again
#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "gui_window_file_dialog.h"

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))
#define CompletePastWriteBeforeFutureWrite asm volatile("" : : : "memory");


typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#define SCREEN_WIDTH 1000
#define SCREEN_HEIGHT 600
#define VIDEO_WIDTH 500
#define VIDEO_HEIGHT 500
#define NUM_THREADS 2
#define MAX_NUMBER_FACES 10

enum UIState 
{
    NOVIDEO,
    VIDEOSELECTED
};

struct VideoDecodingCtx
{
    AVFormatContext *pFormatContext;
    s32 videoStreamIndex;
    AVCodecContext *pVideoCodecCtx;
    AVPacket *pPacket;
    struct SwsContext *sws_ctx;
    u64 volatile *frameCountDraw;
    bool *stop;

};

struct frame_work_queue_entry //We need to free this
{
    AVFrame *Frame = av_frame_alloc();
    AVFrame *pRGBFrame = av_frame_alloc();

};

struct Face
{
    Rectangle Box;
    bool inside;
    bool selected;
    bool done;
};

struct VideoState 
{
    struct SwsContext *sws_ctx;

    Image image;
    Image tempImage;
    Texture texture;

    Face face[MAX_NUMBER_FACES];
    bool Incremental;
    bool Decrement;
    bool Blurring;
    
    AVFrame *pRGBFrameTemp;
    AVFrame *pRGBFramePrevTemp;
    AVFrame *pRGBFrame;
    

    s32 videoStreamIndex;
    AVFormatContext *pFormatContext;
    AVCodecContext *pVideoCodecCtx;
    AVPacket *pPacket;

    float TimePerFrame;
    u64 volatile frameCountDraw;
    u64 volatile frameCountShow;
    u64 volatile blah;
    u32 height;
    u32 width;

    float currRealTime;
    float currVideoTime;
    float currFrameTime;

    float speed;
    float duration;
    bool stop;
    double timebase;
    u32 FPS;

    dispatch_semaphore_t SemaphoreHandle;

};


// A very poor and maybe bad idea of circular buffering for 
// saving memory and maybe some hot memory use
const int gap = 16;
frame_work_queue_entry FrameQueue[gap];
static u32 volatile FrameNextEntryToFill = 0;
static bool volatile DecodingThread = true;
int currentGesture = GESTURE_NONE;
int lastGesture = GESTURE_NONE;
pthread_t threads[NUM_THREADS];
//We need to improve the use of semaphres so that the thread will not be running so frealy when 
//they dont satisfy the if condition
//We also have to learn how to use in the same way the POSIX lib semaphore
sem_t pauseSemaphore;
dispatch_semaphore_t semaphore;
bool activeBlur;
bool activeSelect;


void CleanUpVideo(VideoState *VideoState)
{
    printf("0\n");
    DecodingThread = false;
    printf("1\n");
    dispatch_semaphore_signal(semaphore);
    printf("2\n");
    dispatch_semaphore_signal(semaphore);
    printf("3\n");
    pthread_join(threads[0], NULL);
    printf("4\n");
    pthread_join(threads[1], NULL);
    printf("5\n");
    sws_freeContext(VideoState->sws_ctx);
    printf("6\n");
    UnloadImage(VideoState->image);
    printf("7\n");
    UnloadImage(VideoState->tempImage);
    printf("8\n");
    UnloadTexture(VideoState->texture);
    printf("9\n");
    if (VideoState->pRGBFrame->data[0]) av_frame_free(&VideoState->pRGBFrame);
    printf("10\n");
    if (VideoState->pRGBFrameTemp->data[0]) av_frame_free(&VideoState->pRGBFrameTemp);
    printf("11\n");
    avformat_close_input(&VideoState->pFormatContext);
    printf("12\n");
    av_packet_unref(VideoState->pPacket);
    printf("13\n");
    av_packet_free(&VideoState->pPacket);
    printf("14\n");
    avcodec_free_context(&VideoState->pVideoCodecCtx);
    printf("15\n");
    free(VideoState);
    printf("16\n");
    DecodingThread = true;
    printf("17\n");
    FrameNextEntryToFill = 0;
    printf("18\n");
}

//TODO WE NEED TO UNDERSTAND HOW TO RUN CODEC IN MULTITHREAD (THIS IS NATIVE TO FFMPEG)
// MAYBE WITH THAT WE CAN REMOVE THIS STUPID THREADING I MADE
void* DoDecoding(void* arg)
{      
    VideoState *videoState = (VideoState *)arg;
    while (DecodingThread)
    {
        
        //TODO maybe do an atomic load of videoState->stop;
        if (videoState->stop) dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
        // printf("alive1\n");
        assert(FrameNextEntryToFill >= videoState->frameCountDraw);
        if ((FrameNextEntryToFill - videoState->frameCountDraw < gap))
        {
            if (av_read_frame(videoState->pFormatContext, videoState->pPacket) < 0){break;}

            if (videoState->pPacket->stream_index == videoState->videoStreamIndex) 
            {   
                    frame_work_queue_entry* pFrame = FrameQueue + FrameNextEntryToFill%gap;
                    
                    avcodec_send_packet(videoState->pVideoCodecCtx, videoState->pPacket);
                    avcodec_receive_frame(videoState->pVideoCodecCtx, pFrame->Frame);
                    av_packet_unref(videoState->pPacket);
                    printf("NextEntryToFill: %u, videoState->frameCountDraw: %llu\n" , FrameNextEntryToFill%gap,videoState->frameCountDraw%gap);
                    __sync_add_and_fetch(&FrameNextEntryToFill, 1);
            }
        }
        else
        {
            // printf("waste of cpu 1\n");
        }
    }
    pthread_exit(NULL);
}

void* UpdateFrame(void* arg)
{
    VideoState *videoState = (VideoState *)arg;

    while (DecodingThread)
    {
        //TODO maybe do an atomic load of videoState->stop;
        if (videoState->stop) dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
        // printf("alive2 %lf\n", GetTime());
        int32_t OriginalFrameCountDraw = videoState->frameCountDraw;

        assert(videoState->frameCountDraw >= videoState->frameCountShow);
        // Okay so this -1 is subtle and we might fix this soon..
        // What happens is that when we update the texture we can be using previous frame...
        // i.e lets say we grab RGB framecounttoshow 10... we make a texture and uptade framecountoshow 11
        // now framecounttoshow 10 can be changed and we will keep updating the texture with it untill we get right FPS
        if (OriginalFrameCountDraw - videoState->frameCountShow < gap-1)
        {

            if ((OriginalFrameCountDraw < FrameNextEntryToFill) && FrameNextEntryToFill > 0) //If frameCount too close to NextEntry it fails.. idk why for now
            {   

                int32_t InitialValue = __sync_val_compare_and_swap(&videoState->frameCountDraw, OriginalFrameCountDraw, OriginalFrameCountDraw+1);

                if (InitialValue == OriginalFrameCountDraw) //If the original is what we thought it should be
                {
                    frame_work_queue_entry* FramePtr = FrameQueue + (videoState->frameCountDraw-1)%gap;
                    sws_scale(videoState->sws_ctx, FramePtr->Frame->data, FramePtr->Frame->linesize, 0,
                                FramePtr->Frame->height, FramePtr->pRGBFrame->data, FramePtr->pRGBFrame->linesize);
                    __sync_add_and_fetch(&videoState->blah,1);
                    // printf("frameCountShow: %llu, -blah: %llu, frameCountDraw %llu\n",videoState->frameCountShow,videoState->blah,(videoState->frameCountDraw-1));
                    // printf("frameCountShow: %llu, -blah: %llu, frameCountDraw %llu\n",videoState->frameCountShow%gap,videoState->blah%gap,(videoState->frameCountDraw-1)%gap);
                    
                }
            }
        }
        else
        {
            // printf("waste of cpu 2\n");
        }
    }
    pthread_exit(NULL);
}

inline
AVFrame *GetFrame(VideoState *videoState)
{
    assert(videoState->frameCountShow < videoState->blah);
    frame_work_queue_entry* FramePtr = FrameQueue + (videoState->frameCountShow)%gap;
    videoState->currVideoTime = (double)FramePtr->Frame->pts*videoState->timebase;
    __sync_add_and_fetch(&videoState->frameCountShow,1);
    return FramePtr->pRGBFrame;
}

inline
void Pause(VideoState *videoState)
{
    videoState->stop = !videoState->stop;
    if (!videoState->stop)
    {
        dispatch_semaphore_signal(semaphore);
        dispatch_semaphore_signal(semaphore);
    }
    printf("Pause: %i\n", videoState->stop);
}

bool done = false;
bool selected;
float initialx;
float initialy;
inline
void Controller(VideoState * videoState)
{
    static int counter= 0;
    static int currNumberFaces = 1;
    bool insideAnyFace = false;
    assert(currNumberFaces < MAX_NUMBER_FACES);
    if (IsKeyPressed(KEY_SPACE))
    {
        Pause(videoState);
    }

    if (IsKeyPressed(KEY_RIGHT))
    {

        if (videoState->stop)
        {
            videoState->stop = false;
            dispatch_semaphore_signal(semaphore);
            dispatch_semaphore_signal(semaphore);   
        }

        bool getFrame = true;
        while(getFrame)
        {
            if (videoState->frameCountShow < videoState->blah)
            {
                videoState->pRGBFrameTemp = GetFrame(videoState);
                getFrame = false;
                videoState->stop = true;
            }
        }
    }

    if (IsKeyDown(KEY_RIGHT))
    {
        counter++;
        if (counter > 1000)
        {
            if (!videoState->stop) videoState->stop = !videoState->stop;
            videoState->Decrement = false;
            videoState->Incremental = true; 
        }
    }

    if (IsKeyReleased(KEY_RIGHT))
    {
        counter = 0;
    }

    if (IsKeyPressed(KEY_LEFT))
    {
        if (!videoState->stop) videoState->stop = !videoState->stop;
        videoState->Incremental = false;
        videoState->Decrement = true;
    }


    currentGesture = GetGestureDetected();

    for (int i = 0; i < currNumberFaces; i++)
    {
        Face *face = videoState->face + i;
        if (CheckCollisionPointRec((Vector2){(float)GetMouseX(),(float)GetMouseY()},(Rectangle){face->Box.x,face->Box.y,face->Box.width,face->Box.height}) && (face->done))
        {
            insideAnyFace = true;
            break;
        }
    }

    //Deselect faces clicked outside
    if ((currentGesture == GESTURE_TAP) && !insideAnyFace)
    {
        for (int i = 0; i < currNumberFaces; i++)
        {
            Face *face = videoState->face + i;
            face->selected = false;
        }

    }


    for (int i = 0; i < currNumberFaces; i++)
    {
        Face *face = videoState->face + i;

        if (IsKeyPressed(KEY_BACKSPACE))
        {
            if(face->selected)
            {
                face->Box = (Rectangle){0,0,0,0};
                face->done = 0;
                face->selected = 0;
                face->inside = 0;

            }
        }

        face->inside = (CheckCollisionPointRec((Vector2){(float)GetMouseX(),(float)GetMouseY()},(Rectangle){face->Box.x,face->Box.y,face->Box.width,face->Box.height}) && (face->done));

        //We are creating Faces.. not multi selecting them
        if (activeBlur || IsKeyDown(KEY_TAB))
        {
            // Start drawing box
            if ((currentGesture & (GESTURE_TAP|GESTURE_HOLD)) && !face->done && !insideAnyFace)
            {
                face->Box.x = GetMouseX();
                face->Box.y = GetMouseY();
                face->Box.width = 0;
                face->Box.height = 0;
            }
            //Drag for size
            if ((currentGesture == GESTURE_DRAG) && !face->done && !insideAnyFace)
            {   
                face->Box.width = GetMouseX() - face->Box.x;
                face->Box.height = GetMouseY() - face->Box.y;

            }
        }
        //Ended Drawing box
        if (face->Box.width > 0 && currentGesture == GESTURE_NONE && !face->done) 
        {
            printf("done\n");
            face->done = true;
            currNumberFaces++;
        } 

        //Select a face
        if ((currentGesture & (GESTURE_TAP)) && (face->inside))
        {
            Vector2 pos = GetMousePosition();
            initialx = pos.x;
            initialy = pos.y;
            face->selected = true;
        }
        
        //Drag Face
        if ((currentGesture & (GESTURE_DRAG)) && (face->inside) && (face->selected))
        {
            Vector2 pos1 = GetMousePosition();
            // printf("Initialx : %f, dragx: %f, %lf\n", initialx, pos1.x, GetTime());
            face->Box.x += pos1.x - initialx;
            face->Box.y += pos1.y - initialy;
            initialx = pos1.x;
            initialy = pos1.y;
        }
    }

}

void SpeedLogic(VideoState *videoState, s8 direction)
{
    switch (direction)
    {
        case -1:
        {
            videoState->speed = (videoState->speed > (float)4) ? (float)8 : (float)videoState->speed*2;
    
        }
        break;

        case 1:
        {
            videoState->speed = (videoState->speed < (float)1/2) ? (float)1/4 : videoState->speed/2;
        }
        break;
    }
    printf("Speed: %f\n", videoState->speed);

}

void Slider(VideoState *VideoState)
{
    int heigth = 100;
    int lenght = 500;
    float posSlider = VideoState->currVideoTime*((float)lenght/VideoState->duration);
    DrawRectangle(0,(int)(SCREEN_HEIGHT - heigth),lenght, heigth, CLITERAL(Color){ 64, 64, 64, 50});
    DrawLineEx((Vector2){posSlider,(float)SCREEN_HEIGHT},(Vector2){posSlider,(float)(SCREEN_HEIGHT - heigth)},2,RED);
}

VideoState *InitializeVideo(const char *path)
{
    // Open video file
    AVFormatContext *pFormatContext = avformat_alloc_context(); // = (NULL) ?
    avformat_open_input(&pFormatContext, path, NULL, NULL); //This reads the header (maybe not codec)
    // printf("Format %s, duration %lld us", pFormatContext->iformat->long_name, pFormatContext->duration);
    avformat_find_stream_info(pFormatContext, NULL); //get streams

    //TODO - Support Audio ???
    // Find video stream
    s32 videoStreamIndex = -1;
    
    // For each stream, we're going to keep the AVCodecParameters, which describes the properties of a codec used by the stream i.
    for (u32 i = 0; i < pFormatContext->nb_streams; i++) {
        if (pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            break;
        }
    }

    AVStream *VideoStream = pFormatContext->streams[videoStreamIndex];

    // Get codec parameters
    AVCodecParameters *pVideoCodecParams = VideoStream->codecpar;

    //With the codec properties we can look up the proper CODEC querying the function avcodec_find_decoder and find the 
    // registered decoder for the codec id and return an AVCodec, the component that knows how to enCOde and DECode the stream.
    const AVCodec *pVideoCodec = avcodec_find_decoder(pVideoCodecParams->codec_id);

    //TODO WE NEED TO UNDERSTAND HOW TO RUN CODEC IN MULTITHREAD (THIS IS NATIVE TO FFMPEG)
    // MAYBE WITH THAT WE CAN REMOVE THIS STUPID THREADING I MADE

    // Create codec context for the video stream
    AVCodecContext *pVideoCodecCtx = avcodec_alloc_context3(pVideoCodec);
    avcodec_parameters_to_context(pVideoCodecCtx, pVideoCodecParams);
    avcodec_open2(pVideoCodecCtx, pVideoCodec, NULL);


    u32 FPS = VideoStream->avg_frame_rate.num / VideoStream->avg_frame_rate.den;
    double time_base = av_q2d(VideoStream->time_base);
    float duration = VideoStream->duration*time_base;

    u32 VideoWidth = pVideoCodecCtx->width;
    u32 VideoHeight = pVideoCodecCtx->height;

    struct SwsContext *sws_ctx         = sws_alloc_context();
    sws_ctx = sws_getContext(VideoWidth, VideoHeight, pVideoCodecCtx->pix_fmt,
                             1080, 720, AV_PIX_FMT_RGB24,
                             SWS_FAST_BILINEAR, 0, 0, 0);

    // Allocate buffer for RGB data
    AVFrame *pRGBFrame = av_frame_alloc();
    pRGBFrame->format = AV_PIX_FMT_RGB24; //RGB24 is 8 bits per channel (8*3)
    pRGBFrame->width  = 1080;
    pRGBFrame->height = 720;
    pRGBFrame->linesize[0] = 1080 * 3;
    av_frame_get_buffer(pRGBFrame, 0);
    AVFrame *pRGBFrameTemp = av_frame_alloc();
    pRGBFrameTemp->format = AV_PIX_FMT_RGB24; //RGB24 is 8 bits per channel (8*3)
    pRGBFrameTemp->width  = 1080;
    pRGBFrameTemp->height = 720;
    pRGBFrameTemp->linesize[0] = 1080 * 3;
    av_frame_get_buffer(pRGBFrameTemp, 0);

    for (u32 i = 0; i < gap; i++)
    {
        AVFrame *pRGBFrame = av_frame_alloc();
        pRGBFrame->format = AV_PIX_FMT_RGB24; //RGB24 is 8 bits per channel (8*3)
        pRGBFrame->width  = 1080;
        pRGBFrame->height = 720;
        pRGBFrame->linesize[0] = 1080 * 3;
        av_frame_get_buffer(pRGBFrame, 0);
        FrameQueue[i].pRGBFrame = pRGBFrame;
    }

    Texture texture = {0};
    texture.width   = 1080;
    texture.height  = 720;
    texture.format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
    texture.mipmaps = 1;
    texture.id = rlLoadTexture(NULL, texture.width, texture.height, texture.format, texture.mipmaps);

    Image image = {0};
    image.width = 1080;
    image.height = 720;
    image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
    image.mipmaps = 1;
    image.data = (u_int8_t*)malloc(1080*720*3);


    Image tempImage = {0};

    VideoState *videoState = (VideoState*)malloc(sizeof(VideoState));
    videoState->frameCountDraw = 0;
    videoState->frameCountShow = 0;
    videoState->duration = duration;
    videoState->sws_ctx = sws_ctx;
    videoState->timebase = time_base;
    videoState->stop = false;
    videoState->image = image;
    videoState->pRGBFrameTemp = pRGBFrameTemp;
    videoState->pRGBFrame = pRGBFrame;
    videoState->TimePerFrame = 1/(float)FPS;
    videoState->tempImage = tempImage;
    for (int i = 0; i < MAX_NUMBER_FACES; i++)
    {
        videoState->face[i] = {{0,0,0,0},false,false,false};

    }
    videoState->texture = texture;
    videoState->pFormatContext = pFormatContext;
    videoState->pVideoCodecCtx = pVideoCodecCtx;
    videoState->videoStreamIndex = videoStreamIndex;
    videoState->pPacket = av_packet_alloc();
    videoState->duration = duration;
    videoState->FPS = FPS;
    videoState->speed = 1;
    videoState->Blurring = false;
    // videoState->SemaphoreHandle = semaphore;
    
    if (pthread_create(&threads[0], NULL, DoDecoding, videoState) != 0)
    {
        perror("pthread_create");
        return NULL;
    }
    if (pthread_create(&threads[1], NULL, UpdateFrame, videoState) != 0)
    {
        perror("pthread_create");
        return NULL;
    }
    return videoState;
}

int main(void)
{   
    semaphore = dispatch_semaphore_create(0);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "BlurryFaces");

    GuiWindowFileDialogState fileDialogState = InitGuiWindowFileDialog(GetWorkingDirectory());
    char fileNameToLoad[512] = {0};
    VideoState *videoState;
    UIState UIstate = NOVIDEO; 
    
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {   
        // int CurrScreenHeight = GetScreenHeight();
        // int CurrScreenWidth = GetScreenWidth();
        
        if (fileDialogState.SelectFilePressed)
        {
            if (IsFileExtension(fileDialogState.fileNameText, ".mp4") ||
                IsFileExtension(fileDialogState.fileNameText, ".mov"))
            {

                if(UIstate)
                {
                    CleanUpVideo(videoState);
                } 
                strcpy(fileNameToLoad, TextFormat("%s" PATH_SEPERATOR "%s", fileDialogState.dirPathText, fileDialogState.fileNameText));
                videoState = InitializeVideo(fileNameToLoad);
                printf("%s %s\n",fileDialogState.dirPathText,fileDialogState.fileNameText);
                UIstate = VIDEOSELECTED;
                fileDialogState.SelectFilePressed = false;
                bool getFirst = true;
                while(getFirst)
                {
                    if (videoState->frameCountShow < videoState->blah)
                    {
                        videoState->pRGBFrameTemp = GetFrame(videoState);
                        getFirst = false;
                    }
                    videoState->stop = true;
                }
            }
        }

        if (UIstate)
        {   
           
            // printf("Frame Draw: %llu,Frame Show: %llu\n", videoState->frameCountDraw, videoState->frameCountShow);
            // printf("----\n");
            Controller(videoState);
            float currTime = GetFrameTime();
            videoState->currFrameTime += currTime; 

            if (!videoState->stop)
            {
                videoState->currRealTime += currTime;
                
                if (videoState->currFrameTime >= videoState->TimePerFrame*videoState->speed || videoState->frameCountDraw == 0)
                {  
                    if (videoState->frameCountShow < videoState->blah)
                    {
                        videoState->currFrameTime = 0;
                        videoState->pRGBFrameTemp = GetFrame(videoState);
                    }
                }
            }
            // Okay so this -1 is subtle and we might fix this soon..
            // What happens is that when we update the texture we can be using previous frame...
            // i.e lets say we grab RGB framecounttoshow 10... we make a texture and uptade framecountoshow 11
            // now framecounttoshow 10 can be changed and we will keep updating the texture with it untill we get right FPS
            memcpy(videoState->image.data, videoState->pRGBFrameTemp->data[0], 1080*720*3);
            cv::Mat opencvMat(720, 1080, CV_8UC3, videoState->image.data);
            std::vector<cv::Point2f> features;
            cv::cvtColor(opencvMat, opencvMat, cv::COLOR_RGB2GRAY);
            cv::goodFeaturesToTrack(opencvMat, features, 30, 0.01, 10);
            UpdateTexture(videoState->texture,opencvMat.data);
            for (int i = 0; i<MAX_NUMBER_FACES; i++)
            {
                float x = ((videoState->face[i].Box.x - 500)/500)*1080;
                float y = (videoState->face[i].Box.y/500)*720;
                float w = (videoState->face[i].Box.width/500)*1080;
                float h = (videoState->face[i].Box.height/500)*720;
                // ImageCrop(&videoState->image, (Rectangle){x,y,w,h}); 
                videoState->tempImage = ImageFromImage(videoState->image, (Rectangle){x,y,w,h});
                ImageBlurGaussian(&videoState->tempImage,15);
                ImageDraw(&videoState->image, videoState->tempImage, (Rectangle){0, 0, (float)videoState->tempImage.width, (float)videoState->tempImage.height},(Rectangle){x , y, w, h}, WHITE);
                UnloadImage(videoState->tempImage); //big memory leak :p... for now. In the future we should
                // mofidy this blurring pipeline

            }
            // UpdateTexture(videoState->texture,videoState->image.data);
            //This needs to be here ?? well this is actually the end of the use of the pointer
            //Not if we are using the gap-1
            // if(videoState->currFrameTime == 0)__sync_add_and_fetch(&videoState->frameCountShow,1);

        }
        BeginDrawing(); 
            // ClearBackground(CLITERAL(Color){ 59, 0, 161, 255 });
            ClearBackground(BLACK);
            if (UIstate)
            {
                DrawTexturePro(videoState->texture, (Rectangle){0, 0, (float)videoState->texture.width, (float)videoState->texture.height},
                        (Rectangle){500, 0, 500,500}, (Vector2){0, 0}, 0, WHITE);
                for (int i = 0; i<MAX_NUMBER_FACES; i++)
                {
                    DrawRectangleRoundedLines(videoState->face[i].Box, 2, 2, 2, videoState->face[i].selected ? GREEN : RED);
                }
                Slider(videoState);
                DrawText(TextFormat("Time: %.2lf / %.2f", videoState->currVideoTime, videoState->duration), SCREEN_WIDTH-200, 0, 20, WHITE);
                DrawText(TextFormat("Real Time: %.2f", videoState->currRealTime), SCREEN_WIDTH-200, 80, 20, WHITE);
                DrawText(TextFormat("Real FPS: %f / %d", (float)videoState->frameCountShow/videoState->currRealTime, videoState->FPS), 0, 0, 20, WHITE);
                DrawText(TextFormat("Max Curr FPS: %lf / %d", 1/videoState->TimePerFrame, videoState->FPS), 0, 20, 20, WHITE);
                DrawText(TextFormat("TOTAL FRAMES: %lld",videoState->frameCountDraw), SCREEN_WIDTH-215, 40, 20, WHITE);
                if (GuiButton((Rectangle){ 0, 280, 20, 20 }, GuiIconText(ICON_PLAYER_PAUSE, ""))) videoState->stop = true;
                if (GuiButton((Rectangle){ 20, 280, 20, 20 }, GuiIconText(ICON_PLAYER_PLAY, ""))) Pause(videoState);
                GuiToggle((Rectangle){ 40, 280, 20, 20 }, GuiIconText(ICON_DEMON, ""),&activeBlur);
                if (activeBlur)
                {
                    videoState->Blurring = true;
                }
                else
                {   
                    videoState->Blurring = false;
                }
                if (GuiButton((Rectangle){ 20, 300, 20, 20 }, GuiIconText(ICON_ARROW_RIGHT, ""))) SpeedLogic(videoState, 1);
                if (GuiButton((Rectangle){ 0, 300, 20, 20 }, GuiIconText(ICON_ARROW_LEFT, ""))) SpeedLogic(videoState, -1);
            }

            DrawFPS(0,40);
            if (fileDialogState.windowActive) GuiLock();
            if (GuiButton((Rectangle){ 0, 0, 140, 30 }, GuiIconText(ICON_FILE_OPEN, "Open Image"))) fileDialogState.windowActive = true;
            // GuiToggle((Rectangle){ 40, 300, 20, 20 }, GuiIconText(ICON_CURSOR_CLASSIC, ""),&activeSelect);
            // if (activeSelect)
            // {
            //     videoState->Blurring = false;
            // }

            GuiUnlock();
            // GUI: Dialog Window
            //--------------------------------------------------------------------------------
            GuiWindowFileDialog(&fileDialogState);
            //--------------------------------------------------------------------------------
        EndDrawing();   
    }
    // Cleanup resources
    // TODO -- STOP THREAD
    DecodingThread = false;
    dispatch_semaphore_signal(semaphore);
    dispatch_semaphore_signal(semaphore);
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    printf("Decoding Thread Closed\n");
    if (videoState)
    {
        CleanUpVideo(videoState);
    }
    CloseWindow();                  // Close window and OpenGL context
    return 0;
}
