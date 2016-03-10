
/***********************************************************************
 * Copyright 2016 Sensity Systems Inc. 
 ***********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include "mnannotate.h"

#define DEFAULT_MEDIA_BUFFER_SIZE	32*1024

#ifdef DEBUG
#define d_printf(fmt, args...)    if (debug) fprintf(stderr, fmt, ## args)
#else
#define d_printf(fmt, args...)
#endif


#define OUTPUT_IMAGE_YUV	0
#define OUTPUT_IMAGE_PPM	1
#define OUTPUT_IMAGE_PNG	2
#define OUTPUT_IMAGE_JPG	3


static int debug = 0;


/*
 * IO context for medianode video record
 */
typedef struct _mio_context {
    char *filename;
    AVIOContext *context;
    uint8_t *buffer;
    int buffer_size;
    int fd;
} MIOContext;


static int mio_read(void *data, uint8_t *buf, int buf_size);
static int64_t mio_seek(void *data, int64_t pos, int whence);


static MIOContext *
mio_init(const char *filename)
{
    MIOContext *mctx;

    if (!filename) 
	return NULL;

    mctx = (MIOContext *)malloc(sizeof(MIOContext));
    if (!mctx) {
	d_printf("Error: %s - Out of memory\n", __FUNCTION__);	
	return NULL;
    }
    memset(mctx, 0, sizeof(MIOContext));

    mctx->filename = strdup(filename);
    mctx->fd = open(mctx->filename, O_RDONLY);
    if (mctx->fd < 0) {
	d_printf("Error: %s - Failed to open stream file %s\n", __FUNCTION__, mctx->filename);
	return NULL;
    }

    mctx->buffer_size = DEFAULT_MEDIA_BUFFER_SIZE + FF_INPUT_BUFFER_PADDING_SIZE;
    mctx->buffer = (uint8_t *)av_malloc(mctx->buffer_size);    

    /*
     * Allocate the AVIOContext
     */
    mctx->context = avio_alloc_context(mctx->buffer, mctx->buffer_size, 
				       0,			/* write flag (1=true, 0=false) */
				       (void *)mctx,	 	/* user data passed to callback functions */
				       mio_read,
				       NULL,			/* No writing */
				       mio_seek);

    return mctx;
}


static void
mio_destroy(MIOContext *mctx)
{
    if (mctx) {
	if (mctx->fd)
	    close(mctx->fd);

	av_free(mctx->context);

#if 0
	/*
	 * av_free() seems to free the context's buffer also
	 */
	av_free(mctx->buffer);
	mctx->buffer = NULL;
#endif

	free(mctx);
    }
}


static AVInputFormat *
mio_get_input_format(MIOContext *mctx)
{
    AVProbeData probeData;
    int score = AVPROBE_SCORE_EXTENSION;
    uint8_t *buf;
    int n, len;

    if (!mctx)
	return NULL;

    buf = mctx->buffer;
    len = mctx->buffer_size;

    while (len > 0) {
	n = read(mctx->fd, buf, len);
	if (!n && errno == EINTR)
	    continue;
	if (n < 0)
	    break;

	buf += n;
	len -= n;
    }

    lseek(mctx->fd, 0, SEEK_SET);
    probeData.buf = mctx->buffer;
    probeData.buf_size = mctx->buffer_size - len;
    probeData.filename = "";

    return (av_probe_input_format2(&probeData, 1, &score));
}


static int 
mio_read(void *data, uint8_t *buf, int buf_size)
{
    MIOContext *mctx = (MIOContext *)data;
    int n;

    if (!data)
	return -1;

#ifdef DEBUG_TRACE
    d_printf(">>>>> %s: buf = %p, buf_size = %d\n", __FUNCTION__, buf, buf_size);
#endif

    do {
	n = read(mctx->fd, buf, buf_size);
	if (!n) break;
    } while (n < 0 && errno == EINTR);

    if (!n)
	n = AVERROR_EOF;

#if 0
    d_printf ("<<<< %s: n = %d\n", __FUNCTION__, n);
#endif

    return n;
}


