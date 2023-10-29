
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
#include <dispatch/dispatch.h>
#include <stdlib.h>

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
    AVFrame *pRGBFrame;

};

struct texture_work_queue_entry //We need to free this
{
    AVFrame *pRGBFrame;
};

struct VideoState 
{
    struct SwsContext *sws_ctx;
    AVFrame *currFrame;
    Image image;
    Image tempImage;
    Rectangle face;
    AVFrame *pRGBFrameTemp;
    AVFrame *pRGBFrame;
    Texture texture;
    float TimePerFrame;
    u64 volatile frameCountDraw;
    u64 volatile frameCountShow;
    u64 volatile blah;
    u32 height;
    u32 width;
    float currRealTime;
    float currVideoTime;
    float currFrameTime;
    float duration;
    bool stop;
    bool Incremental;
    bool Decrement;
    double timebase;

    AVFormatContext *pFormatContext;
    s32 videoStreamIndex;
    AVCodecContext *pVideoCodecCtx;
    AVPacket *pPacket;

};

Rectangle Faces[10];
// A very poor and maybe bad idea of circular buffering for 
// saving memory and maybe some hot memory use
const int gap = 4;
frame_work_queue_entry FrameQueue[gap];
texture_work_queue_entry TextureQueue[gap];
static u32 volatile FrameNextEntryToFill = 0;
static bool volatile DecodingThread = true;
int currentGesture = GESTURE_NONE;
int lastGesture = GESTURE_NONE;
pthread_t threads[NUM_THREADS];


//TODO WE NEED TO UNDERSTAND HOW TO RUN CODEC IN MULTITHREAD (THIS IS NATIVE TO FFMPEG)
// MAYBE WITH THAT WE CAN REMOVE THIS STUPID THREADING I MADE
void* DoDecoding(void* arg)
{      
    VideoState *videoState = (VideoState *)arg;
    while (DecodingThread)
    {
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
                    __sync_add_and_fetch(&FrameNextEntryToFill, 1);
            }
        }
    }
    pthread_exit(NULL);
}

void* UpdateFrame(void* arg)
{
    VideoState *videoState = (VideoState *)arg;
    // pthread_t thread_id = pthread_self(); // Get the thread ID

    while (DecodingThread)
    {

        int32_t OriginalFrameCount = videoState->frameCountDraw;

        assert(OriginalFrameCount >= videoState->frameCountShow);
        if (OriginalFrameCount - videoState->frameCountShow < gap-1)
        {
            if ((OriginalFrameCount < FrameNextEntryToFill) && FrameNextEntryToFill > 0) //If frameCount too close to NextEntry it fails.. idk why for now
            {   

                int32_t InitialValue = __sync_val_compare_and_swap(&videoState->frameCountDraw, OriginalFrameCount, OriginalFrameCount+1);

                if (InitialValue == OriginalFrameCount) //If the original is what we thought it should be
                {
                    frame_work_queue_entry* FramePtr = FrameQueue + (videoState->frameCountDraw-1)%gap;
                    // videoState->currVideoTime = 0; //(double)FramePtr->Frame->pts*time_base; //TODO this will not work with more than one thread
                    sws_scale(videoState->sws_ctx, FramePtr->Frame->data, FramePtr->Frame->linesize, 0,
                                FramePtr->Frame->height, FramePtr->pRGBFrame->data, FramePtr->pRGBFrame->linesize);
                    __sync_add_and_fetch(&videoState->blah,1);
                    
                }
            }
        }
    }
    pthread_exit(NULL);

}

inline
AVFrame *GetFrame(VideoState *videoState)
{
    int32_t OriginalFrameCount = videoState->blah;

    if (videoState->frameCountShow < OriginalFrameCount)
    {   
        frame_work_queue_entry* FramePtr = FrameQueue + (videoState->frameCountShow)%gap;
        videoState->currVideoTime = (double)FramePtr->Frame->pts*videoState->timebase;
        if (!videoState->stop || videoState->Incremental)
        {
            __sync_add_and_fetch(&videoState->frameCountShow,1);
            videoState->Incremental = false;
        } 
        return FramePtr->pRGBFrame;
    }
    return NULL;
}

