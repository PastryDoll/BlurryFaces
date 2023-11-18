#include "ffmpeg_decoder_memory.cpp"

#include <assert.h>


#define VIDEO_WIDTH 500
#define VIDEO_HEIGHT 500

// A very poor and maybe bad idea of circular buffering for 
// saving memory and mayObe some hot memory use


//TODO WE NEED TO UNDERSTAND HOW TO RUN CODEC IN MULTITHREAD (THIS IS NATIVE TO FFMPEG)
// MAYBE WITH THAT WE CAN REMOVE THIS STUPID THREADING I MADE
void* DoDecoding(void* arg)
{      
    video_decoder *VideoDecoder = (video_decoder *)arg;
    while (Decoding)
    {
        assert(VideoDecoder->FrameToFill >= VideoDecoder->FrameToConvert);
        if ((VideoDecoder->FrameToFill - VideoDecoder->FrameToConvert < RingSize))
        {
            if (av_read_frame(VideoDecoder->pFormatContext, VideoDecoder->pPacket) < 0){break;}

            if (VideoDecoder->pPacket->stream_index == VideoDecoder->VideoStreamIndex) 
            {   
                    frame_work_queue_memory* FramePtr = VideoDecoder->FrameQueue + VideoDecoder->FrameToFill%RingSize;
                    
                    avcodec_send_packet(VideoDecoder->pVideoCodecCtx, VideoDecoder->pPacket);
                    avcodec_receive_frame(VideoDecoder->pVideoCodecCtx, FramePtr->Frame);
                    av_packet_unref(VideoDecoder->pPacket);
                    // printf("NextEntryToFill: %u, VideoDecoder->frameCountDraw: %llu\n" , FrameToFill%RingSize,VideoDecoder->frameCountDraw%RingSize);
                    __sync_add_and_fetch(&VideoDecoder->FrameToFill, 1);
            }
        }
        else
        {
            if (VideoDecoder->FrameToConvert > 1)
            {
#ifdef DEBUG
                static int a = 0;
                printf("waste of cpu pause 1 %i\n", a);
                a++;
#endif
                dispatch_semaphore_wait(VideoDecoder->SemaphoreDecoder, DISPATCH_TIME_FOREVER);
            }
        }
    }
    pthread_exit(NULL);
}

void* DoConvertion(void* arg)
{
    video_decoder *VideoDecoder = (video_decoder *)arg;


    struct SwsContext *sws_ctx = sws_alloc_context();
    u32 VideoWidth = VideoDecoder->pVideoCodecCtx->width;
    u32 VideoHeight = VideoDecoder->pVideoCodecCtx->height;
    AVPixelFormat PixFmt = VideoDecoder->pVideoCodecCtx->pix_fmt;
    sws_ctx = sws_getContext(VideoWidth, VideoHeight, PixFmt,
                             TARGET_WIDTH, TARGET_HEIGHT, AV_PIX_FMT_RGB24,
                             SWS_FAST_BILINEAR, 0, 0, 0);


    while (Decoding)
    {
        int32_t OriginalFrameToConvert = VideoDecoder->FrameToConvert;

        assert(VideoDecoder->FrameToConvert >= VideoDecoder->FrameToRender);
        // Okay so this -1 is subtle and we might fix this soon..
        // What happens is that when we update the texture we can be using previous frame...
        // i.e lets say we grab RGB framecounttoshow 10... we make a texture and uptade framecountoshow 11
        // now framecounttoshow 10 can be changed and we will keep updating the texture with it untill we get right FPS
        if (OriginalFrameToConvert - VideoDecoder->FrameToRender < RingSize-1)
        {

            if ((OriginalFrameToConvert < VideoDecoder->FrameToFill) && VideoDecoder->FrameToFill > 0) //If frameCount too close to NextEntry it fails.. idk why for now
            {   

                int32_t InitialValue = __sync_val_compare_and_swap(&VideoDecoder->FrameToConvert, OriginalFrameToConvert, OriginalFrameToConvert+1);
                dispatch_semaphore_signal(VideoDecoder->SemaphoreDecoder);

                if (InitialValue == OriginalFrameToConvert) //If the original is what we thought it should be
                {
                    frame_work_queue_memory* FramePtr = VideoDecoder->FrameQueue + (VideoDecoder->FrameToConvert-1)%RingSize;
                    sws_scale(sws_ctx, FramePtr->Frame->data, FramePtr->Frame->linesize, 0,
                                FramePtr->Frame->height, FramePtr->pRGBFrame->data, FramePtr->pRGBFrame->linesize);
                    FramePtr->pRGBFrame->pts = FramePtr->Frame->pts;
                    __sync_add_and_fetch(&VideoDecoder->FrameToGrab,1);

#ifdef DEBUG
                    printf("FrameToFill: %llu, FrameToRender: %llu, -FrameToGrab: %llu, FrameToConvert %llu\n", VideoDecoder->FrameToFill, VideoDecoder->FrameToRender,VideoDecoder->FrameToGrab,(VideoDecoder->FrameToConvert-1));
                    printf("FrameToFill: %llu, FrameToRender: %llu, -FrameToGrab: %llu, FrameToConvert %llu\n",VideoDecoder->FrameToFill%RingSize, VideoDecoder->FrameToRender%RingSize,VideoDecoder->FrameToGrab%RingSize,(VideoDecoder->FrameToConvert-1)%RingSize);
#endif

                }
            }
        }
        else
        {

#ifdef DEBUG
            static int a = 0;
            printf("waste of cpu pause 2 %i\n",a);
            a++;
#endif
            dispatch_semaphore_wait(VideoDecoder->SemaphoreConvertion, DISPATCH_TIME_FOREVER);

        }
    }
    sws_freeContext(sws_ctx);
    pthread_exit(NULL);
}