static int64_t 
mio_seek(void *data, int64_t pos, int whence)
{
    MIOContext *mctx = (MIOContext *)data;
    struct stat sb;
    int res;

    if (!mctx)
	return -1;

#ifdef DEBUG_TRACE
    d_printf(">>>>> %s: pos = %ld, whence %d\n", __FUNCTION__, pos, whence);
#endif

    /*
     * whence: SEEK_SET, SEEK_CUR, SEEK_END (like fseek) and AVSEEK_SIZE
     */
    if (whence == AVSEEK_SIZE) {
	res = fstat(mctx->fd, &sb);
	if (res < 0)
	    return res;
	else {
#ifdef DEBUG
	    d_printf("##### AVSEEK_SIZE = %lld\n", (long long)sb.st_size);
#endif
	    return sb.st_size; //TODO
	}
    }
    
    return (lseek(mctx->fd, (long)pos, whence));
}


static int
generate_ppm_image(AVCodecContext *ctx, AVFrame *frame, char *filename)
{
    FILE *fh;
    int y;
			

    fh = fopen(filename, "wb");
    if (!fh) {
	d_printf("Error: Failed to write PPM image file %s\n", filename);
	return -1;
    }

    d_printf("##### Generate PPM image ... %s\n", filename);

    /*
     * Write header
     */
    fprintf(fh, "P6\n%d %d\n255\n", ctx->width, ctx->height);
  
    /*
     * Write pixel data
     */
    for (y = 0; y < ctx->height; y++)
	fwrite(frame->data[0] + y*frame->linesize[0], 1, ctx->width*3, fh);

    fclose(fh);

    return 0;
}


static int
generate_image_with_annotation(AVCodecContext *ctx, AVFrame *frame, int format, char *filename, char *annotation)
{
    uint8_t *video_data[4] = { NULL };
    int video_linesize[4];
    int video_bufsize;
    CvMat *image;

    /*
     * Allocate image buffer for the decoded image
     */
    video_bufsize = av_image_alloc(video_data, video_linesize, ctx->width, ctx->height, ctx->pix_fmt, 1);
    if (video_bufsize < 0) {
	d_printf("Error: Failed to allocate raw video buffer\n");
	return -1;
    }

    d_printf("##### Generate raw video image ... %s\n", filename);

    /*
     * copy decoded frame to raw video buffer:
     * this is required since rawvideo expects non aligned data
     */
    av_image_copy(video_data, video_linesize,
		  (const uint8_t **)(frame->data), frame->linesize, ctx->pix_fmt, ctx->width, ctx->height);

    image = LoadImageBuffer(video_data[0], ctx->width, ctx->height, PIXEL_FORMAT_IYUV);
#if 0
    cvShowImage("Sensity", image);
    cvWaitKey(0);
#endif

    if (annotation)
	AnnoteImage(image, annotation);

#if 0
    cvShowImage("Sensity", image);
    cvWaitKey(0);
#endif

    if (filename)
	cvSaveImage(filename, image, NULL);

    cvReleaseMat(&image);

    av_free(video_data[0]);

    return 0;
}


static int
generate_raw_image(AVCodecContext *ctx, AVFrame *frame, char *filename)
{
    FILE *fh;
    uint8_t *video_data[4] = { NULL };
    int video_linesize[4];
    int video_bufsize;
			

    fh = fopen(filename, "wb");
    if (!fh) {
	d_printf("Error: Failed to create raw image file %s\n", filename);
	return 0;
    }

    /* 
     * Allocate image buffer for the decoded image
     */
    video_bufsize = av_image_alloc(video_data, video_linesize, ctx->width, ctx->height, ctx->pix_fmt, 1);
    if (video_bufsize < 0) {
	d_printf("Error: Failed to allocate raw video buffer\n");
	return -1;
    }

    d_printf("##### Generate raw video image ... %s\n", filename);

    /* 
     * copy decoded frame to raw video buffer:
     * this is required since rawvideo expects non aligned data
     */
    av_image_copy(video_data, video_linesize,
		  (const uint8_t **)(frame->data), frame->linesize, ctx->pix_fmt, ctx->width, ctx->height);

    fwrite(video_data[0], 1, video_bufsize, fh);
    fclose(fh);

    av_free(video_data[0]);

    return 0;
}


static int
generate_png_image(AVCodecContext *ctx, AVFrame *frame, char *filename)
{
    FILE *fh;
    AVPacket packet;
    int res, image_ready;

    av_init_packet(&packet);
    packet.size = 0;
    packet.data = NULL;
    image_ready = 0;

    d_printf("##### Generate PNG image ... %s\n", filename);

    res = avcodec_encode_video2(ctx, &packet, frame, &image_ready);
    if (res >= 0 && image_ready) {
	fh = fopen(filename, "wb");
	if (!fh) {
	    d_printf("Error: Failed to create PNG image file %s\n", filename);
	    return -1;
	}

	fwrite(packet.data, packet.size, 1, fh);
	fclose(fh);
    }

    av_free_packet(&packet);

    return 0;
}


