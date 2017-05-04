/*
 * Copyright (c) 2012 Stefano Sabatini
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @example resampling_audio.c
 * libswresample API use example.
 */

#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

#include <libavcodec/avcodec.h>

#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#include <SDL2/SDL.h>

#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

const char *pCamName = ":Built-in Microphone";
AVFormatContext *pCamFormatCtx = NULL;
AVInputFormat *pCamInputFormat = NULL;
AVDictionary *pCamOpt = NULL;
AVCodecContext *pCamCodecCtx = NULL;
AVCodec *pCamCodec = NULL;
AVPacket camPacket;
AVFrame *pCamFrame = NULL;
int camAudioStreamIndex = -1;
struct SwsContext *pCamSwsContext = NULL;
struct SwrContext *swr_ctx = NULL;




static  Uint8  *audio_chunk;
static  Uint32  audio_len;
static  Uint8  *audio_pos;

/* The audio function callback takes the following parameters:
 * stream: A pointer to the audio buffer to be filled
 * len: The length (in bytes) of the audio buffer
 */
void  fill_audio(void *udata,Uint8 *stream,int len){
    printf("Callback: %d. audio_len: %d\n", len, audio_len);
    //SDL 2.0
    SDL_memset(stream, 0, len);
    if(audio_len==0)		/*  Only  play  if  we  have  data  left  */
        return;
    len=(len>audio_len?audio_len:len);	/*  Mix  as  much  data  as  possible  */
    
    SDL_memcpy (stream, audio_pos, len);
    //SDL_MixAudio(stream,audio_pos,len,SDL_MIX_MAXVOLUME);
    audio_pos += len;
    audio_len -= len;
}





static int get_format_from_sample_fmt(const char **fmt,
                                      enum AVSampleFormat sample_fmt)
{
    int i;
    struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt; const char *fmt_be, *fmt_le;
    } sample_fmt_entries[] = {
        { AV_SAMPLE_FMT_U8,  "u8",    "u8"    },
        { AV_SAMPLE_FMT_S16, "s16be", "s16le" },
        { AV_SAMPLE_FMT_S32, "s32be", "s32le" },
        { AV_SAMPLE_FMT_FLT, "f32be", "f32le" },
        { AV_SAMPLE_FMT_DBL, "f64be", "f64le" },
    };
    *fmt = NULL;
    
    for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt) {
            *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
            return 0;
        }
    }
    
    fprintf(stderr,
            "Sample format %s not supported as output format\n",
            av_get_sample_fmt_name(sample_fmt));
    return AVERROR(EINVAL);
}

/**
 * Fill dst buffer with nb_samples, generated starting from t.
 */
static void fill_samples(float *dst, int nb_samples, int nb_channels, int sample_rate, float *t)
{
    int i, j;
    float tincr = 1.0 / sample_rate, *dstp = dst;
    const float c = 2 * M_PI * 440.0;
    
    /* generate sin tone with 440Hz frequency and duplicated channels */
    for (i = 0; i < nb_samples; i++) {
//        *dstp = sin(c * *t);
//        for (j = 1; j < nb_channels; j++)
//            dstp[j] = dstp[0];
//        dstp += nb_channels;
        *t += tincr;
    }
}

