
/***********************************************************************
 * Copyright 2016 Sensity Systems Inc. 
 ***********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include "mnannotate.h"

#define DEBUG	1

#ifdef DEBUG
#define d_printf(fmt, args...)    if (debug) fprintf(stderr, fmt, ## args)
#else
#define d_printf(fmt, args...)
#endif

static debug = 1;

static void
print_usage(void)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: mndraw [OPTION]... [FILE]\n");
    fprintf(stderr, "Draw graph annotation on an image file.\n");
    fprintf(stderr, "  -d	turn on debug message\n");
    fprintf(stderr, "  -f       pixel format (yuv420|yuv422|nv12) for raw image input file)\n");
    fprintf(stderr, "  -o	output image file\n");
    fprintf(stderr, "  -s	dimension of the raw image input file\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:  cat annotation.jason | mndraw -f yuv420 -s 1280x720 cam0_H_yuv420p.raw -o cam0_H_yuv420p.jpg\n");
    fprintf(stderr, "\n");
}


int
main(int argc, char **argv)
{
    CvMat *image;
    char *input_file = NULL, *output_file = NULL, c;
    char *format_str = NULL;
    int pixel_format = PIXEL_FORMAT_NONE;
    char *dimension_str = NULL;
    int width = 0, height = 0;
    char *annotation_str = NULL;
    int len;


    while ((c = getopt(argc, argv, "adf:ho:s:")) != -1) {
	switch (c) {
	    case 'd':
		debug = 1;
		break;

	    case 'f':
		format_str = optarg;
		break;

	    case 'o':
		output_file = optarg;
		break;

	    case 's':
		dimension_str = optarg;
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

    input_file = argv[optind];
    if (!input_file) {
	fprintf(stderr, "Error: No input file\n");
	exit (1);
    }

    annotation_str = (char *)malloc(MAX_ANNOTATION_STRING_LEN+16);
    if (annotation_str) {
	len = fread(annotation_str, 1, MAX_ANNOTATION_STRING_LEN, stdin);
#if 0	
	printf(">>>> len = %d\n", len);
#endif
    }

    if (format_str) {
	if (!strcmp(format_str, "yuv420"))
	    pixel_format = PIXEL_FORMAT_IYUV;
	else if (!strcmp(format_str, "yuv422"))
	    pixel_format = PIXEL_FORMAT_UYVY;
	else if (!strcmp(format_str, "nv12"))
	    pixel_format = PIXEL_FORMAT_NV12;
    } 

    if (dimension_str)
	sscanf(dimension_str, "%dx%d", &width, &height);
    
    if (pixel_format != PIXEL_FORMAT_NONE && (!width || !height)) {
	fprintf(stderr, "Error: No dimension information\n");
	exit (1);
    }

    image = LoadImageFile(input_file, width, height, pixel_format);
#if 0
    cvShowImage("Sensity", image);
    c = cvWaitKey(0);
#endif

    if (annotation_str)
	AnnoteImage(image, annotation_str);

#if 0
    cvShowImage("Sensity", image);
    c = cvWaitKey(0);
#endif

    if (output_file)
	cvSaveImage(output_file, image, NULL);

    cvReleaseMat(&image);

    exit(0);
}