static int
generate_jpg_image(AVCodecContext *ctx, AVFrame *frame, char *filename)
{
    FILE *fh;
    AVPacket packet;
    int res, image_ready;

    av_init_packet(&packet);
    packet.size = 0;
    packet.data = NULL;
    image_ready = 0;

    d_printf("##### Generate JPEG image ... %s\n", filename);

    res = avcodec_encode_video2(ctx, &packet, frame, &image_ready);
    if (res >= 0 && image_ready) {
	fh = fopen(filename, "wb");
	if (!fh) {
	    d_printf("Error: Failed to create JPEG image file %s\n", filename);
	    return -1;
	}

	fwrite(packet.data, packet.size, 1, fh);
	fclose(fh);
    }

    av_free_packet(&packet);

    return 0;
}





static void
print_usage(void)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: mngrab [OPTION]... [FILE]\n");
    fprintf(stderr, "Grab a frame from media record FILE and output in image format.\n");
    fprintf(stderr, "  -d	turn on debug message\n");
    fprintf(stderr, "  -t	play time of the frame in milisecond\n");
    fprintf(stderr, "  -n	number of consecutive frames\n");
    fprintf(stderr, "  -i	image format of the generated frames\n");
    fprintf(stderr, "  -p	prefix of the image filename\n");
    fprintf(stderr, "  -a	performe image annotation based on the JSON annotation request\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:  mngrab -t 2000 -n 5 -i png -p camera_1H mnrecord_1H.mnf\n");
    fprintf(stderr, "           cat annotation.jason | mngrab -t 2000 -n 5 -i png -p camera_1H mnrecord_1H.mnf\n");
    fprintf(stderr, "\n");
}


int
main(int argc, char **argv)
{
    MIOContext *mctx;
    int res, i, program, frame_decode_done;
    int codec_id = AV_CODEC_ID_MJPEG;
    int pixel_format = PIX_FMT_YUVJ420P;
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *dec_codec_ctx = NULL;
    AVCodec *dec_codec = NULL;
    AVCodecContext *enc_codec_ctx = NULL;
    AVCodec *enc_codec = NULL;
    AVFrame *decode_frame = NULL;
    AVFrame *output_frame = NULL;
    struct SwsContext *sws_ctx = NULL;
    AVPacket packet;
    char *stream_filename = NULL;
    char image_filename[64];
    int c;
    int num_frames = 1;				/* default one frame */
    int64_t frame_time = 0;				/* default the first frame */
    int image_format = OUTPUT_IMAGE_YUV;	/* default native format */
    char *prefix = "frame";			/* default save image using "frame" prefix */
    int num_tries;
    unsigned long gop_duration = 0;
    int seek_last_frame = 0;
    int image_generation_done = 0;
    char *annotation_str = NULL;
    int annotation_flag = 0;
    int len;


    while ((c = getopt(argc, argv, "adhi:n:p:t:")) != -1) {
	switch (c) {
	    case 'a':
		annotation_flag = 1;
		break;

	    case 'd':
		debug = 1;
		break;

	    case 'i':
	    {
		if (!strcmp(optarg, "yuv"))
		    image_format = OUTPUT_IMAGE_YUV;
		else if (!strcmp(optarg, "ppm"))
		    image_format = OUTPUT_IMAGE_PPM;
		else if (!strcmp(optarg, "png"))
		    image_format = OUTPUT_IMAGE_PNG;
		else if (!strcmp(optarg, "jpg"))
		    image_format = OUTPUT_IMAGE_JPG;
		break;
	    }

	    case 'n':
		num_frames = atoi(optarg);
		break;

	    case 'p':
		prefix = optarg;
		break;

	    case 't':
		frame_time = atol(optarg);
		break;

	    case '?':
		if (isprint(optopt))
		    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
		else
		    fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
		exit (1);

	    case 'h':
	    default:
		print_usage();
		exit (1);
	}
    }

    stream_filename = argv[optind];
    if (!stream_filename) {
	fprintf(stderr, "Error: No media file\n");
	exit (1);
    }

    if (annotation_flag > 0) {
	annotation_str = (char *)malloc(MAX_ANNOTATION_STRING_LEN+16);
	if (annotation_str) {
	    len = fread(annotation_str, 1, MAX_ANNOTATION_STRING_LEN, stdin);
#if 1
	    printf(">>>> len = %d\n", len);
#endif
	}
    }

    av_register_all();

    mctx = mio_init(stream_filename);
    if (!mctx) {
	fprintf(stderr, "Error: Failed to initialize media io - %s\n", stream_filename);
	exit (1); 
    }
	
    fmt_ctx = avformat_alloc_context();
    fmt_ctx->pb = mctx->context;
    fmt_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;
    fmt_ctx->iformat = mio_get_input_format(mctx);

    /*
     * Tell avformat context to start rolling
     */
    if(avformat_open_input(&fmt_ctx, "", fmt_ctx->iformat, NULL) != 0) {
	fprintf(stderr, "Error: Failed to initialize avformat context\n");
	exit (1);
    }

    /*
     * Retrieve stream information
     */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
	fprintf(stderr, "Error: Failed to get media info\n");
	exit (1);
    }