inline
u8 *GetFrame(video_decoder *VideoDecoder, double *CurrVideoTime)
{
    assert(VideoDecoder->FrameToRender < VideoDecoder->FrameToGrab);
    frame_work_queue_memory* FramePtr = VideoDecoder->FrameQueue + (VideoDecoder->FrameToRender)%RingSize;
    *CurrVideoTime = (double)FramePtr->pRGBFrame->pts*VideoDecoder->TimeBase;
    __sync_add_and_fetch(&VideoDecoder->FrameToRender,1);
    dispatch_semaphore_signal(VideoDecoder->SemaphoreConvertion);
    return FramePtr->pRGBFrame->data[0];
}

video_decoder *InitializeVideo(const char *path, frame_work_queue_memory *FrameQueueMemory)
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

    video_decoder *VideoDecoder = (video_decoder*)malloc(sizeof(video_decoder));
    VideoDecoder->FrameQueue = FrameQueueMemory;
    VideoDecoder->FrameToConvert = 0;
    VideoDecoder->FrameToRender = 0;
    VideoDecoder->TimeBase = time_base;

    VideoDecoder->pFormatContext = pFormatContext;
    VideoDecoder->pVideoCodecCtx = pVideoCodecCtx;
    VideoDecoder->VideoStreamIndex = videoStreamIndex;
    VideoDecoder->pPacket = av_packet_alloc();
    VideoDecoder->Duration = duration;
    VideoDecoder->Fps = FPS;

    pthread_t DecoderThread = {0};
    pthread_t ConvertThread = {0};

    VideoDecoder->threads[0] = DecoderThread;
    VideoDecoder->threads[1] = ConvertThread;

    VideoDecoder->SemaphoreDecoder = dispatch_semaphore_create(0);
    VideoDecoder->SemaphoreConvertion = dispatch_semaphore_create(0);
    
    if (pthread_create(&VideoDecoder->threads[0], NULL, DoDecoding, VideoDecoder) != 0)
    {
        perror("pthread_create");
        return NULL;
    }
    if (pthread_create(&VideoDecoder->threads[1], NULL, DoConvertion, VideoDecoder) != 0)
    {
        perror("pthread_create");
        return NULL;
    }
    return VideoDecoder;

    //For now we can add extra thread for DoConvertion if needed

}