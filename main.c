#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/avstring.h>

#include <time.h>

// Increase:
// analyzeduration
// probesize

#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

//// A global buffer size just to have one
//#define BUFFER_SIZE 640
//
//// The IP where the broadcast is sent to (the whole network)
//#define DISCOVER_BROADCAST_IP "255.255.255.255"
//// The port where the broadcast is sent to
//#define DISCOVER_BROADCAST_PORT 51205
//// The port we listen when waiting for streams
//#define REGISTER_BROADCAST_PORT 51206
//
//#define RPI_COMMAND_IP "192.168.0.100"	// The ip of the rpi to send commands to
//#define RPI_COMMAND_PORT 51200   		// The port of the rpi to send commands to

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

#define VIDEO_PICTURE_QUEUE_SIZE 1
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

typedef struct VideoPicture {
    //SDL_Overlay *bmp;
    
    int width, height; /* source height & width */
    int allocated;
} VideoPicture;

typedef struct VideoState {
    
    AVFormatContext *pFormatCtx;
    int             videoStream;
    AVStream        *video_st;
    AVCodecContext  *video_ctx;
    PacketQueue     videoq;
    struct SwsContext *sws_ctx;
    
    VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
    int             pictq_size, pictq_rindex, pictq_windex;
    SDL_mutex       *pictq_mutex;
    SDL_cond        *pictq_cond;
    
    SDL_Thread      *parse_tid;
    SDL_Thread      *video_tid;
    
    SDL_Texture     *texture;
    
    char            url[1024];
    int             quit;
} VideoState;

const int SCREEN_WIDTH = 640;
const int SCREEN_HEIGHT = 480;
//const int FRAMES_PER_SECOND = 10;
//
//bool isRunning;
//
bool initSDL();
bool createRenderer();
void setupRenderer();

void setupButtonPositions();
SDL_mutex *screen_mutex = NULL;
SDL_Window *gWindow = NULL;
SDL_Renderer *renderer = NULL;
SDL_Rect btnStart;
SDL_Rect btnStop;

// TODO: Put in VideoState
size_t yPlaneSz, uvPlaneSz;
Uint8 *yPlane, *uPlane, *vPlane;
int uvPitch;

// Since we only have one decoding thread, the Big Struct can be global in case we need it.
VideoState *global_video_state;

void packet_queue_init(PacketQueue *q) {
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}
int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
    
    AVPacketList *pkt1;
    // TODO: use AVPacket tmp_pkt = {0}; instead
    // TODO:
    /*
     ret = av_packet_ref(&tmp_pkt, pkt);
     if (ret < 0)
     exit_program(1);
     */
    if(av_dup_packet(pkt) < 0) {
        return -1;
    }
    pkt1 = av_malloc(sizeof(AVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;
    
    SDL_LockMutex(q->mutex);
    
    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    SDL_CondSignal(q->cond);
    
    SDL_UnlockMutex(q->mutex);
    return 0;
}
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
    printf("[packet_queue_get]\n");
    AVPacketList *pkt1 = NULL;
    int ret = 0;
    
    SDL_LockMutex(q->mutex);
    
    while(true) {
        
        if(global_video_state->quit) {
            ret = -1;
            break;
        }
        
        pkt1 = q->first_pkt;
        if (pkt1) {
            printf("[packet_queue_get] 1\n");
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            printf("[packet_queue_get] 2\n");
            ret = 0;
            break;
        } else {
            printf("[packet_queue_get] 3\n");
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    printf("[packet_queue_get] End.\n");
    return ret;
}

//void closeSDL();
//
//// The window we'll be rendering to

//SDL_Renderer* renderer;
//SDL_Texture *texture;
//
//int startTicks;
//// A thread that waits for incoming registrations
//pthread_t registerThread;
//// A thread the processes video streams
//pthread_t processThread;
//
//
//// Discover existing video streams in the network (done manually)
//void discoverStreams();
//// A video stream registers after discovery (done in a thread)
//void registerStream();
//
//char* urlStream = NULL;
//
//char* concat(char *s1, char *s2) {
//	char *result = malloc(strlen(s1) + strlen(s2) + 1); //+1 for the zero-terminator
//	//in real code you would check for errors in malloc here
//	strcpy(result, s1);
//	strcat(result, s2);
//	return result;
//}
//
//void die(char *s) {
//	perror(s);
//	exit(1);
//}
//
bool initSDL() {
	// Initialization flag
	bool success = true;

	// Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
		printf("[initSDL] Error: SDL could not initialize. SDL_Error: %s\n", SDL_GetError());
		success = false;
	} else {
		// Create window
		gWindow = SDL_CreateWindow("Livestream", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
		if (gWindow == NULL) {
			printf("[initSDL] Error: Window could not be created. SDL_Error: %s\n", SDL_GetError());
			success = false;
		} else {
			// We're ready to go
            printf("[initSDL] Success: We're ready to go\n");
			createRenderer();
			setupRenderer();
			setupButtonPositions();
		}
	}

	return success;
}
//
//void closeSDL() {
//	//Destroy window
//	SDL_DestroyWindow(gWindow);
//	gWindow = NULL;
//
//	//Quit SDL subsystems
//	SDL_Quit();
//}
//
//void startTimer() {
//	startTicks = SDL_GetTicks();
//}
//
//int getTicks() {
//	return SDL_GetTicks() - startTicks;
//}
//
bool createRenderer() {
	renderer = SDL_CreateRenderer(gWindow, -1, 0);
	if (renderer == NULL) {
		printf("[createRenderer] Failed to create renderer.\n");
		return false;
	}
	return true;
}

void setupRenderer() {
	// Set size of renderer to the same as window
	SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);
	// Set color of renderer to green
	SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
}

void setupButtonPositions() {
	// Start
	btnStart.x = 10;
	btnStart.y = 10;
	btnStart.w = 100;
	btnStart.h = 50;
	// Stop
	btnStop.x = 120;
	btnStop.y = 10;
	btnStop.w = 100;
	btnStop.h = 50;
}

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
    SDL_Event event;
    event.type = FF_REFRESH_EVENT;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);
    return 0; /* 0 means stop timer */
}