#if 1
    /*
     * Dump information about file onto standard error
     */
    av_dump_format(fmt_ctx, 0, mctx->filename, 0);
#endif

    /*
     *  Find the first video stream
     */
    program = -1;
    for (i = 0; i < (int)fmt_ctx->nb_streams; i++) {
	if (fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
	    program = i;
	    break;
	}
    }

    if (program == -1) {
	fprintf(stderr, "Error: Failed to find video program in the media stream\n");
	exit (1);
    }
  
    /*
     * Process codec information
     */
    dec_codec_ctx = fmt_ctx->streams[program]->codec;
    dec_codec = avcodec_find_decoder(dec_codec_ctx->codec_id);
    if (dec_codec == NULL) {
	fprintf(stderr, "Error: Unsupported codec\n");
	exit (1);
    }

    /*
     * Initialize decoder
     */
    if (avcodec_open2(dec_codec_ctx, dec_codec, NULL) < 0) {
	fprintf(stderr, "Error: Couldn't open codec for decode\n");
	exit (1);
    }
  
#if 0
    d_printf("##### codec->name = %s\n", dec_codec->name);
    d_printf("##### codec->width = %d\n", dec_codec_ctx->width);
    d_printf("##### codec->height = %d\n", dec_codec_ctx->height);
    d_printf("\n");

    d_printf("##### bit_rate = %d\n", fmt_ctx->bit_rate);
    d_printf("##### start_time = %lld\n", (long long)fmt_ctx->start_time);
    d_printf("##### start_time_realtime = %lld\n", (long long)fmt_ctx->start_time_realtime);
    d_printf("##### duration = %lld\n", (long long)fmt_ctx->duration);
    d_printf("\n");

    d_printf("##### time_base.den = %d\n", fmt_ctx->streams[program]->time_base.den);
    d_printf("##### time_base.num = %d\n", fmt_ctx->streams[program]->time_base.num);


    d_printf("##### start_time = %lld\n", (long long)fmt_ctx->streams[program]->start_time);
    d_printf("##### duration = %lld\n", (long long)fmt_ctx->streams[program]->duration);
    d_printf("##### nb_frames = %lld\n", (long long)fmt_ctx->streams[program]->nb_frames);

    d_printf("##### avg_frame_rate.den = %d\n", fmt_ctx->streams[program]->avg_frame_rate.den);
    d_printf("##### avg_frame_rate.num = %d\n", fmt_ctx->streams[program]->avg_frame_rate.num);

    d_printf("##### codec->qmin = %d\n", dec_codec_ctx->qmin);
    d_printf("##### codec->qmax = %d\n", dec_codec_ctx->qmax);
    d_printf("##### codec->time_base.den = %d\n", dec_codec_ctx->time_base.den);
    d_printf("##### codec->time_base.num = %d\n", dec_codec_ctx->time_base.num);
    d_printf("##### codec->gop_size = %d\n", dec_codec_ctx->gop_size);
