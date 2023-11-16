#include "blurryface_types.h"
#include <dispatch/dispatch.h>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
}

const int RingSize = 16;

struct thread_manager 
{
    dispatch_semaphore_t SemaphoreDecoder;
    dispatch_semaphore_t SemaphoreConvertion;
    pthread_t threads[2];

};

struct frame_work_queue_memory //We need to free this
{
    AVFrame *Frame = av_frame_alloc();
    AVFrame *pRGBFrame = av_frame_alloc();

};

struct video_decoder 
{
    frame_work_queue_memory *FrameQueue;

    AVFormatContext *pFormatContext;
    AVCodecContext *pVideoCodecCtx;
    AVPacket *pPacket;
    s32 VideoStreamIndex;

    u64 volatile FrameToFill;
    u64 volatile FrameToConvert;
    u64 volatile FrameToGrab;
    u64 volatile FrameToRender;

    double TimeBase;
    float Duration;
    u8 Fps;

    dispatch_semaphore_t SemaphoreDecoder;
    dispatch_semaphore_t SemaphoreConvertion;
    pthread_t threads[2];

};