int main(int argc, char **argv)
{
    int64_t src_ch_layout = AV_CH_LAYOUT_STEREO, dst_ch_layout = AV_CH_LAYOUT_STEREO;
    int src_rate = 44100, dst_rate = 44100;
    uint8_t **src_data = NULL, **dst_data = NULL;
    int src_nb_channels = 0, dst_nb_channels = 0;
    int src_linesize, dst_linesize;
    int src_nb_samples = 512, dst_nb_samples, max_dst_nb_samples;
    enum AVSampleFormat src_sample_fmt = AV_SAMPLE_FMT_FLT, dst_sample_fmt = AV_SAMPLE_FMT_S16;
    const char *dst_filename = NULL;
    FILE *dst_file;
    int dst_bufsize;
    const char *fmt;
    struct SwrContext *swr_ctx;
    float t;
    int ret;
    
    /* register all the codecs */
    avcodec_register_all();
    avdevice_register_all();
    
    
    
    
    pCamFormatCtx = avformat_alloc_context();
    pCamInputFormat = av_find_input_format("avfoundation");
    av_dict_set(&pCamOpt, "video_size", "640x480", 0);
    av_dict_set(&pCamOpt, "framerate", "30", 0);
    av_dict_set(&pCamOpt, "sample_format", "s16", 0);
    av_dict_set(&pCamOpt, "sample_fmt", "s16", 0);
    av_dict_set(&pCamOpt, "channels", "asd", 0);
    av_dict_set(&pCamOpt, "sample_rate", "asd", 0);
    if (avformat_open_input(&pCamFormatCtx, pCamName, pCamInputFormat, &pCamOpt) != 0) {
        printf("Camera: Can't open format\n");
        return -1;
    }
    if (avformat_find_stream_info(pCamFormatCtx, NULL) < 0) {
        printf("Camera: Can't find stream information\n");
        return -1;
    }
    av_dump_format(pCamFormatCtx, 0, pCamName, 0);
    for(int i=0; i<pCamFormatCtx->nb_streams; i++) {
        if(pCamFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            camAudioStreamIndex = i;
            break;
        }
    }
    printf("Audio stream index: %d\n", camAudioStreamIndex);
    if (camAudioStreamIndex == -1) {
        return -1;
    }
    pCamCodecCtx = pCamFormatCtx->streams[camAudioStreamIndex]->codec;
    pCamCodec = avcodec_find_decoder(pCamCodecCtx->codec_id);
    if (pCamCodec==NULL) {
        printf("Codec %d not found\n", pCamCodecCtx->codec_id);
        return -1;
    }
    if (avcodec_open2(pCamCodecCtx, pCamCodec, NULL) < 0) {
        printf("Can't open audio codec\n");
        return -1;
    }
    
    pCamFrame = av_frame_alloc();
    if (!pCamFrame) {
        fprintf(stderr, "Could not allocate audio frame\n");
        exit(1);
    }
    uint64_t out_channel_layout=AV_CH_LAYOUT_STEREO;
    //nb_samples: AAC-1024 MP3-1152
    int out_nb_samples=1152;
    enum AVSampleFormat out_sample_fmt;
    out_sample_fmt=AV_SAMPLE_FMT_S16;
    int out_sample_rate=44100;
    int out_channels=av_get_channel_layout_nb_channels(out_channel_layout);
    //Out Buffer Size
    int out_buffer_size=av_samples_get_buffer_size(NULL,out_channels ,out_nb_samples,out_sample_fmt, 1);
    uint8_t			*out_buffer=(uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE*2);
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        printf( "Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }
    
    SDL_AudioSpec wanted_spec;
    wanted_spec.freq = out_sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = out_channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = out_nb_samples;
    wanted_spec.callback = fill_audio;
    wanted_spec.userdata = NULL;
    
    if (SDL_OpenAudio(&wanted_spec, NULL)<0){
        printf("can't open audio.\n");
        return -1;
    }
    
    
    
    
    
    
    
    if (argc != 2) {
        fprintf(stderr, "Usage: %s output_file\n"
                "API example program to show how to resample an audio stream with libswresample.\n"
                "This program generates a series of audio frames, resamples them to a specified "
                "output format and rate and saves them to an output file named output_file.\n",
                argv[0]);
        exit(1);
    }
    dst_filename = argv[1];
    
    dst_file = fopen(dst_filename, "wb");
    if (!dst_file) {
        fprintf(stderr, "Could not open destination file %s\n", dst_filename);
        exit(1);
    }
    
    /* create resampler context */
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        fprintf(stderr, "Could not allocate resampler context\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }
    
    /* set options */
    av_opt_set_int(swr_ctx, "in_channel_layout",    src_ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate",       src_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", src_sample_fmt, 0);
    
    av_opt_set_int(swr_ctx, "out_channel_layout",    dst_ch_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate",       dst_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_sample_fmt, 0);
    
    /* initialize the resampling context */
    if ((ret = swr_init(swr_ctx)) < 0) {
        fprintf(stderr, "Failed to initialize the resampling context\n");
        goto end;
    }
    
    /* allocate source and destination samples buffers */
    
    src_nb_channels = av_get_channel_layout_nb_channels(src_ch_layout);
    ret = av_samples_alloc_array_and_samples(&src_data, &src_linesize, src_nb_channels,
                                             src_nb_samples, src_sample_fmt, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate source samples\n");
        goto end;
    }
    
    /* compute the number of converted samples: buffering is avoided
     * ensuring that the output buffer will contain at least all the
     * converted input samples */
    max_dst_nb_samples = dst_nb_samples =
    av_rescale_rnd(src_nb_samples, dst_rate, src_rate, AV_ROUND_UP);
    
    dst_nb_samples = 1152;
    
    /* buffer is going to be directly written to a rawaudio file, no alignment */
    dst_nb_channels = av_get_channel_layout_nb_channels(dst_ch_layout);
    ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, dst_nb_channels,
                                             dst_nb_samples, dst_sample_fmt, 0);
    
    printf("SAMPLES: %d vs. %d\n", dst_nb_samples, ret);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate destination samples\n");
        goto end;
    }
    
    t = 0;
    int x = 0;
    do {
        int ret = av_read_frame(pCamFormatCtx, &camPacket);
        
        if (camPacket.stream_index == camAudioStreamIndex) {
            
            AVFrame *decoded_frame = av_frame_alloc();
            //decoded_frame->nb_samples = 1152;
            int camFrameFinished = 0;
            //decode(pCamCodecCtx,&camPacket,decoded_frame,NULL);
            int size = avcodec_decode_audio4 (pCamCodecCtx, decoded_frame, &camFrameFinished, &camPacket);
            
            int sampleCount = 0;
            if (camFrameFinished) {
                src_data = decoded_frame->data;
                //http://stackoverflow.com/questions/32051847/c-ffmpeg-distorted-sound-when-converting-audio
                uint8_t *convertedData=NULL;
                
                if (av_samples_alloc(&convertedData, NULL, 2, 1152, dst_sample_fmt, 0) < 0) {
                    printf("ERROR\n");
                    exit(-1);
                }
                
                int outSamples = swr_convert(swr_ctx, NULL, 0,
                                             //&convertedData,
                                             //audioFrameConverted->nb_samples,
                                             (const uint8_t **)src_data, src_nb_samples);
                if (outSamples < 0) {
                    printf("No samples \n");
                    exit(-1);
                }
                
                for (;;) {
                    outSamples = swr_get_out_samples(swr_ctx, 0);
                    printf("Out: %d\n", outSamples);
                    // 2 = channels of dest
                    // 1152 = frame_size of dest
                    if (outSamples < 1152 * 2) {
                        break;
                    }
                    
                    outSamples = swr_convert(swr_ctx, dst_data, 1152, NULL, 0);
                    
                    printf("Do it withOut samples: %d\n", outSamples);
                    
                    while(audio_len>0)//Wait until finish
                        SDL_Delay(1);
                    
                    //Set audio buffer (PCM data)
                    audio_chunk = (Uint8 *) dst_data[0];
                    //audio_chunk = convertedData;
                    //Audio buffer length
                    audio_len =out_buffer_size;
                    audio_pos = audio_chunk;
                    
                    //Play
                    SDL_PauseAudio(0);
                    
                    x++;
                    
                    if (x > 100) {
                        printf("EXIT\n");
                        exit(-1);
                    }
                }
//                printf("Stream (mic 2): Sample rate: %d, Channel layout: %d, Channels: %d, Samples: %d\n", decoded_frame->sample_rate, decoded_frame->channel_layout, decoded_frame->channels, decoded_frame->nb_samples);
//                
//                decoded_frame->nb_samples = 1152;
//                
//                //float tincr = 1.0 / src_rate;
//                //t += tincr;
//                
//                src_data = decoded_frame->data;
//                
//                /* generate synthetic audio */
//                fill_samples((float *)src_data[0], src_nb_samples, src_nb_channels, src_rate, &t);
//                
//                /* compute destination number of samples */
////                dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, src_rate) +
////                                                src_nb_samples, dst_rate, src_rate, AV_ROUND_UP);
////                if (dst_nb_samples > max_dst_nb_samples) {
////                    av_freep(&dst_data[0]);
////                    ret = av_samples_alloc(dst_data, &dst_linesize, dst_nb_channels,
////                                           dst_nb_samples, dst_sample_fmt, 1);
////                    if (ret < 0)
////                        break;
////                    max_dst_nb_samples = dst_nb_samples;
////                }
//                
//                dst_nb_samples = 1152;
//                
//                /* convert to destination format */
//                //ret = swr_convert(swr_ctx, dst_data, dst_nb_samples, (const uint8_t **)src_data, src_nb_samples);
//                ret = swr_convert(swr_ctx, decoded_frame->data, dst_nb_samples, (const uint8_t **)src_data, src_nb_samples);
//                printf("t:%f in:%d out:%d\n", t, src_nb_samples, ret);
//                if (ret < 0) {
//                    fprintf(stderr, "Error while converting\n");
//                    goto end;
//                }
//                
//                //decoded_frame->data =NULL;
//                
////                dst_bufsize = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels,
////                                                         ret, dst_sample_fmt, 1);
////                if (dst_bufsize < 0) {
////                    fprintf(stderr, "Could not get sample buffer size\n");
////                    goto end;
////                }
////                printf("t:%f in:%d out:%d\n", t, src_nb_samples, ret);
////                fwrite(dst_data[0], 1, dst_bufsize, dst_file);
////                
//                
//                
//                while(audio_len>0)//Wait until finish
//                    SDL_Delay(1);
//                
//                //Set audio buffer (PCM data)
//                //audio_chunk = (Uint8 *) dst_data[0];
//                audio_chunk = decoded_frame->data[0];
//                //Audio buffer length
//                audio_len =out_buffer_size;
//                audio_pos = audio_chunk;
//                
//                //Play
//                SDL_PauseAudio(0);
                
            }
            
            //printf("Got frame: %d. Index: %d\n", ret, camPacket.stream_index);
            
            
        }
        
        
        
        
        
        
        
        
        
    } while (t < 4);
    
    if ((ret = get_format_from_sample_fmt(&fmt, dst_sample_fmt)) < 0)
        goto end;
    fprintf(stderr, "Resampling succeeded. Play the output file with the command:\n"
            "ffplay -f %s -channel_layout %"PRId64" -channels %d -ar %d %s\n",
            fmt, dst_ch_layout, dst_nb_channels, dst_rate, dst_filename);
    
end:
    fclose(dst_file);
    
    if (src_data)
        av_freep(&src_data[0]);
    av_freep(&src_data);
    
    if (dst_data)
        av_freep(&dst_data[0]);
    av_freep(&dst_data);
    
    swr_free(&swr_ctx);
    return ret < 0;
}