#endif


    /*
     *  AVFrame(YUV) for video decode
     */
    decode_frame = av_frame_alloc();
    if (decode_frame == NULL) {
	fprintf(stderr, "Error: Couldn't allocate AVFrame for decode\n");
	exit (1);
    }
  
    switch (image_format) {
	case OUTPUT_IMAGE_YUV:
	    pixel_format = PIX_FMT_YUVJ420P;
	break;

	case OUTPUT_IMAGE_PPM:
	    pixel_format = PIX_FMT_RGB24;
	break;

	case OUTPUT_IMAGE_PNG:
	    codec_id = AV_CODEC_ID_PNG;
	    pixel_format = PIX_FMT_RGB24;
	break;

	case OUTPUT_IMAGE_JPG:
	    codec_id = AV_CODEC_ID_MJPEG;
	    pixel_format = PIX_FMT_YUVJ420P;
	break;

	default:
	    break;
    }


    /*
     * Initialize scaler for image conversion if needed
     */
    if (image_format == OUTPUT_IMAGE_PPM || image_format == OUTPUT_IMAGE_PNG) {
	/*
	 *  AVFrame for video image output
	 */
	output_frame = av_frame_alloc();
	if (output_frame == NULL) {
	    fprintf(stderr, "Error: Couldn't allocate AVFrame for output\n");
	    exit (1);
	}

	/*
	 * Initialize picture buffers for picture output
	 */
	res = av_image_alloc(output_frame->data, output_frame->linesize, dec_codec_ctx->width,
			     dec_codec_ctx->height, pixel_format, 1);
	if (res < 0) {
  	    fprintf(stderr, "Error: Couldn't allocate output frame\n");
	    exit (1);
	}
	output_frame->format = pixel_format;
	output_frame->width = dec_codec_ctx->width;
	output_frame->height = dec_codec_ctx->height;

	/*
	 * Initialize SWS context for software scaling
	 */
	sws_ctx = sws_getContext(dec_codec_ctx->width, dec_codec_ctx->height, dec_codec_ctx->pix_fmt,
				 dec_codec_ctx->width, dec_codec_ctx->height, pixel_format, SWS_BILINEAR,
				 NULL, NULL, NULL);
    }


    /*
     * Initialize encoder for image generation if needed
     */
    if (image_format == OUTPUT_IMAGE_PNG || image_format == OUTPUT_IMAGE_JPG) {
	enc_codec = avcodec_find_encoder(codec_id);
	if (enc_codec == NULL) {
	    fprintf(stderr, "Error: Unsupported encoder codec\n");
	    exit (1);
	}

	enc_codec_ctx = avcodec_alloc_context3(enc_codec);
	if (enc_codec_ctx == NULL) {
	    fprintf(stderr, "Error: Failed to allocate encoder codec context\n");
	    exit (1);
	}

	enc_codec_ctx->pix_fmt	= pixel_format;
	enc_codec_ctx->bit_rate = dec_codec_ctx->bit_rate;
	enc_codec_ctx->width 	= dec_codec_ctx->width; 
	enc_codec_ctx->height 	= dec_codec_ctx->height; 

	if (image_format == OUTPUT_IMAGE_JPG) {
	    enc_codec_ctx->mb_lmin 	= enc_codec_ctx->qmin * FF_QP2LAMBDA;
	    enc_codec_ctx->lmin 	= enc_codec_ctx->qmin * FF_QP2LAMBDA;
	    enc_codec_ctx->mb_lmax 	= enc_codec_ctx->qmax * FF_QP2LAMBDA;
	    enc_codec_ctx->lmax 	= enc_codec_ctx->qmax * FF_QP2LAMBDA; 
	    enc_codec_ctx->flags 	= CODEC_FLAG_QSCALE; 
	    enc_codec_ctx->global_quality = enc_codec_ctx->qmin * FF_QP2LAMBDA; 
	    enc_codec_ctx->time_base = (AVRational){1,25};
	}

	if (avcodec_open2(enc_codec_ctx, enc_codec, NULL) < 0) {
	    fprintf(stderr, "Error: Failed to open codec for encode\n");
	    exit(1);
	}
    }


    gop_duration = av_rescale(dec_codec_ctx->gop_size, fmt_ctx->streams[program]->avg_frame_rate.den*1000, fmt_ctx->streams[program]->avg_frame_rate.num);
    if (frame_time > 0 && frame_time * 1000 > fmt_ctx->duration) {
	frame_time  = fmt_ctx->duration / 1000;
	seek_last_frame = 1;
    } else {
	frame_time -= gop_duration;
	if (frame_time < 0)
	    frame_time = 0;
	seek_last_frame = 0;
    }

    image_generation_done = 0;
    for (num_tries = 0; num_tries < 3 && !image_generation_done; num_tries++) {
	/*
	 * Seek to the closest I-frame location
	 *
	 * Note: timestamp = frame_time (in millisecond) * 90 (90K time base)
	 */
	if (seek_last_frame)
	    frame_time -= gop_duration;

#if 1
	res = avformat_seek_file(fmt_ctx, program, 
				 fmt_ctx->streams[program]->start_time,
				 fmt_ctx->streams[program]->start_time + frame_time*90,
				 INT64_MAX,
				 AVSEEK_FLAG_ANY);
#else
	res = av_seek_frame(fmt_ctx, program, 
				 fmt_ctx->streams[program]->start_time + frame_time*90,
				 AVSEEK_FLAG_ANY);
#endif

	if (res < 0) {
	    if (num_tries < 3)
		continue;

	    fprintf(stderr, "Error: Failed in seeking media file\n");
	    exit(1);
	}

#ifdef DEBUG	
	d_printf("##### Seeking to frame location at %ld\n", fmt_ctx->streams[program]->start_time + frame_time*90);
#endif

	/*
	 * Decode video and process picture frames
	 */
	i = 0;
	while (i < num_frames) {
	    if (av_read_frame(fmt_ctx, &packet) < 0)
		break;

	    if (packet.stream_index != program) {
		av_free_packet(&packet);
		continue;
	    }

#if 0
	    d_printf("##### PTS = %llu, %d\n", (long long)packet.pts, AV_TIME_BASE);
	    d_printf("##### DTS = %llu, %d\n", (long long)packet.dts, AV_TIME_BASE);
	    d_printf("##### duration = %lld\n", (long long)packet.duration);
#endif

	    res = -1;
	    avcodec_decode_video2(dec_codec_ctx, decode_frame, &frame_decode_done, &packet);
	    if (frame_decode_done) {
		switch (image_format) {
		    case OUTPUT_IMAGE_YUV:
			sprintf(image_filename, "%s%d.yuv", prefix, ++i);
			if (annotation_flag > 0 && annotation_str)
			    d_printf("Warning: Annotation on yuv output is not currently supported.\n");

			res = generate_raw_image(dec_codec_ctx, decode_frame, image_filename);
			break;

		    case OUTPUT_IMAGE_PPM:
			sprintf(image_filename, "%s%d.ppm", prefix, ++i);
			if (annotation_flag > 0 && annotation_str)
			    res = generate_image_with_annotation(dec_codec_ctx, decode_frame, image_format,
								 image_filename, annotation_str);
			else {
			    /*
			     * Convert the image from its native format to RGB
			     */
			    sws_scale(sws_ctx, (uint8_t const * const *)decode_frame->data,
				      decode_frame->linesize, 0, dec_codec_ctx->height,
				      output_frame->data, output_frame->linesize);

			    res = generate_ppm_image(dec_codec_ctx, output_frame, image_filename);
			}
			break;

		    case OUTPUT_IMAGE_PNG:
			sprintf(image_filename, "%s%d.png", prefix, ++i);
			if (annotation_flag > 0 && annotation_str)
			    res = generate_image_with_annotation(dec_codec_ctx, decode_frame, image_format,
								 image_filename, annotation_str);
			else {
			    /*
			     * Convert the image from its native format to RGB
			     */
			    sws_scale(sws_ctx, (uint8_t const * const *)decode_frame->data,
				      decode_frame->linesize, 0, dec_codec_ctx->height,
				      output_frame->data, output_frame->linesize);
			    res = generate_png_image(enc_codec_ctx, output_frame, image_filename);
			}
			break;

		    case OUTPUT_IMAGE_JPG:
			sprintf(image_filename, "%s%d.jpg", prefix, ++i);
			if (annotation_flag > 0 && annotation_str)
			    res = generate_image_with_annotation(dec_codec_ctx, decode_frame, image_format,
								 image_filename, annotation_str);
			else
			    res = generate_jpg_image(enc_codec_ctx, decode_frame, image_filename);
			break;

		    default:
			break;
		}

		if (res < 0) {
		    av_free_packet(&packet);
		    break;
		}

		if (res == 0) {
		    int64_t position;

		    position = av_rescale_q(decode_frame->pkt_pts, fmt_ctx->streams[program]->time_base, AV_TIME_BASE_Q) - fmt_ctx->start_time;
		    printf("%s %dms\n", image_filename, (int)(position / 1000));
		    image_generation_done = 1;
		}
	    }

	    av_free_packet(&packet);
	}
    }
  
  
    /*
     * Close the codecs
     */
    if (dec_codec_ctx)
	avcodec_close(dec_codec_ctx);
    if (enc_codec_ctx)
	avcodec_close(enc_codec_ctx);

    av_frame_free(&output_frame);
    av_frame_free(&decode_frame);

    /*
     * Stop avformat input
     */
    avformat_close_input(&fmt_ctx);

    mio_destroy(mctx);
  
    return 0;
}