static void schedule_refresh(VideoState *is, int delay) {
    printf("[main] Schedule refresh with delay %d.\n", delay);
    SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

void showButton(SDL_Rect rect) {
	// Change color to blue
	SDL_SetRenderDrawColor(renderer, 0, 100, 200, 255);
	// Render our "player"
	SDL_RenderFillRect(renderer, &rect);
	// Change color to green
	SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
}
//
//int decode(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *pkt) {
//	int ret;
//
//	*got_frame = 0;
//
//	if (pkt) {
//		ret = avcodec_send_packet(avctx, pkt);
//		// In particular, we don't expect AVERROR(EAGAIN), because we read all
//		// decoded frames with avcodec_receive_frame() until done.
//		if (ret < 0)
//			return ret == AVERROR_EOF ? 0 : ret;
//	}
//
//	ret = avcodec_receive_frame(avctx, frame);
//	if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
//		return ret;
//	if (ret >= 0)
//		*got_frame = 1;
//
//	return 0;
//}
//
////void *processStream(void *dummy) {
//void* processStream() {
//
//	
//	AVFormatContext *pFormatCtx = NULL;
//	AVCodecContext *pCodecCtxOrig = NULL;
//	AVCodecContext *pCodecCtx = NULL;
//	AVPacket packet;
//	AVCodec *pCodec = NULL;
//	AVFrame *pFrame = NULL;
//	int videoStream;
//	int frameFinished;
//	struct SwsContext *sws_ctx = NULL;
//	size_t yPlaneSz, uvPlaneSz;
//	Uint8 *yPlane, *uPlane, *vPlane;
//	int uvPitch;
//	SDL_Event event;
//	unsigned i;
//
//	//while (isRunning) {
//	//	startTimer();
//	// Clear the window and make it all green
//	//SDL_RenderClear(renderer);
//
//	/*if (urlStream == NULL) {
//		printf("The URL of the stream is not available. Make sure that 'urlStream' is set.\n");
//		return NULL;
//	}*/
//
//	// Register all formats and codecs
//	av_register_all();
//
//	//char *url = "udp://192.168.0.12:51234?overrun_nonfatal=1&fifo_size=50000000";
//	//char *url = "tcp://192.168.0.100:51200?overrun_nonfatal=1";
//
//    //char *url = "tcp://127.0.0.1:51234?overrun_nonfatal=1";
//    // In case of packet loss (green frames, etc.) use &fifo_size=50000000
//    char *url = "udp://127.0.0.1:1234?overrun_nonfatal=1";
//	
//	//char* url = urlStream;
//	avformat_network_init();
//
//	// Need to probe buffer for input format unless you already know it
//	/* AVProbeData probe_data;
//	 probe_data.buf_size = 4096;
//	 probe_data.filename = "video.h264";
//	 probe_data.buf = (unsigned char *) malloc(probe_data.buf_size);
//	 //memcpy(probe_data.buf, pBuffer, 4096);
//	 AVInputFormat *pAVInputFormat = av_probe_input_format(&probe_data, 1);
//	 if(!pAVInputFormat)
//	 pAVInputFormat = av_probe_input_format(&probe_data, 0);
//	 // cleanup
//	 free(probe_data.buf);
//	 probe_data.buf = NULL;
//	 printf("Input format: %s", pAVInputFormat->long_name);*/
//
//	//avformat_flush(&pFormatCtx);
//	printf("Open stream\n");
//	// Open video file
//
//	/*AVInputFormat* fmt = av_find_input_format("mpegts");
//	 //AVInputFormat* fmt = av_find_input_format("mjpeg");
//	 if (fmt != NULL) {
//	 printf("Found: %s\n", fmt->name);
//	 }*/
//
//    int testing = 1;
//    while (testing == 1) {
//
//    
//    int result = avformat_open_input(&pFormatCtx, url, NULL, NULL);
//	printf("Result: %d\n", result);
//	//if (avformat_open_input(&pFormatCtx, "video.h264", NULL, NULL) != 0) {
//	if (result != 0) {
//		printf("Can't open file\n");
//		return -1; // Couldn't open file
//	}
//
//    
//	printf("Get stream information\n");
//	// Retrieve stream information
//    int result2 = avformat_find_stream_info(pFormatCtx, NULL);
//    printf("Result: %d\n", result2);
//	if (result2 < 0)
//		return -1; // Couldn't find stream information
//
//	printf("Get stream information: Done\n");
//
//	// Dump information about file onto standard error
//	av_dump_format(pFormatCtx, 0, url, 0);
//
//	printf("Received: %s\n", pFormatCtx->iformat->name);
//
//	// Find the first video stream
//	videoStream = -1;
//	for (i = 0; i < pFormatCtx->nb_streams; i++)
//		if (pFormatCtx->streams[i]->codecpar->codec_type
//				== AVMEDIA_TYPE_VIDEO) {
//			videoStream = i;
//			break;
//		}
//
//	printf("Video stream: %d\n", videoStream);
//
//	if (videoStream == -1)
//		return -1; // Didn't find a video stream
//
//	// Get a pointer to the codec context for the video stream
//	pCodecCtxOrig = pFormatCtx->streams[videoStream]->codec;
//	// Find the decoder for the video stream
//	pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
//	if (pCodec == NULL) {
//		fprintf(stderr, "Unsupported codec!\n");
//		return -1; // Codec not found
//	}
//
//	// Copy context
//	pCodecCtx = avcodec_alloc_context3(pCodec);
//	if (avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
//		fprintf(stderr, "Couldn't copy codec context");
//		return -1; // Error copying codec context
//	}
//
//	// Open codec
//	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
//		return -1; // Could not open codec
//
//	// Allocate video frame
//	pFrame = av_frame_alloc();
//    
//    printf("Width: %d Height %d\n", pCodecCtx->width, pCodecCtx->height);
//        if (pCodecCtx->width != 0 && pCodecCtx->height != 0) {
//            testing = 0;
//        } else {
//            
//            
//            
//            
//            avformat_close_input(&pFormatCtx);
//            pFormatCtx = NULL;
//        }
//    }
//    // In some cases these values are both 0 so we set them manually
//    pCodecCtx->width = 640;
//    pCodecCtx->height = 480;
//    // In some cases pCodecCtx->pix_fmt is -1, should be 0
//    
//	// Allocate a place to put our YUV image on that screen
//	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
//			SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
////    while (!texture) {
////        fprintf(stderr, "SDL: could not create texture - try again\n");
////        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
////                                    SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
////    }
//	if (!texture) {
//		fprintf(stderr, "SDL: could not create texture - exiting\n");
//		exit(1);
//	}
//
//	enum AVPixelFormat oldPixelFormat = pCodecCtx->pix_fmt;
//    
//    printf("Pixel format: %d\n", oldPixelFormat);
//
//	// initialize SWS context for software scaling
//	sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
//			//AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height,
//			//AV_PIX_FMT_YUV420P, SWS_BILINEAR,
//            AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height,
//            AV_PIX_FMT_YUV420P, SWS_BILINEAR,
//			NULL,
//			NULL,
//			NULL);
//
//	printf("SWS2 \n");
//
//	// set up YV12 pixel array (12 bits per pixel)
//	yPlaneSz = pCodecCtx->width * pCodecCtx->height;
//	uvPlaneSz = pCodecCtx->width * pCodecCtx->height / 4;
//	yPlane = (Uint8*) malloc(yPlaneSz);
//	uPlane = (Uint8*) malloc(uvPlaneSz);
//	vPlane = (Uint8*) malloc(uvPlaneSz);
//	if (!yPlane || !uPlane || !vPlane) {
//		fprintf(stderr, "Could not allocate pixel buffers - exiting\n");
//		exit(1);
//	}
//    
//    int novideocount = 0;
//
//	// http://stackoverflow.com/questions/17546073/how-can-i-seek-to-frame-no-x-with-ffmpeg
//
//	uvPitch = pCodecCtx->width / 2;
//	while (av_read_frame(pFormatCtx, &packet) >= 0) {
//
//        //printf("1 Received frame: %d\n", packet.stream_index);
//
//        if (novideocount > 50) {
//            time_t rawtime;
//            struct tm * timeinfo;
//            time ( &rawtime );
//            timeinfo = localtime ( &rawtime );
//            printf ( "No video at: %s", asctime (timeinfo) );
//            novideocount = 0;
//        }
//        
//		//printf("%d\n", )
//		//startTimer();
//		// Is this a packet from the video stream?
//		if (packet.stream_index == videoStream) {
//            novideocount = 0;
//            //printf("2 video\n");
//			// Decode video frame
//			//avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
//			decode(pCodecCtx, pFrame, &frameFinished, &packet);
//
//            //printf("3 decoded\n");
//			//printf("Decoded frame: %d %d %d\n", pFrame->pts, packet.pos, packet.pts);
//
//			// Did we get a video frame?
//			if (frameFinished) {
//                //printf("4 finished\n");
//				AVPicture pict;
//				pict.data[0] = yPlane;
//				pict.data[1] = uPlane;
//				pict.data[2] = vPlane;
//				pict.linesize[0] = pCodecCtx->width;
//				pict.linesize[1] = uvPitch;
//				pict.linesize[2] = uvPitch;
//
//				// Convert the image into YUV format that SDL uses
//				sws_scale(sws_ctx, (uint8_t const * const *) pFrame->data,
//						pFrame->linesize, 0, pCodecCtx->height, pict.data,
//						pict.linesize);
//
//				SDL_UpdateYUVTexture(texture,
//				NULL, yPlane, pCodecCtx->width, uPlane, uvPitch, vPlane,
//						uvPitch);
//
//				SDL_RenderClear(renderer);
//				SDL_RenderCopy(renderer, texture, NULL, NULL);
//				showButton(btnStart);
//				showButton(btnStop);
//				SDL_RenderPresent(renderer);
//			}
//        } else {
//            novideocount++;
//        }
//
//		// Free the packet that was allocated by av_read_frame
//		av_packet_unref(&packet);
////		SDL_PollEvent(&event);
////		switch (event.type) {
////                printf("Received event: %d\n");
////            case SDL_QUIT:
////                SDL_DestroyTexture(texture);
////                SDL_DestroyRenderer(renderer);
////                SDL_DestroyWindow(gWindow);
////                SDL_Quit();
////                exit(0);
////                break;
////            default:
////                break;
////		}
//        
//        //printf("x End of while\n");
//
//		//SDL_Delay(100);
//
//		/*if (getTicks() < 1000 / FRAMES_PER_SECOND) {
//		 //Sleep the remaining frame time
//		 SDL_Delay((1000 / FRAMES_PER_SECOND) - getTicks());
//		 }*/
//	}
//
//	// Render the changes above
//	//SDL_RenderPresent(renderer);
//
//	//If we want to cap the frame rate
//	/*if (getTicks() < 1000 / FRAMES_PER_SECOND) {
//	 //Sleep the remaining frame time
//	 SDL_Delay((1000 / FRAMES_PER_SECOND) - getTicks());
//	 }*/
//	//}
//	// Free the YUV frame
//	av_frame_free(&pFrame);
//	free(yPlane);
//	free(uPlane);
//	free(vPlane);
//
//	// Close the codec
//	avcodec_close(pCodecCtx);
//	avcodec_close(pCodecCtxOrig);
//
//	// Close the video file
//	avformat_close_input(&pFormatCtx);
//
//	/*SDL_Rect r;
//	 r.x = 100;
//	 r.y = 100;
//	 r.w = 100;
//	 r.h = 50;
//
//	 for (int i = 0; i < 10; i++) {
//	 printf("Processing stream\n");
//	 // Change color to blue
//	 SDL_SetRenderDrawColor(renderer, 0, 100, 200, 255);
//	 // Render our "player"
//	 SDL_RenderFillRect(renderer, &r);
//	 // Change color to green
//	 SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
//	 sleep(1);
//	 }*/
//	return NULL;
//}
//
//void handleButtonClick(int x, int y) {
//	if (x > btnStart.x && x < btnStart.x + btnStart.w && y > btnStart.y
//			&& y < btnStart.y + btnStart.h) {
//		printf("Register for video\n");
//		//sendCommand("REGISTER");
//		//pthread_create(&processThread, NULL, processStream, NULL);
//        
//        SDL_Thread *thread = SDL_CreateThread(processStream, "VideoThread", NULL);
//        if(thread == NULL) {
//            printf("Could not create thread\n");
//            return;
//        }
//		//processStream();
//		return;
//	}
//
//	if (x > btnStop.x && x < btnStop.x + btnStop.w && y > btnStop.y
//			&& y < btnStop.y + btnStop.h) {
//		printf("Discover video streams\n");
//		discoverStreams();
//		return;
//	}
//}
//
//void discoverStreams() {
//
//	int sock;
//	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
//		fprintf(stderr, "socket error");
//		exit(1);
//	}
//
//	int broadcastPermission = 1;
//	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
//			(void *) &broadcastPermission, sizeof(broadcastPermission)) < 0) {
//		perror("setsockopt() failed");
//	}
//
//	struct sockaddr_in broadcastAddr;
//	int broadcastPort = 51205;
//	memset(&broadcastAddr, 0, sizeof(broadcastAddr));
//	broadcastAddr.sin_family = AF_INET;
//	broadcastAddr.sin_addr.s_addr = inet_addr(DISCOVER_BROADCAST_IP);
//	broadcastAddr.sin_port = htons(DISCOVER_BROADCAST_PORT);
//
//	char* sendString = "DISCOVER";
//	int sendStringLen = strlen(sendString);
//
//	/* Broadcast sendString in datagram to clients */
//	if (sendto(sock, sendString, sendStringLen, 0,
//			(struct sockaddr *) &broadcastAddr, sizeof(broadcastAddr))
//			!= sendStringLen) {
//		fprintf(stderr, "sendto error");
//		exit(1);
//	}
//}
//
//void registerStream() {
//	struct sockaddr_in si_me, si_other;
//
//	int s, i, slen = sizeof(si_other), recv_len;
//	char buf[BUFFER_SIZE];
//
//	// Create a UDP socket
//	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
//		die("socket");
//	}
//
//	memset((char * ) &si_me, 0, sizeof(si_me));
//	si_me.sin_family = AF_INET;
//	si_me.sin_port = htons(REGISTER_BROADCAST_PORT);
//	si_me.sin_addr.s_addr = htonl(INADDR_ANY);
//
//	// Bind socket to port
//	if (bind(s, (struct sockaddr*) &si_me, sizeof(si_me)) == -1) {
//		die("bind");
//	}
//
//	// Keep listening for data
//	while (1) {
//		printf("Waiting for data...");
//		fflush(stdout);
//
//		// Try to receive some data, this is a blocking call
//		if ((recv_len = recvfrom(s, buf, BUFFER_SIZE, 0,
//				(struct sockaddr *) &si_other, &slen)) == -1) {
//			die("recvfrom()");
//		}
//
//		// Print details of the client/peer and the data received
//		printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr),
//				ntohs(si_other.sin_port));
//		printf("Data: %s\n", buf);
//
//		// TODO: Set IP and port of video stream
//		urlStream = "tcp://192.168.0.100:51200?overrun_nonfatal=1";
//		/*char* url = concat("tcp://", inet_ntoa(si_other.sin_addr));
//		url = concat(url, ":");
//		url = concat(url, ntohs(si_other.sin_port));
//		url = concat(url, "?overrun_nonfatal=1");
//		urlStream = url;*/
//		pthread_create(&processThread, NULL, processStream, NULL);
//	}
//}

int decode(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *pkt) {
    int ret;
    
    *got_frame = 0;
    
    if (pkt) {
        ret = avcodec_send_packet(avctx, pkt);
        // In particular, we don't expect AVERROR(EAGAIN), because we read all
        // decoded frames with avcodec_receive_frame() until done.
        if (ret < 0)
            return ret == AVERROR_EOF ? 0 : ret;
    }
    
    ret = avcodec_receive_frame(avctx, frame);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        return ret;
    if (ret >= 0)
        *got_frame = 1;
    
    return 0;
}

int queue_picture(VideoState *is, AVFrame *pFrame) {
    printf("[queue_picture]\n");
    
    VideoPicture *vp;
    int dst_pix_fmt;
    AVPicture pict;
    
    /* wait until we have space for a new pic */
    SDL_LockMutex(is->pictq_mutex);
    while(is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE &&
          !is->quit) {
        SDL_CondWait(is->pictq_cond, is->pictq_mutex);
    }
    SDL_UnlockMutex(is->pictq_mutex);
    
    if(is->quit)
        return -1;
    
    // windex is set to 0 initially
    vp = &is->pictq[is->pictq_windex];
    
//    /* allocate or resize the buffer! */
//    if(!vp->texture ||
//       vp->width != is->video_ctx->width ||
//       vp->height != is->video_ctx->height) {
//        
//        printf("----> CREATING NEW \n");
//        
//        vp->allocated = 0;
//        
//        vp->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
//                                    SDL_TEXTUREACCESS_STREAMING, is->video_ctx->width, is->video_ctx->height);
//        
//        if (!vp->texture) {
//            fprintf(stderr, "Error: SDL: could not create texture.\n");
//            exit(1);
//        }
//        
//        
//        
//        //alloc_picture(is);
//        if(is->quit) {
//            return -1;
//        }
//    }
    
    if(is->texture) {
        printf("YES, texture is set. display it\n");
        
        
        AVPicture pict;
        pict.data[0] = yPlane;
        pict.data[1] = uPlane;
        pict.data[2] = vPlane;
        pict.linesize[0] = is->video_ctx->width;
        pict.linesize[1] = uvPitch;
        pict.linesize[2] = uvPitch;
        
        // Convert the image into YUV format that SDL uses
        sws_scale(is->sws_ctx, (uint8_t const * const *) pFrame->data,
                  pFrame->linesize, 0, is->video_ctx->height, pict.data,
                  pict.linesize);
        
        /* now we inform our display thread that we have a pic ready */
        if(++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
            is->pictq_windex = 0;
        }
        SDL_LockMutex(is->pictq_mutex);
        is->pictq_size++;
        SDL_UnlockMutex(is->pictq_mutex);
    }
    return 0;
}

int video_thread(void *arg) {
    printf("[video_thread]\n");
    VideoState *is = (VideoState *)arg;
    AVFrame *pFrame = NULL;
    // TODO: Try with = NULL instead of creating a dummy object
    AVPacket pkt1;
    AVPacket *packet = &pkt1;
    int frameFinished = 0;
    
    
    // Allocate video frame
    pFrame = av_frame_alloc();
    
    while(true) {
        if(packet_queue_get(&is->videoq, packet, 1) < 0) {
            printf("[video_thread] We quit getting packages.\n");
            // Means we quit getting packets
            break;
        }
        printf("[video_thread] Decode another frame.\n");
        decode(is->video_ctx, pFrame, &frameFinished, packet);
        // Did we get a video frame?
        if (frameFinished) {
            printf("[video_thread] Frame is finished.\n");
            if(queue_picture(is, pFrame) < 0) {
                break;
            }      
        } else {
            printf("[video_thread] Frame is NOT finished.\n");
        }
        
        av_packet_unref(packet);
    }
    av_frame_free(&pFrame);
    return 0;
}

int decode_thread(void *arg) {
    printf("[decode_thread] Called.\n");
    
    VideoState *is = (VideoState *)arg;
    AVFormatContext *pFormatCtx = NULL;
    //AVCodecContext *pCodecCtxOrig = NULL;
    AVCodecContext *pCodecCtx = NULL;
    AVCodec *pCodec = NULL;
    AVPacket pkt1;
    AVPacket *packet = &pkt1;
    
    int video_index = -1;
    int i = 0;
    is->videoStream = -1;
    global_video_state = is;
    
    int testing = 1;
    while (testing == 1) {
        int result = avformat_open_input(&pFormatCtx, is->url, NULL, NULL);
        if (result != 0) {
            printf("[decode_thread] Error: Can't open input.\n");
            return -1;
        }
        printf("[decode_thread] Success: Opened input stream to %s.\n", is->url);
        
        is->pFormatCtx = pFormatCtx;
        
        printf("[decode_thread] Get stream information\n");
        // Retrieve stream information
        result = avformat_find_stream_info(pFormatCtx, NULL);
        if (result < 0) {
            printf("[decode_thread] Error: Can't find stream information.\n");
            return -1;
        }
        printf("[decode_thread] Success: Got stream information.\n");
        
        av_dump_format(pFormatCtx, 0, is->url, 0);
        
        printf("[decode_thread] Format name: %s.\n", pFormatCtx->iformat->name);
        
        // Find the first video stream
        video_index = -1;
        for (i = 0; i < pFormatCtx->nb_streams; i++) {
            if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_index = i;
                break;
            }
        }
        
        /*
         * stream_component_open() start
         */
        printf("[decode_thread] Video stream index: %d.\n", video_index);
        
        if (video_index < 0) {
            printf("[decode_thread] Could not find a video stream.\n");
            return -1;
        }
        
        AVCodecParameters *pCodecParams = pFormatCtx->streams[video_index]->codecpar;
        //pCodecCtxOrig = pFormatCtx->streams[video_index]->codec;
        // Find the decoder for the video stream
        //pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
        pCodec = avcodec_find_decoder(pCodecParams->codec_id);
        if (pCodec == NULL) {
            fprintf(stderr, "[decode_thread] Error: Codec with id %d not found.\n", pCodecParams->codec_id);
            return -1;
        }
        printf("[decode_thread] Found the codec.\n");

        // Copy context
        pCodecCtx = avcodec_alloc_context3(pCodec);
        result = avcodec_parameters_to_context(pCodecCtx, pCodecParams);
        if (result < 0) {
            printf("[decode_thread] Failed to copy decoder parameters to input decoder context\n");
            return -1;
        }
        
        printf("[decode_thread] Copied the decoder.\n");
        
        // Open codec
        if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
            fprintf(stderr, "[decode_thread] Error: Can't open codec.\n");
            return -1; // Could not open codec
        }
        
        printf("[decode_thread] Opened the decoder.\n");
        
        if (pCodecCtx->width != 0 && pCodecCtx->height != 0) {
            printf("[decode_thread] Success: Found codec information. Width: %d, Height %d.\n", pCodecCtx->width, pCodecCtx->height);
            testing = 0;
        } else {
            printf("[decode_thread] Warning: Some codec information is not set. Try again.\n");
            avformat_close_input(&pFormatCtx);
            pFormatCtx = NULL;
            continue;
        }

        is->videoStream = video_index;
        is->video_st = pFormatCtx->streams[video_index];
        is->video_ctx = pCodecCtx;
        packet_queue_init(&is->videoq);
        is->video_tid = SDL_CreateThread(video_thread, "video_thread", is);
        is->sws_ctx = sws_getContext(is->video_ctx->width, is->video_ctx->height,
                                     is->video_ctx->pix_fmt, is->video_ctx->width,
                                     is->video_ctx->height, AV_PIX_FMT_YUV420P,
                                     SWS_BILINEAR, NULL, NULL, NULL
                                     );
        is->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
                                        SDL_TEXTUREACCESS_STREAMING, is->video_ctx->width, is->video_ctx->height);
        uvPitch = is->video_ctx->width / 2;
        
        // set up YV12 pixel array (12 bits per pixel)
        yPlaneSz = is->video_ctx->width * is->video_ctx->height;
        uvPlaneSz = is->video_ctx->width * is->video_ctx->height / 4;
        yPlane = (Uint8*) malloc(yPlaneSz);
        uPlane = (Uint8*) malloc(uvPlaneSz);
        vPlane = (Uint8*) malloc(uvPlaneSz);
        if (!yPlane || !uPlane || !vPlane) {
            fprintf(stderr, "Could not allocate pixel buffers - exiting\n");
            exit(1);
        }
        
        /*
         * stream_component_open() end
         */
        while(true) {
            //printf("Do something\n");
            if(is->quit) {
                break;
            }
            // seek stuff goes here
            if(is->videoq.size > MAX_VIDEOQ_SIZE) {
                SDL_Delay(10);
                continue;
            }
            if(av_read_frame(is->pFormatCtx, packet) < 0) {
                if(is->pFormatCtx->pb->error == 0) {
                    SDL_Delay(100); /* no error; wait for user input */
                    continue;
                } else {
                    break;
                }
            }
            // Is this a packet from the video stream?
            if(packet->stream_index == is->videoStream) {
                packet_queue_put(&is->videoq, packet);
            //} else if(packet->stream_index == is->audioStream) {
                //packet_queue_put(&is->audioq, packet);
            } else {
                av_packet_unref(packet);
            }
        }
        /* all done - wait for it */
        while(!is->quit) {
            SDL_Delay(100);
        }
    }

    return 0;
}

