#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
 
int main(int argc, char *argv[])
{
        avdevice_register_all();
        avcodec_register_all();
     
        const char  *filenameSrc = "FaceTime HD Camera";
     
        AVCodecContext  *pCodecCtx;
        AVFormatContext *pFormatCtx = avformat_alloc_context();
    
//    
    AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        printf("Codec not found\b");
        exit(-1);
    }
    pCodecCtx = avcodec_alloc_context3(codec);
//    if(avcodec_copy_context(pCodecCtx, pFormatCtx->streams[stream_index]->codec) != 0) {
//        fprintf(stderr, "Couldn't copy codec context");
//        return -1; // Error copying codec context
//    }
//    if(avcodec_open2(pCodecCtx, codec, NULL) < 0) {
//        fprintf(stderr, "Unsupported codec!\n");
//        return -1;
//    }
//
//    pFormatCtx->video_codec_id = AV_CODEC_ID_H264;
//    av_format_set_video_codec(pFormatCtx, codec);
    
        AVCodec * pCodec;
        AVInputFormat *iformat = av_find_input_format("avfoundation");
        AVFrame *pFrame, *pFrameRGB;
    
    
     
    AVDictionary *opt = NULL;
    //av_dict_set(&opt, "vcodec", "mjpeg", 0);
    //av_dict_set(&opt, "vcodec", &codec, 0);
    av_dict_set(&opt, "video_size", "640x480", 0);
    av_dict_set(&opt, "framerate", "30", 0);
    
    
        if(avformat_open_input(&pFormatCtx,filenameSrc,iformat,&opt) != 0) return -12;
        if(avformat_find_stream_info(pFormatCtx, NULL) < 0)   return -13;
        av_dump_format(pFormatCtx, 0, filenameSrc, 0);
        int videoStream = 1;
        for(int i=0; i < pFormatCtx->nb_streams; i++)
            {
                    if(pFormatCtx->streams[i]->codec->coder_type==AVMEDIA_TYPE_VIDEO)
                        {
                                videoStream = i;
                                break;
                            }
                }
     
    printf("Video: %d\n", videoStream);
    
        if(videoStream == -1) return -14;
        pCodecCtx = pFormatCtx->streams[videoStream]->codec;
     
        pCodec =avcodec_find_decoder(pCodecCtx->codec_id);
        if(pCodec==NULL) return -15; //codec not found
     
        if(avcodec_open2(pCodecCtx,pCodec,NULL) < 0) return -16;
     
        pFrame    = av_frame_alloc();
        pFrameRGB = av_frame_alloc();
     
        uint8_t *buffer;
        int numBytes;
     
        enum AVPixelFormat  pFormat = AV_PIX_FMT_BGR24;
        numBytes = avpicture_get_size(pFormat,pCodecCtx->width,pCodecCtx->height) ;
        buffer = (uint8_t *) av_malloc(numBytes*sizeof(uint8_t));
        avpicture_fill((AVPicture *) pFrameRGB,buffer,pFormat,pCodecCtx->width,pCodecCtx->height);
     
        int res;
        int frameFinished;
        AVPacket packet;
        while(res = av_read_frame(pFormatCtx,&packet)>=0)
            {
             
                    if(packet.stream_index == videoStream){
                 
                            avcodec_decode_video2(pCodecCtx,pFrame,&frameFinished,&packet);
                 
                            if(frameFinished){
                     
                                    struct SwsContext * img_convert_ctx;
                                    img_convert_ctx = sws_getCachedContext(NULL,pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,   pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_BGR24, SWS_BICUBIC, NULL, NULL,NULL);
                                    sws_scale(img_convert_ctx, ((AVPicture*)pFrame)->data, ((AVPicture*)pFrame)->linesize, 0, pCodecCtx->height, ((AVPicture *)pFrameRGB)->data, ((AVPicture *)pFrameRGB)->linesize);
                     
                                    printf("Go framee\n");
                     
                                    av_free_packet(&packet);
                                    sws_freeContext(img_convert_ctx);
                     
                                }
                 
                        }
             
                }
     
        av_free_packet(&packet);
        avcodec_close(pCodecCtx);
        av_free(pFrame);
        av_free(pFrameRGB);
        avformat_close_input(&pFormatCtx);
     
        return 0;
}