void CleanUpAll(VideoDecodingCtx *VideoDecodingCtx, frame_work_queue_entry FrameQueue[], u32 SizeOfQueue, AVFrame *pRGBFrame, struct SwsContext *swsContext)
{

    for (u32 i = 0; i < SizeOfQueue; i++)
    {
        av_frame_free(&(FrameQueue[i].Frame));
    }
    av_frame_free(&pRGBFrame);
    // UnloadTexture(*texture);
    sws_freeContext(swsContext);
    avformat_close_input(&VideoDecodingCtx->pFormatContext);
    av_packet_unref(VideoDecodingCtx->pPacket);
    av_packet_free(&VideoDecodingCtx->pPacket);
    avcodec_free_context(&VideoDecodingCtx->pVideoCodecCtx);
    printf("Cleaned All...\n");
}

Vector2 dragVec;
inline
void Controller(VideoState * videoState, Rectangle *face)
{
    static int counter;
    if (IsKeyPressed(KEY_SPACE))
    {
        // stop = (stop == 0)? 1 : 0;
        videoState->stop = !videoState->stop;
        printf("Pause: %i\n", videoState->stop);
    }

    if (IsKeyPressed(KEY_RIGHT))
    {
        if (!videoState->stop) videoState->stop = !videoState->stop;
        videoState->Decrement = false;
        videoState->Incremental = true;
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

    lastGesture = currentGesture;
    currentGesture = GetGestureDetected();

    if (currentGesture & (GESTURE_TAP | GESTURE_DOUBLETAP | GESTURE_HOLD))
    {
        face->x = GetMouseX();
        face->y = GetMouseY();
        face->width = 0;
        face->height = 0;
    }
    //TODO - FIX MOUSE OFFSET AT DRAGGING when moving fast
    if (currentGesture == GESTURE_DRAG)
    {   
        face->width = GetMouseX() - face->x ;
        face->height = GetMouseY() - face->y;
    }
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
        // pRGBFrame->linesize = linesize;
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

    Image tempImage = {0};

    Rectangle face = {0,0,0,0};
    // pthread_t threads[NUM_THREADS];
    VideoState *videoState = (VideoState*)malloc(sizeof(VideoState));
    videoState->frameCountDraw = 0;
    videoState->frameCountShow = 0;
    videoState->duration = duration;
    videoState->sws_ctx = sws_ctx;
    videoState->timebase = time_base;
    videoState->stop = true;
    videoState->image = image;
    videoState->pRGBFrameTemp = pRGBFrameTemp;
    videoState->pRGBFrame = pRGBFrame;
    videoState->TimePerFrame = 1/(float)FPS;
    videoState->tempImage = tempImage;
    videoState->face = face;
    videoState->texture = texture;
    videoState->pFormatContext = pFormatContext;
    videoState->pVideoCodecCtx = pVideoCodecCtx;
    videoState->videoStreamIndex = videoStreamIndex;
    videoState->pPacket = av_packet_alloc();
    // videoState->threads = threads;

    // struct VideoDecodingCtx DecodingCtx;
    // DecodingCtx.pFormatContext = pFormatContext;
    // DecodingCtx.pVideoCodecCtx = pVideoCodecCtx;
    // DecodingCtx.videoStreamIndex = videoStreamIndex;
    // DecodingCtx.pPacket = av_packet_alloc();
    // DecodingCtx.frameCountDraw = videoState.frameCountDraw;
    // DecodingCtx.stop = videoState.stop;

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
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "BlurryFaces");
    GuiWindowFileDialogState fileDialogState = InitGuiWindowFileDialog(GetWorkingDirectory());
    const char *path = "84.mp4";
    VideoState *videoState = InitializeVideo(path);
    
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {   
        Controller(videoState, &videoState->face);
        float currTime = GetFrameTime();
        videoState->currFrameTime += currTime; 
        // total += currTime;
        // printf("now: %f, %f\n", videoState.currFrameTime,TimePerFrame);
        if (videoState->currFrameTime >= videoState->TimePerFrame || videoState->frameCountDraw == 0)
        {   
            videoState->currFrameTime = 0;
            videoState->pRGBFrameTemp = GetFrame(videoState);
            
            if (videoState->pRGBFrameTemp)
            {
                videoState->currFrameTime = 0;
                // printf("now: %f, %f\n", videoState->currFrameTime,videoState->TimePerFrame);
                if (videoState->face.width > 0 && videoState->face.height > 0)
                {
                    if (videoState->stop)
                    {
                        av_frame_copy(videoState->pRGBFrame, videoState->pRGBFrameTemp);
                        videoState->image.data = videoState->pRGBFrame->data[0];
                    }
                    else videoState->image.data = videoState->pRGBFrameTemp->data[0];
                    float x = ((videoState->face.x - 500)/500)*1080;
                    float y = (videoState->face.y/500)*720;
                    float w = (videoState->face.width/500)*1080;
                    float h = (videoState->face.height/500)*720;
                    videoState->tempImage = ImageFromImage(videoState->image, (Rectangle){x,y,w,h});
                    ImageBlurGaussian(&videoState->tempImage,15);
                    ImageDraw(&videoState->image, videoState->tempImage, (Rectangle){0, 0, (float)videoState->tempImage.width, (float)videoState->tempImage.height},(Rectangle){x , y, w, h}, WHITE);
                    UpdateTexture(videoState->texture,videoState->image.data);
                    // frame_count++;
                }
                else
                {
                UpdateTexture(videoState->texture,videoState->pRGBFrameTemp->data[0]);
                // frame_count++;
                }
            } 
        }
        if (videoState->frameCountDraw > 0)
        {
            BeginDrawing(); 
                ClearBackground(CLITERAL(Color){ 59, 0, 161, 255 });

                DrawTexturePro(videoState->texture, (Rectangle){0, 0, (float)videoState->texture.width, (float)videoState->texture.height},
                        (Rectangle){500, 0, VIDEO_WIDTH, VIDEO_HEIGHT}, (Vector2){0, 0}, 0, WHITE);
                // DrawTexturePro(text, (Rectangle){0, 0, (float)tempImage.width, (float)tempImage.height},
                        // (Rectangle){0, 100, 100, 100}, (Vector2){0, 0}, 0, WHITE);
                // DrawTexture(text,0,0,WHITE);
                DrawRectangleRoundedLines(videoState->face, 2, 2, 2, RED);
                Slider(videoState);
                // DrawText(TextFormat("Time: %.2lf / %.2f", videoState->currVideoTime, duration), SCREEN_WIDTH-200, 0, 20, WHITE);
                DrawText(TextFormat("Real Time: %.2f", videoState->currRealTime), SCREEN_WIDTH-200, 80, 20, WHITE);
                // DrawText(TextFormat("Real FPS: %lf / %d", frame_count/total, FPS), 0, 0, 20, WHITE);
                // DrawText(TextFormat("Max Curr FPS: %lf / %d", 1/videoState->TimePerFrame, FPS), 0, 20, 20, WHITE);
                DrawText(TextFormat("TOTAL FRAMES: %lld",videoState->frameCountDraw), SCREEN_WIDTH-215, 40, 20, WHITE);
                DrawText(TextFormat("Drag: %f,%f",dragVec.x,dragVec.y), SCREEN_WIDTH-215, 60, 15, WHITE);
                DrawFPS(0,40);
                if (fileDialogState.windowActive) GuiLock();

                if (GuiButton((Rectangle){ 0, 200, 140, 30 }, GuiIconText(ICON_FILE_OPEN, "Open Image"))) fileDialogState.windowActive = true;

                GuiUnlock();
                // GUI: Dialog Window
                //--------------------------------------------------------------------------------
                GuiWindowFileDialog(&fileDialogState);
                //--------------------------------------------------------------------------------
            EndDrawing();   
            // UnloadTexture(text);
        }
    }
    // Cleanup resources
    // TODO -- STOP THREAD
    DecodingThread = false;
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    printf("Decoding Thread Closed\n");
    // CleanUpAll(&DecodingCtx,FrameQueue,ARRAY_COUNT(FrameQueue),pRGBFrame,sws_ctx);
    CloseWindow();                  // Close window and OpenGL context
    return 0;
}
