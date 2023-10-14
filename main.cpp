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
#define NUM_THREADS 8


bool stop = false; // Name pause is taken by unistd

struct VideoDecodingCtx
{
    AVFormatContext *pFormatContext;
    s32 videoStreamIndex;
    AVCodecContext *pVideoCodecCtx;
    AVPacket *pPacket;

};

struct frame_work_queue_entry //We need to free this
{
    AVFrame *Frame = av_frame_alloc();
};


void CleanUp(VideoDecodingCtx *VideoDecodingCtx, frame_work_queue_entry FrameQueue[], u32 SizeOfQueue, AVFrame *pRGBFrame, struct SwsContext *swsContext, Texture2D *texture)
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

}

inline
void Controller(void)
{
    if (IsKeyPressed(KEY_SPACE))
    {
        // stop = (stop == 0)? 1 : 0;
        stop = !stop;
        printf("Pause: %i\n", stop);
    }
}

frame_work_queue_entry FrameQueue[1000];
static u32 volatile FrameNextEntryToFill;

void* DoDecoding(void* arg)
{      
    VideoDecodingCtx *DecodingCtx = (VideoDecodingCtx *)arg;
    AVFrame* Frame;

    for (;;)
    {
        if (av_read_frame(DecodingCtx->pFormatContext, DecodingCtx->pPacket) < 0){break;}

        if (DecodingCtx->pPacket->stream_index == DecodingCtx->videoStreamIndex) 
        {   
                assert(FrameNextEntryToFill < ARRAY_COUNT(FrameQueue));
                frame_work_queue_entry* pFrame = FrameQueue + FrameNextEntryToFill;
                FrameNextEntryToFill++;
                Frame = pFrame->Frame;

                avcodec_send_packet(DecodingCtx->pVideoCodecCtx, DecodingCtx->pPacket);
                av_packet_unref(DecodingCtx->pPacket);
                avcodec_receive_frame(DecodingCtx->pVideoCodecCtx, Frame);

        }
    }
}

// Function to clamp a value between a minimum and maximum
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

    struct VideoDecodingCtx DecodingCtx;
    DecodingCtx.pFormatContext = pFormatContext;
    DecodingCtx.pVideoCodecCtx = pVideoCodecCtx;
    DecodingCtx.videoStreamIndex = videoStreamIndex;
    DecodingCtx.pPacket = av_packet_alloc();

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

    u32 FPS = VideoStream->avg_frame_rate.num / VideoStream->avg_frame_rate.den;
    double curr_time = 0;
    double time_base = av_q2d(VideoStream->time_base);
    double duration = (double)VideoStream->duration*time_base;

    // AVPacket *pPacket = av_packet_alloc(); //pPacket holds data buffer reference and more
    // AVFrame *pFrame = av_frame_alloc();
    double TotalTime = 0;
    // SetTargetFPS(FPS);  // So this uses BeginDrawing block... if that is outside the videoStreamIndex 
                        // it will also delay the sound frames.. making the video slow down
    u64 TotalFrames = 0;        
    u32 FrameToDraw = 0;
    AVFrame *pFrame = av_frame_alloc();
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {
        Vector2 mouse = GetMousePosition();  
        Controller();
        if (FrameToDraw < FrameNextEntryToFill)
        {
            frame_work_queue_entry* FramePtr = FrameQueue + FrameToDraw;
            FrameToDraw++;
            pFrame = FramePtr->Frame;

        }
        else {printf("Render Too fast !!");}

        if (pFrame)
        {
            sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0,
                        pFrame->height, pRGBFrame->data, pRGBFrame->linesize);
            UpdateTexture(texture, pRGBFrame->data[0]);
            BeginDrawing(); 
                ClearBackground(RAYWHITE);
                DrawTexturePro(texture, (Rectangle){0, 0, (float)texture.width, (float)texture.height},
                        (Rectangle){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT}, (Vector2){0, 0}, 0, WHITE);
                // DrawText(TextFormat("Time: %.2f / %.2f", curr_time, duration), SCREEN_WIDTH-200, 0, 20, WHITE);
                DrawText(TextFormat("FPS: %d / %d", GetFPS(), FPS), 0, 0, 20, WHITE);
                DrawText(TextFormat("Time Ray: %lf",TotalTime), SCREEN_WIDTH-200, 20, 20, WHITE);
                DrawText(TextFormat("TOTAL FRAMES: %lld",TotalFrames), SCREEN_WIDTH-215, 40, 20, WHITE);
                DrawFPS(0,30);
                TotalTime += GetFrameTime();
            EndDrawing();    
        }
        // if (pPacket->stream_index == videoStreamIndex) // Necessary for SetTargetFPS
        // {
        //    //rendering stuff before
        // }
        // else{
        //     Controller(); //There is some bug with raylib... if the loop goes
        //     //without draw it will carry the pressed key to the next loop
        // }
        // av_packet_unref(pPacket);
    }
    // Cleanup resources
    // TODO -- STOP THREAD
    CleanUp(&DecodingCtx,FrameQueue,ARRAY_COUNT(FrameQueue),pRGBFrame,sws_ctx,&texture);
    CloseWindow();                  // Close window and OpenGL context
    return 0;
}
