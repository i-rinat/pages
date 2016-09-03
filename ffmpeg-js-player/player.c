/**
 * FFmpeg sample player
 * Related to the article at http://habrahabr.ru/blogs/video/137793/
 * (changed)
 */
#include <assert.h>
#include <stdio.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <SDL.h>



#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"

int done = 0;
AVFormatContext *format_context = NULL;
int video_stream = -1;
AVCodecContext *codec_context;
AVFrame *frame;
AVFrame *frame_rgb;
struct SwsContext *img_convert_context;
SDL_Surface *screen;

void
draw_frame(void)
{
    int err;
    AVPacket packet;
    while (av_read_frame(format_context, &packet) >= 0) {
        if (packet.stream_index == video_stream) {
            // Video stream packet
            int frame_finished;
            avcodec_decode_video2(codec_context, frame, &frame_finished, &packet);

            if (frame_finished) {
                sws_scale(img_convert_context, frame->data, frame->linesize, 0,
                          codec_context->height, frame_rgb->data, frame_rgb->linesize);

                err = SDL_LockSurface(screen);
                assert(err == 0 && "SDL_LockSurface");

                for (int row = 0; row < codec_context->height; row ++) {
                    memcpy((uint8_t *)screen->pixels + screen->pitch * row,
                           (uint8_t *)frame_rgb->data[0] + (row * frame_rgb->linesize[0]),
                           frame_rgb->linesize[0]); // TODO: 4?
                }

                SDL_UnlockSurface(screen);
                SDL_Flip(screen);

                av_free_packet(&packet);
                return;
            }
        }

        // Free the packet that was allocated by av_read_frame
        av_free_packet(&packet);
    }

    done = 1;

#ifdef __EMSCRIPTEN__
    emscripten_cancel_main_loop();
#endif
}

Uint32
my_callbackfunc(Uint32 interval, void *param)
{
    SDL_Event event;
    SDL_UserEvent userevent;

    userevent.type = SDL_USEREVENT;
    userevent.code = 0;
    //~ userevent.data1 = &my_function;
    //~ userevent.data2 = param;

    event.type = SDL_USEREVENT;
    event.user = userevent;

    SDL_PushEvent(&event);
    return interval;
}

void
loop(void)
{
    static int k = 0;
    printf("frame %d\n", k++);
    SDL_Event event;

#ifdef __EMSCRIPTEN__
    draw_frame();
    if (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            exit(0);
        }
    }
#else
    if (SDL_WaitEvent(&event)) {
        if (event.type == SDL_QUIT) {
            exit(0);
        }

        if (event.type == SDL_USEREVENT) {
            draw_frame();
        }
    }
#endif
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s filename\n", argv[0]);
        return 0;
    }

    // Register all available file formats and codecs
    av_register_all();

    int err;
    // Init SDL with video support
    err = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    if (err < 0) {
        fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
        return -1;
    }

    // Open video file
    const char *filename = argv[1];
    err = avformat_open_input(&format_context, filename, NULL, NULL);
    if (err < 0) {
        fprintf(stderr, "ffmpeg: Unable to open input file. err = %x\n", err);
        fprintf(stderr, "filename = %s\n", filename);
        FILE *fp = fopen(filename, "rb");
        fprintf(stderr, "fp = %p\n", fp);
        return -1;
    }

    // Retrieve stream information
    err = avformat_find_stream_info(format_context, NULL);
    if (err < 0) {
        fprintf(stderr, "ffmpeg: Unable to find stream info\n");
        return -1;
    }

    // Dump information about file onto standard error
    av_dump_format(format_context, 0, argv[1], 0);

    // Find the first video stream
    for (video_stream = 0; video_stream < format_context->nb_streams; ++video_stream) {
        if (format_context->streams[video_stream]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            break;
        }
    }
    if (video_stream == format_context->nb_streams) {
        fprintf(stderr, "ffmpeg: Unable to find video stream\n");
        return -1;
    }

    codec_context = format_context->streams[video_stream]->codec;
    AVCodec *codec = avcodec_find_decoder(codec_context->codec_id);
    err = avcodec_open2(codec_context, codec, NULL);
    assert(err == 0 && "ffmpeg: Unable to open codec");

    screen = SDL_SetVideoMode(codec_context->width, codec_context->height, 0, 0);
    assert(screen && "Couldn't set video mode");

    uint8_t *bmp = NULL;
    int num_bytes =
        avpicture_get_size(AV_PIX_FMT_RGBA, codec_context->width, codec_context->height);
    bmp = av_malloc(num_bytes);
    assert(bmp && "Couldn't alloc rgb frame");

    img_convert_context =
        sws_getCachedContext(NULL, codec_context->width, codec_context->height,
                             codec_context->pix_fmt, codec_context->width, codec_context->height,
                             AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    if (img_convert_context == NULL) {
        fprintf(stderr, "Cannot initialize the conversion context\n");
        return -1;
    }

    frame = av_frame_alloc();
    frame_rgb = av_frame_alloc();

    avpicture_fill(frame_rgb, bmp, AV_PIX_FMT_RGBA, codec_context->width, codec_context->height);

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(loop, 25, 1);
#else
    SDL_TimerID my_timer_id = SDL_AddTimer(1000/25, my_callbackfunc, NULL);
    while (!done) {
        loop();
    }
#endif

    sws_freeContext(img_convert_context);
    av_free(frame);
    avcodec_close(codec_context);
    avformat_close_input(&format_context);
    SDL_Quit();
    return 0;
}
