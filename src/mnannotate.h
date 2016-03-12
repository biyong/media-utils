
/***********************************************************************
 * Copyright 2016 Sensity Systems Inc. 
 ***********************************************************************/

#include <opencv/cv.h> 
#include <opencv2/videoio/videoio_c.h>
#include <opencv2/highgui/highgui_c.h>
#include <json-c/json.h>

#define PIXEL_FORMAT_NONE	0	/* Default none */
#define PIXEL_FORMAT_IYUV	1 	/* YUV420: e.g. mpeg decoder output */
#define PIXEL_FORMAT_UYVY	2	/* YUV422: e.g. camera output */
#define PIXEL_FORMAT_NV12	3	/* NV12:   e.g. IPU transformation output */

#define OL_LABEL		0
#define OL_RECTANGLE		1
#define OL_LINE			2
#define OL_CIRCLE		3
#define OL_ELLIPSE		4
#define OL_POLYGON		5


#define MAX_ANNOTATION_STRING_LEN	10*1024

typedef struct _point {
    double x;
    double y;
} Point;


typedef struct _annotation {
    int op;			/* Operation */
    Point *roi;			/* Region of interest */
    int np;			/* Number of points in the ROI */
    double phi[3];		/* Rotation angle, starting angle, ending angle */
    int argb[4];		/* Color for forground items, such as text, line, in ARGB format */
    int fill[4];		/* Color for background or filling in ARGB format */
    char *label;		/* Text string */
    double scale;		/* Scale ratio */
    int bold;			/* Thickness of the line of text */
} Annotation;


CvMat *LoadImageFile(const char *filename, int width, int height, int pixel_format);
CvMat *LoadImageBuffer(unsigned char *buffer, int width, int height, int pixel_format);
int AnnotateImage(CvMat *mat, char *commands);
