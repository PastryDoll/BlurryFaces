#include "raylib.h"
#include "rlgl.h"
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

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
}

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
#define NUM_THREADS 3


struct VideoDecodingCtx
{
    AVFormatContext *pFormatContext;
    s32 videoStreamIndex;
    AVCodecContext *pVideoCodecCtx;
    AVPacket *pPacket;
    struct SwsContext *sws_ctx;
    u64 volatile *frameCountDraw;

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
    u64 volatile frameCountDraw;
    u64 volatile frameCountShow;
    u64 volatile blah;
    AVFrame *pRGBFrame;
    u32 height;
    u32 width;
    float currRealTime;
    float currVideoTime;
    float currFrameTime;
    float duration;
    bool stop;
    bool Incremental;
    bool Decrement;
};

Rectangle Faces[10];
// A very poor and maybe bad idea of circular buffering for 
// saving memory and maybe some hot memory use
const int gap = 10;
frame_work_queue_entry FrameQueue[gap];
texture_work_queue_entry TextureQueue[gap];
static u32 volatile FrameNextEntryToFill = 0;
// static u32 volatile TextureNextEntryToFill;
static bool volatile DecodingThread = true;
int currentGesture = GESTURE_NONE;
int lastGesture = GESTURE_NONE;
// pthread_mutex_t writeMutex = PTHREAD_MUTEX_INITIALIZER; //Overkill ? 

void* DoDecoding(void* arg)
{      
    VideoDecodingCtx *DecodingCtx = (VideoDecodingCtx *)arg;
    while (DecodingThread)
    {
        assert(FrameNextEntryToFill >= *DecodingCtx->frameCountDraw);
        if (FrameNextEntryToFill - *DecodingCtx->frameCountDraw < gap)
        {
            if (av_read_frame(DecodingCtx->pFormatContext, DecodingCtx->pPacket) < 0){break;}

            if (DecodingCtx->pPacket->stream_index == DecodingCtx->videoStreamIndex) 
            {   
                    // assert((unsigned char)FrameNextEntryToFill < ARRAY_COUNT(FrameQueue));
                    frame_work_queue_entry* pFrame = FrameQueue + FrameNextEntryToFill%gap;
                    
                    avcodec_send_packet(DecodingCtx->pVideoCodecCtx, DecodingCtx->pPacket);
                    avcodec_receive_frame(DecodingCtx->pVideoCodecCtx, pFrame->Frame);
                    av_packet_unref(DecodingCtx->pPacket);
                    __sync_add_and_fetch(&FrameNextEntryToFill, 1);

            }
        }
    }
    pthread_exit(NULL);
}

void* UpdateFrame(void* arg)
{
    VideoState *videoState = (VideoState *)arg;
    pthread_t thread_id = pthread_self(); // Get the thread ID

    while (DecodingThread)
    {
        int32_t OriginalFrameCount = videoState->frameCountDraw;

        assert(OriginalFrameCount >= videoState->frameCountShow);
        if (OriginalFrameCount - videoState->frameCountShow < gap)
        {
            // printf("111Todraw: %llu,ToShow: %d, FrameNextEntry: %u, ID: %lu\n", videoState->frameCountDraw,videoState->frameCountShow,FrameNextEntryToFill, thread_id);
            if ((OriginalFrameCount < FrameNextEntryToFill) && FrameNextEntryToFill > 0) //If frameCount too close to NextEntry it fails.. idk why for now
            {   

                int32_t InitialValue = __sync_val_compare_and_swap(&videoState->frameCountDraw, OriginalFrameCount, OriginalFrameCount+1);

                if (InitialValue == OriginalFrameCount) //If the original is what we thought it should be
                {
                    frame_work_queue_entry* FramePtr = FrameQueue + (videoState->frameCountDraw-1)%gap;
                    // texture_work_queue_entry* TextPtr = TextureQueue + (videoState->frameCount-1)%gap;
                    videoState->currFrame  = FramePtr->Frame;
                    videoState->Incremental = false;
                    videoState->currVideoTime = 0; //(double)FramePtr->Frame->pts*time_base; //TODO this will not work with more than one thread
                    // printf("222Frame Count: %llu,OriginalFrameCount: %d, FrameNextEntry: %u, ID: %lu\n", videoState->frameCountDraw,OriginalFrameCount,FrameNextEntryToFill, thread_id);
                    sws_scale(videoState->sws_ctx, FramePtr->Frame->data, FramePtr->Frame->linesize, 0,
                                FramePtr->Frame->height, FramePtr->pRGBFrame->data, FramePtr->pRGBFrame->linesize);
                    __sync_add_and_fetch(&videoState->blah,1);
                    
                }
            }
        }
    }
    pthread_exit(NULL);

}