void video_display(VideoState *is) {
    
    VideoPicture *vp;
    vp = &is->pictq[is->pictq_rindex];
    
    if (is->texture) {
        SDL_LockMutex(screen_mutex);
        
        SDL_UpdateYUVTexture(is->texture,
                             NULL, yPlane, is->video_ctx->width, uPlane, uvPitch, vPlane,
                             uvPitch);
        
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, is->texture, NULL, NULL);
        showButton(btnStart);
        showButton(btnStop);
        SDL_RenderPresent(renderer);
        
        SDL_UnlockMutex(screen_mutex);
    }
    
    
    
//    SDL_Rect rect;
//    VideoPicture *vp;
//    float aspect_ratio;
//    int w, h, x, y;
//    int i;
//    
//    vp = &is->pictq[is->pictq_rindex];
//    if(vp->bmp) {
//        if(is->video_ctx->sample_aspect_ratio.num == 0) {
//            aspect_ratio = 0;
//        } else {
//            aspect_ratio = av_q2d(is->video_ctx->sample_aspect_ratio) *
//            is->video_ctx->width / is->video_ctx->height;
//        }
//        if(aspect_ratio <= 0.0) {
//            aspect_ratio = (float)is->video_ctx->width /
//            (float)is->video_ctx->height;
//        }
//        h = screen->h;
//        w = ((int)rint(h * aspect_ratio)) & -3;
//        if(w > screen->w) {
//            w = screen->w;
//            h = ((int)rint(w / aspect_ratio)) & -3;
//        }
//        x = (screen->w - w) / 2;
//        y = (screen->h - h) / 2;
//        
//        rect.x = x;
//        rect.y = y;
//        rect.w = w;
//        rect.h = h;
//        SDL_LockMutex(screen_mutex);
//        SDL_DisplayYUVOverlay(vp->bmp, &rect);
//        SDL_UnlockMutex(screen_mutex);
//        
//    }
}

