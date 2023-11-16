#include "ffmpeg_decoder.h"

#define TARGET_WIDTH 640
#define TARGET_HEIGHT 480

static bool volatile Decoding = true;

frame_work_queue_memory *AllocFrameQueue()
{
    frame_work_queue_memory *FrameQueue = (frame_work_queue_memory *)malloc(RingSize * sizeof(frame_work_queue_memory));

    if (FrameQueue == NULL)
    {
        // Handle memory allocation failure
        return NULL;
    }


    for (u32 i = 0; i < RingSize; i++)
    {
        AVFrame *pRGBFrame = av_frame_alloc();
        pRGBFrame->format = AV_PIX_FMT_RGB24; //RGB24 is 8 bits per channel (8*3)
        pRGBFrame->width  = TARGET_WIDTH;
        pRGBFrame->height = TARGET_HEIGHT;
        pRGBFrame->linesize[0] = TARGET_WIDTH * 3;
        av_frame_get_buffer(pRGBFrame, 0);
        FrameQueue[i].pRGBFrame = pRGBFrame;
    }
    return FrameQueue;

}

void CleanUpVideo(video_decoder *VideoDecoder)
{
    printf("0\n");
    Decoding = false;
    printf("1\n");
    printf("3\n");
    dispatch_semaphore_signal(VideoDecoder->SemaphoreDecoder);
    printf("32\n");
    dispatch_semaphore_signal(VideoDecoder->SemaphoreConvertion);
    printf("33\n");
    pthread_join(VideoDecoder->threads[0], NULL);
    printf("4\n");
    pthread_join(VideoDecoder->threads[1], NULL);
    printf("12\n");
    av_packet_unref(VideoDecoder->pPacket);
    printf("13\n");
    av_packet_free(&VideoDecoder->pPacket);
    printf("14\n");
    avcodec_free_context(&VideoDecoder->pVideoCodecCtx);
    avformat_free_context(VideoDecoder->pFormatContext);
    printf("15\n");
    free(VideoDecoder);
    printf("16\n");
    Decoding = true;
    printf("17\n");
}