AVFrame *GetFrame(VideoState *videoState)
{
    int32_t OriginalFrameCount = videoState->blah;

    // printf("OriginalFrameCount: %d,frameCountShow: %llu\n", OriginalFrameCount, videoState->frameCountShow);
    if (videoState->frameCountShow < OriginalFrameCount)
    {   
        // printf("oi\n");
        frame_work_queue_entry* FramePtr = FrameQueue + (videoState->frameCountShow)%gap;
        return FramePtr->pRGBFrame;
    }
    // printf("xau\n");
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

// void PlayButton(VideoState *VideoState)
// {
//     DrawTriangle()

// }

int main(void)
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "BlurryFaces");

    // Open video file
    AVFormatContext *pFormatContext = avformat_alloc_context(); // = (NULL) ?
    avformat_open_input(&pFormatContext, "1.mov", NULL, NULL); //This reads the header (maybe not codec)
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
                             VideoWidth, VideoHeight, AV_PIX_FMT_RGB24,
                             SWS_FAST_BILINEAR, 0, 0, 0);

    // Allocate buffer for RGB data
    AVFrame *pRGBFrame = av_frame_alloc();
    pRGBFrame->format = AV_PIX_FMT_RGB24; //RGB24 is 8 bits per channel (8*3)
    pRGBFrame->width  = VideoWidth;
    pRGBFrame->height = VideoHeight;
    av_frame_get_buffer(pRGBFrame, 0);

    for (u32 i = 0; i < gap; i++)
    {
        AVFrame *pRGBFrame = av_frame_alloc();
        pRGBFrame->format = AV_PIX_FMT_RGB24; //RGB24 is 8 bits per channel (8*3)
        pRGBFrame->width  = VideoWidth;
        pRGBFrame->height = VideoHeight;
        av_frame_get_buffer(pRGBFrame, 0);
        TextureQueue[i].pRGBFrame = pRGBFrame;
    }

    for (u32 i = 0; i < gap; i++)
    {
        AVFrame *pRGBFrame = av_frame_alloc();
        pRGBFrame->format = AV_PIX_FMT_RGB24; //RGB24 is 8 bits per channel (8*3)
        pRGBFrame->width  = VideoWidth;
        pRGBFrame->height = VideoHeight;
        av_frame_get_buffer(pRGBFrame, 0);
        FrameQueue[i].pRGBFrame = pRGBFrame;
    }

    Texture texture = {0};
    texture.height  = VideoHeight;
    texture.width   = VideoWidth;
    texture.format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
    texture.mipmaps = 1;
    texture.id = rlLoadTexture(NULL, texture.width, texture.height, texture.format, texture.mipmaps);

    Image image = {0};
    image.height = VideoHeight;
    image.width = VideoWidth;
    image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
    image.mipmaps = 1;

    VideoState videoState = {0};
    videoState.frameCountDraw = 0;
    videoState.frameCountShow = 0;
    videoState.duration = duration;
    videoState.pRGBFrame = pRGBFrame;
    videoState.height = VideoHeight;
    videoState.sws_ctx = sws_ctx;
    videoState.width = VideoWidth;

    struct VideoDecodingCtx DecodingCtx;
    DecodingCtx.pFormatContext = pFormatContext;
    DecodingCtx.pVideoCodecCtx = pVideoCodecCtx;
    DecodingCtx.videoStreamIndex = videoStreamIndex;
    DecodingCtx.pPacket = av_packet_alloc();
    DecodingCtx.frameCountDraw = &videoState.frameCountDraw;
    pthread_t threads[NUM_THREADS];

    if (pthread_create(&threads[0], NULL, DoDecoding, &DecodingCtx) != 0)
    {
        perror("pthread_create");
        return 1;
    }
    if (pthread_create(&threads[1], NULL, UpdateFrame, &videoState) != 0)
    {
        perror("pthread_create");
        return 1;
    }
    // if (pthread_create(&threads[2], NULL, UpdateFrame, &videoState) != 0)
    // {
    //     perror("pthread_create");
    //     return 1;
    // }

    // We want the application to run freely but the video to run at its FPS... Thats why we use videoState.currFrameTime
    // Maybe ideally we need second thread to render the video.. but I dont belive this is possible with RayLib
    // SetTargetFPS(FPS);   // So this uses BeginDrawing block... if that is outside the videoStreamIndex 
                            // it will also delay the sound frames.. making the video slow down

    Rectangle face = {0,0,0,0};
    Vector2 dragVec;
    float TimePerFrame = 1/(float)FPS/4;
    u32 frame_count = 0;
    float total =0;
    // printf("Drag: %f,%f", dragVec.x,dragVec.y);
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {   
        Controller(&videoState, &face);
        float currTime = GetFrameTime();
        // videoState.currRealTime += currTime;
        videoState.currFrameTime += currTime; 
        total += currTime;
        printf("Current Frame Time: %f\n", videoState.currFrameTime);
        if (videoState.currFrameTime >= TimePerFrame || videoState.frameCountDraw == 0)
        {   
            if (!videoState.stop || videoState.Incremental)
            {   
                videoState.currFrameTime = 0;
                pRGBFrame = GetFrame(&videoState);
                // printf("GetFrame\n");
                if (pRGBFrame)
                {
                    frame_count++;
                    UpdateTexture(texture, pRGBFrame->data[0]);
                    __sync_add_and_fetch(&videoState.frameCountShow,1);
                } 
            }
        }
        else
        {
            printf("now\n");
        }
        //         // videoState.currFrameTime = 0;
        //         printf("Frame Count: %llu FrameNextEntry: %u\n", videoState.frameCount,FrameNextEntryToFill);
        //         fflush(stdout);
        //         // TODO understand the error if we take -1 out

                
        //         if ((videoState.frameCount < FrameNextEntryToFill ) && (FrameNextEntryToFill > 0)) //If frameCount too close to NextEntry it fails.. idk why for now
        //         {   
        //             frame_work_queue_entry* FramePtr = FrameQueue + videoState.frameCount%gap;
        //             // videoState.currFrame  = FramePtr->pRGBFrame;
        //             __sync_add_and_fetch(&videoState.frameCount, 1);
        //             // videoState.frameCount++;
        //             videoState.Incremental = false;
        //             videoState.currVideoTime = (double)FramePtr->Frame->pts*time_base;

                    // PLEASE UNDERSTAND THIS !!!!!!!!!
                    // It doest make sense.. if we put this outside the if (stop) when we pause the FPS goes down
                    // TODO maybe make this in another thread
                    // sws_scale(sws_ctx, videoState.currFrame->data, videoState.currFrame->linesize, 0,
                    //             videoState.currFrame->height, pRGBFrame->data, pRGBFrame->linesize);
                    
                    // Create a temporary image and copy the specific region into it
                    // image.data = pRGBFrame->data[0];

                    // Invert the color of the temporary image

                    // Copy the modified region from the temporary image back to the original image
                    // if (face.width > 10 && face.height > 10)
                    // {
                    //     printf("x,y: %f,%f\n", face.x-500,face.y);
                    //     Image tempImage = ImageFromImage(image, (Rectangle){face.x - 500, face.y, face.width, face.height});
                    //     ImageColorInvert(&tempImage);
                    //     ImageDraw(&image, tempImage, (Rectangle){0, 0, face.width, face.height},(Rectangle){face.x , face.y, (float)texture.width, (float)texture.height}, WHITE);
                    // }
                    // printf("Video H,W: %d,%d\n", VideoWidth, image.width);
                    // ImageColorInvert(&image);  
                    // UpdateTexture(texture, image.data);
        //             UpdateTexture(texture, FramePtr->pRGBFrame->data[0]);
        //         }
        //         else
        //         {
        //         printf("Too Fast Render\n");
        //         }

        //     }
        // }
       
        // This whole thing breaks when we let the FPS go high
        if (videoState.frameCountDraw > 0)
        {
            //TODO Check if pFrame is valid
            // printf("img ptr %p\n", videoState.currFrame->data);
            BeginDrawing(); 
                ClearBackground(CLITERAL(Color){ 59, 0, 161, 255 });

                DrawTexturePro(texture, (Rectangle){0, 0, (float)texture.width, (float)texture.height},
                        (Rectangle){500, 0, VIDEO_WIDTH, VIDEO_HEIGHT}, (Vector2){0, 0}, 0, WHITE);
                DrawRectangleRoundedLines(face, 2, 2, 2, RED);
                Slider(&videoState);
                DrawText(TextFormat("Time: %.2lf / %.2f", videoState.currVideoTime, duration), SCREEN_WIDTH-200, 0, 20, WHITE);
                DrawText(TextFormat("Real Time: %.2f", videoState.currRealTime), SCREEN_WIDTH-200, 80, 20, WHITE);
                DrawText(TextFormat("Real FPS: %lf / %d", frame_count/total, FPS), 0, 0, 20, WHITE);
                DrawText(TextFormat("Max Curr FPS: %lf / %d", 1/TimePerFrame, FPS), 0, 20, 20, WHITE);
                DrawText(TextFormat("TOTAL FRAMES: %lld",videoState.frameCountDraw), SCREEN_WIDTH-215, 40, 20, WHITE);
                DrawText(TextFormat("Drag: %f,%f",dragVec.x,dragVec.y), SCREEN_WIDTH-215, 60, 15, WHITE);
                DrawFPS(0,40);
            EndDrawing();    
        }
    }
    // Cleanup resources
    // TODO -- STOP THREAD
    DecodingThread = false;
    pthread_join(threads[0], NULL);
    printf("Decoding Thread Closed\n");
    CleanUpAll(&DecodingCtx,FrameQueue,ARRAY_COUNT(FrameQueue),pRGBFrame,sws_ctx);
    CloseWindow();                  // Close window and OpenGL context
    return 0;
}
