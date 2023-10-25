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
#define NUM_THREADS 2


struct VideoDecodingCtx
{
    AVFormatContext *pFormatContext;
    s32 videoStreamIndex;
    AVCodecContext *pVideoCodecCtx;
    AVPacket *pPacket;
    u64 *frameCount;

};

struct frame_work_queue_entry //We need to free this
{
    AVFrame *Frame = av_frame_alloc();
};

struct VideoState 
{
    AVFrame *currFrame;
    u64 frameCount;
    float currRealTime;
    float currVideoTime;
    float currFrameTime;
    float duration;
    bool stop;
    bool Incremental;
    bool Decrement;
};

Rectangle Faces[10];
const int gap = 10;
frame_work_queue_entry FrameQueue[gap];
static u32 volatile FrameNextEntryToFill;
static bool volatile DecodingThread = true;
int currentGesture = GESTURE_NONE;
int lastGesture = GESTURE_NONE;
// pthread_mutex_t writeMutex = PTHREAD_MUTEX_INITIALIZER; //Overkill ? 

void CleanUpAll(VideoDecodingCtx *VideoDecodingCtx, frame_work_queue_entry FrameQueue[], u32 SizeOfQueue, AVFrame *pRGBFrame, struct SwsContext *swsContext, Texture2D *texture)
{

    for (u32 i = 0; i < SizeOfQueue; i++)
    {
        av_frame_free(&(FrameQueue[i].Frame));
    }
    av_frame_free(&pRGBFrame);
    UnloadTexture(*texture);
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

void* DoDecoding(void* arg)
{      
    VideoDecodingCtx *DecodingCtx = (VideoDecodingCtx *)arg;
    // int counter = 0; 
    while (DecodingThread)
    {
        if (FrameNextEntryToFill - *DecodingCtx->frameCount < gap)
        {
            if (av_read_frame(DecodingCtx->pFormatContext, DecodingCtx->pPacket) < 0){break;}
            // counter++;

            if (DecodingCtx->pPacket->stream_index == DecodingCtx->videoStreamIndex) 
            {   
                    // assert((unsigned char)FrameNextEntryToFill < ARRAY_COUNT(FrameQueue));
                    frame_work_queue_entry* pFrame = FrameQueue + FrameNextEntryToFill%gap;
                    
                    __sync_add_and_fetch(&FrameNextEntryToFill, 1);
                    // if (counter == 100)
                    // {

                    // }
                    avcodec_send_packet(DecodingCtx->pVideoCodecCtx, DecodingCtx->pPacket);
                    avcodec_receive_frame(DecodingCtx->pVideoCodecCtx, pFrame->Frame);
                    av_packet_unref(DecodingCtx->pPacket);

            }
        }
    }
    pthread_exit(NULL);
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
    avformat_open_input(&pFormatContext, "84.mp4", NULL, NULL); //This reads the header (maybe not codec)
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

    VideoState videoState = {0};
    videoState.duration = duration;
    
    struct VideoDecodingCtx DecodingCtx;
    DecodingCtx.pFormatContext = pFormatContext;
    DecodingCtx.pVideoCodecCtx = pVideoCodecCtx;
    DecodingCtx.videoStreamIndex = videoStreamIndex;
    DecodingCtx.pPacket = av_packet_alloc();
    DecodingCtx.frameCount = &videoState.frameCount;

    pthread_t threads[NUM_THREADS];
    if (pthread_create(&threads[0], NULL, DoDecoding, &DecodingCtx) != 0)
    {
        perror("pthread_create");
        return 1;
    }
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



    // We want the application to run freely but the video to run at its FPS... Thats why we use videoState.currFrameTime
    // Maybe ideally we need second thread to render the video.. but I dont belive this is possible with RayLib
    // SetTargetFPS(FPS);   // So this uses BeginDrawing block... if that is outside the videoStreamIndex 
                            // it will also delay the sound frames.. making the video slow down

    Rectangle face = {0,0,0,0};
    Vector2 dragVec;
    float TimePerFrame = 1/(float)FPS/4;
    // printf("Drag: %f,%f", dragVec.x,dragVec.y);
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {   
        Controller(&videoState, &face);
        float currTime = GetFrameTime();
        videoState.currRealTime += currTime;
        videoState.currFrameTime += currTime; 
        // printf("Current Frame Time: %f\n", videoState.currFrameTime);
        if (videoState.currFrameTime >= TimePerFrame || videoState.frameCount == 0)
        {   
            if (!videoState.stop || videoState.Incremental)
            {   
                videoState.currFrameTime = 0;
                printf("Frame Count: %llu FrameNextEntry: %u\n", videoState.frameCount,FrameNextEntryToFill);
                fflush(stdout);
                // TODO understand the error if we take -1 out
                if (videoState.frameCount < FrameNextEntryToFill - 1) //If frameCount too close to NextEntry it fails.. idk why for now
                {   
                    frame_work_queue_entry* FramePtr = FrameQueue + videoState.frameCount%gap;
                    videoState.currFrame  = FramePtr->Frame;
                    __sync_add_and_fetch(&videoState.frameCount, 1);
                    // videoState.frameCount++;
                    videoState.Incremental = false;
                    videoState.currVideoTime = (double)FramePtr->Frame->pts*time_base;

                    // PLEASE UNDERSTAND THIS !!!!!!!!!
                    // It doest make sense.. if we put this outside the if (stop) when we pause the FPS goes down
                    // TODO maybe make this in another thread
                    sws_scale(sws_ctx, videoState.currFrame->data, videoState.currFrame->linesize, 0,
                                videoState.currFrame->height, pRGBFrame->data, pRGBFrame->linesize);
                    
                    // Create a temporary image and copy the specific region into it
                    image.data = pRGBFrame->data[0];

                    // Invert the color of the temporary image

                    // Copy the modified region from the temporary image back to the original image
                    if (face.width > 10 && face.height > 10)
                    {
                        printf("x,y: %f,%f\n", face.x-500,face.y);
                        Image tempImage = ImageFromImage(image, (Rectangle){face.x - 500, face.y, face.width, face.height});
                        ImageColorInvert(&tempImage);
                        ImageDraw(&image, tempImage, (Rectangle){0, 0, face.width, face.height},(Rectangle){face.x , face.y, (float)texture.width, (float)texture.height}, WHITE);
                    }
                    // printf("Video H,W: %d,%d\n", VideoWidth, image.width);
                    // ImageColorInvert(&image);  
                    UpdateTexture(texture, image.data);
                }
                else
                {
                printf("Too Fast Render\n");
                }

            }
        }
       
        // This whole thing breaks when we let the FPS go high
        if (videoState.frameCount > 0)
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
                DrawText(TextFormat("Real FPS: %lf / %d", videoState.frameCount/videoState.currVideoTime, FPS), 0, 0, 20, WHITE);
                DrawText(TextFormat("Max Curr FPS: %lf / %d", 1/TimePerFrame, FPS), 0, 20, 20, WHITE);
                DrawText(TextFormat("TOTAL FRAMES: %lld",videoState.frameCount), SCREEN_WIDTH-215, 40, 20, WHITE);
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
    CleanUpAll(&DecodingCtx,FrameQueue,ARRAY_COUNT(FrameQueue),pRGBFrame,sws_ctx,&texture);
    CloseWindow();                  // Close window and OpenGL context
    return 0;
}
