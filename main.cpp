#include "raylib.h"
#include "rlgl.h"
#include "math.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>         // Required for: malloc() and free()
#include <time.h>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
}

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

int pause = 0;

void Controller(void)
{
    if (IsKeyPressed(KEY_SPACE)) pause = (pause == 0)? 1 : 0;
}

// Function to clamp a value between a minimum and maximum
int main(void)
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "BlurryFaces");

    // Open video file
    AVFormatContext *pFormatContext = avformat_alloc_context(); // = NULL ?
    avformat_open_input(&pFormatContext, "84.mp4", NULL, NULL); //This reads the header (maybe not codec)
    // printf("Format %s, duration %lld us", pFormatContext->iformat->long_name, pFormatContext->duration);
    avformat_find_stream_info(pFormatContext, NULL); //get streams

    //TODO - Support Audio ???
    // Find video stream
    int videoStreamIndex = -1;
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

    u32 VideoWidth = pVideoCodecCtx->width;
    u32 VideoHeight = pVideoCodecCtx->height;

    struct SwsContext *img_convert_ctx = sws_alloc_context();
    struct SwsContext *sws_ctx         = NULL;
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

#if 1

    u32 FPS = VideoStream->avg_frame_rate.num / VideoStream->avg_frame_rate.den;
    double curr_time = 0;
    double time_base = av_q2d(VideoStream->time_base);
    double duration = (double)VideoStream->duration*time_base;

    AVPacket *pPacket = av_packet_alloc(); //pPacket holds data buffer reference and more
    AVFrame *pFrame = av_frame_alloc();
    double TotalTime = 0;
    SetTargetFPS(FPS);  // So this uses BeginDrawing block... if that is outside the videoStreamIndex 
                        // it will also delay the sound frames.. making the video slow down
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {
        Vector2 mouse = GetMousePosition();  
        Controller();

        // TODO - Check if there is a bug here with the pause.. 
        if (!pause)
        {
            if (av_read_frame(pFormatContext, pPacket) < 0){break;}

            if (pPacket->stream_index == videoStreamIndex) 
            {
                // // Decode video frame
                avcodec_send_packet(pVideoCodecCtx, pPacket);
                avcodec_receive_frame(pVideoCodecCtx, pFrame);

                curr_time = ((double)pFrame->pts)*time_base;

                // Convert the YUV frame to RGB for Texture
                sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0,
                            pFrame->height, pRGBFrame->data, pRGBFrame->linesize);
                UpdateTexture(texture, pRGBFrame->data[0]);
            }
        }

        if (pPacket->stream_index == videoStreamIndex) // Necessary for SetTargetFPS
        {
            BeginDrawing(); 
                ClearBackground(RAYWHITE);
                DrawTexturePro(texture, (Rectangle){0, 0, (float)texture.width, (float)texture.height},
                        (Rectangle){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT - 100}, (Vector2){0, 0}, 0, WHITE);
                DrawText(TextFormat("Time: %.2f / %.2f", curr_time, duration), SCREEN_WIDTH-200, 0, 20, WHITE);
                DrawText(TextFormat("FPS: %d / %d", GetFPS(), FPS), 0, 0, 20, WHITE);
                DrawText(TextFormat("Time Ray: %lf",TotalTime), SCREEN_WIDTH-200, 20, 20, WHITE);
                DrawFPS(0,30);
                TotalTime += GetFrameTime();
            EndDrawing();    
        }


        av_packet_unref(pPacket);
    }

    // Cleanup resources
    avformat_close_input(&pFormatContext);
    av_packet_unref(pPacket);
    av_packet_free(&pPacket);
    av_frame_free(&pRGBFrame);
    av_frame_free(&pFrame);
    avcodec_free_context(&pVideoCodecCtx);
    UnloadTexture(texture);
    sws_freeContext(sws_ctx);
#endif 
    CloseWindow();                  // Close window and OpenGL context
    return 0;
}