void video_refresh_timer(void *userdata) {
    
    VideoState *is = (VideoState *)userdata;
    VideoPicture *vp;
    
    if(is->video_st) {
        if(is->pictq_size == 0) {
            schedule_refresh(is, 1);
        } else {
            vp = &is->pictq[is->pictq_rindex];
            /* Now, normally here goes a ton of code
             about timing, etc. we're just going to
             guess at a delay for now. You can
             increase and decrease this value and hard code
             the timing - but I don't suggest that ;)
             We'll learn how to do it for real later.
             */
            schedule_refresh(is, 0);
            
            /* show the picture! */
            video_display(is);
            
            /* update queue for next picture! */
            if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
                is->pictq_rindex = 0;
            }
            SDL_LockMutex(is->pictq_mutex);
            is->pictq_size--;
            SDL_CondSignal(is->pictq_cond);
            SDL_UnlockMutex(is->pictq_mutex);
        }
    } else {
        schedule_refresh(is, 100);
    }
}

int main(int argc, char* argv[]) {
    
    SDL_Event       event;
    VideoState      *is;
    
    is = av_mallocz(sizeof(VideoState));
    
    // Register all formats and codecs
    av_register_all();
    avformat_network_init();
    
    // Start up SDL and create window
    if (!initSDL()) {
        printf("[main] Error: Failed to initialize SDL.\n");
        return -1;
    }
    
    // Show background and buttons
    SDL_RenderClear(renderer);
    showButton(btnStart);
    showButton(btnStop);
    SDL_RenderPresent(renderer);

    printf("[main] Success: SDL initialized.\n");
    screen_mutex = SDL_CreateMutex();
    
    char *url = "udp://127.0.0.1:1234?overrun_nonfatal=1";
    av_strlcpy(is->url, url, sizeof(is->url));
    
    printf("[main] Creating mutex.\n");
    is->pictq_mutex = SDL_CreateMutex();
    is->pictq_cond = SDL_CreateCond();
    
    schedule_refresh(is, 40);
    
    is->parse_tid = SDL_CreateThread(decode_thread, "decode_thread", is);
    if (!is->parse_tid) {
        printf("[main] Error: Could not create decode_thread.\n");
        av_free(is);
        return -1;
    }
    
    while(true) {
        SDL_WaitEvent(&event);
        switch(event.type) {
            case FF_QUIT_EVENT:
            case SDL_QUIT:
                printf("[main] Received quit event\n");
                is->quit = 1;
                SDL_Quit();
                return 0;
                break;
            case FF_REFRESH_EVENT:
                printf("[main] FF_REFRESH_EVENT.\n");
                video_refresh_timer(event.user.data1);
                break;
            default:
                break;
        }
    }
    
//
//		isRunning = true;
//
//		SDL_RenderClear(renderer);
//		showButton(btnStart);
//		showButton(btnStop);
//		SDL_RenderPresent(renderer);
//
//		//pthread_create(&registerThread, NULL, registerStream, NULL);
//
//		// Event handler
//		SDL_Event e;
//
//		while (isRunning) {
//            // Handle events on queue
//			//while (SDL_PollEvent(&e) != 0) {
//			if (SDL_WaitEvent(&e) != 0) {
//				//User requests quit
//				if (e.type == SDL_QUIT) {
//                    printf("Received quit event\n");
//                    SDL_DestroyTexture(texture);
//                    SDL_DestroyRenderer(renderer);
//                    SDL_DestroyWindow(gWindow);
//                    SDL_Quit();
//					isRunning = false;
//				}
//
//				if (e.type == SDL_MOUSEBUTTONDOWN) {
//					int x = e.button.x;
//					int y = e.button.y;
//					//printf("Clicked with mouse: %d / %d\n", x, y);
//					handleButtonClick(x, y);
//				}
//			}
//
//			// Update the surface
//			//SDL_UpdateWindowSurface(gWindow);
//		}
//        
//        printf("[main] Stopping\n");
//	}
//
//	// Free resources and close SDL
//	closeSDL();
//	// Stop all threads
//	pthread_join(registerThread, NULL);

	return 0;
}
