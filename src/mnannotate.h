/*
 * This file is part of media-utis
 *
 * Media-utils is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Media-utils is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with media-utils; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

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
