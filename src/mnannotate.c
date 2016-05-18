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

#include <stdio.h>
#include <stdlib.h>
#include "mnannotate.h"


#define d_printf(fmt, args...)    if (debug) fprintf(stderr, fmt, ## args)

static int debug = 1;

static int
annotation_get_op(json_object *obj, int *op) 
{
    *op = json_object_get_int(obj);
    
    return 0;
}

static int
annotation_get_point(json_object *obj, Point *p) 
{
    json_object_object_foreach(obj, key, val) {
	switch (key[0]) {
	    case 'x':
		p->x = json_object_get_double(val);
		break;

	    case 'y':
		p->y = json_object_get_double(val);
		break;
    
	    default:
		break;
	}
    }
    
    return 0;
}

static int
annotation_get_roi(json_object *obj, Annotation *a) 
{
    json_object *item;
    int i, count;
    Point *p;

    count = json_object_array_length(obj);
    a->roi = (Point *)malloc(count*sizeof(Point));
    if (a->roi == NULL)
	return -1;

    for (i = 0, p = a->roi; i < count; i++, p++) {
	item = json_object_array_get_idx(obj, i);
	annotation_get_point(item, p);
    }
    a->np = count;
    
    return 0;
}

static int
annotation_get_phi(json_object *obj, double *phi) 
{
    json_object *item;
    int i, count;

    count = json_object_array_length(obj);

    if (count > 3)
	count = 3;
    for (i = 0; i < count; i++) {
	item = json_object_array_get_idx(obj, i);
	phi[i] = json_object_get_double(item);
    }

    return 0;
}

static int
annotation_get_argb(json_object *obj, int *argb) 
{
    json_object *item;
    int i, count;

    count = json_object_array_length(obj);

    if (count > 4)
	count = 4;
    for (i = 0; i < count; i++) {
	item = json_object_array_get_idx(obj, i);
	argb[i] = json_object_get_int(item);
    }

    return 0;
}

static int
annotation_get_fill(json_object *obj, int *fill) 
{
    json_object *item;
    int i, count;

    count = json_object_array_length(obj);

    if (count > 4)
	count = 4;
    for (i = 0; i < count; i++) {
	item = json_object_array_get_idx(obj, i);
	fill[i] = json_object_get_int(item);
    }

    return 0;
}

static int
annotation_get_scale(json_object *obj, double *scale) 
{
    *scale = json_object_get_double(obj);
    
    return 0;
}

static int
annotation_get_bold(json_object *obj, int *bold) 
{
    *bold = json_object_get_int(obj);
    
    return 0;
}

static int
annotation_get_label(json_object *obj, char **label) 
{
    *label = (char *)json_object_get_string(obj);
    
    return 0;
}


static int
parse_annotation(json_object *obj, Annotation *annotation)
{
    int op_seen = 0;

    if (!annotation)
	return -1;

    memset(annotation, 0, sizeof(Annotation));
    json_object_object_foreach(obj, key, val) {
	switch (key[0]) {
	    case 'a':
		annotation_get_argb(val, annotation->argb);
		break;

	    case 'b':
		annotation_get_bold(val, &annotation->bold);
		break;

	    case 'f':
		annotation_get_fill(val, annotation->fill);
		break;

	    case 'l':
		annotation_get_label(val, &annotation->label);
		break;

	    case 'o':
		if (op_seen)
		    return 0;

		annotation_get_op(val, &annotation->op);
		op_seen = 1;
		break;
		
	    case 'p':
		annotation_get_phi(val, annotation->phi);
		break;

	    case 'r':
		annotation_get_roi(val, annotation);
		break;

	    case 's':
		annotation_get_scale(val, &annotation->scale);
		break; 

	    default:
		break;
	}
    }

    return 0;
}


CvMat *
LoadImageFile(const char *filename, int width, int height, int pixel_format)
{
    FILE *file;
    CvMat *src_mat, *dst_mat;
    int n;

    if (pixel_format != PIXEL_FORMAT_IYUV &&
	pixel_format != PIXEL_FORMAT_UYVY &&
	pixel_format != PIXEL_FORMAT_NV12)
	return cvLoadImageM(filename, CV_LOAD_IMAGE_COLOR);

    file = fopen(filename , "r");
    if (!file) {
	printf("Error: failed to open image file %s\n", filename);
	return NULL;
    }

    switch (pixel_format) {
	case PIXEL_FORMAT_IYUV:
	    src_mat = cvCreateMat(height+height/2, width, CV_8UC1);
	    if (!src_mat)
		return NULL;
	    dst_mat = cvCreateMat(height, width, CV_8UC3); 
	    if (!dst_mat) {
		cvReleaseMat(&src_mat);
		return NULL;
	    }
	    n = fread(src_mat->data.ptr, 1, width*(height+height/2), file);
	    if (n != width*(height+height/2)) {
		cvReleaseMat(&src_mat);
		cvReleaseMat(&dst_mat);
		return NULL;
	    }
	    cvCvtColor(src_mat, dst_mat, CV_YUV2BGR_IYUV);
	    cvReleaseMat(&src_mat);
	    return dst_mat;

	case PIXEL_FORMAT_UYVY:
	    src_mat = cvCreateMat(height, width, CV_8UC2);
	    if (!src_mat)
		return NULL;
	    dst_mat = cvCreateMat(height, width, CV_8UC3); 
	    if (!dst_mat) {
		cvReleaseMat(&src_mat);
		return NULL;
	    }
	    n = fread(src_mat->data.ptr, 1, width*height*2, file);
	    if (n != width*height*2) {
		cvReleaseMat(&src_mat);
		cvReleaseMat(&dst_mat);
		return NULL;
	    }
	    cvCvtColor(src_mat, dst_mat, CV_YUV2BGR_UYVY);
	    cvReleaseMat(&src_mat);
	    return dst_mat;

	case PIXEL_FORMAT_NV12:
	    src_mat = cvCreateMat(height+height/2, width, CV_8UC1);
	    if (!src_mat)
		return NULL;
	    dst_mat = cvCreateMat(height, width, CV_8UC3); 
	    if (!dst_mat) {
		cvReleaseMat(&src_mat);
		return NULL;
	    }
	    n = fread(src_mat->data.ptr, 1, width*(height+height/2), file);
	    if (n != width*(height+height/2)) {
		cvReleaseMat(&src_mat);
		cvReleaseMat(&dst_mat);
		return NULL;
	    }
	    cvCvtColor(src_mat, dst_mat, CV_YUV2BGR_NV12);
	    cvReleaseMat(&src_mat);
	    return dst_mat;

	case PIXEL_FORMAT_NONE:
	default:
	    break;
    }

    return NULL;
}


CvMat *
LoadImageBuffer(unsigned char *buffer, int width, int height, int pixel_format)
{
    CvMat src_mat, *dst_mat;


    if (!buffer)
	return NULL;

    if (pixel_format != PIXEL_FORMAT_IYUV &&
	pixel_format != PIXEL_FORMAT_UYVY &&
	pixel_format != PIXEL_FORMAT_NV12) {
	src_mat = cvMat(height, width, CV_32FC3, (void *)buffer);
	return cvDecodeImageM(&src_mat, CV_LOAD_IMAGE_COLOR);
    }

    dst_mat = cvCreateMat(height, width, CV_8UC3); 
    if (!dst_mat)
	return NULL;

    switch (pixel_format) {
	case PIXEL_FORMAT_IYUV:
	    src_mat = cvMat(height+height/2, width, CV_8UC1, (void *)buffer);
	    cvCvtColor(&src_mat, dst_mat, CV_YUV2BGR_IYUV);
	    return dst_mat;

	case PIXEL_FORMAT_UYVY:
	    src_mat = cvMat(height, width, CV_8UC2, (void *)buffer);
	    cvCvtColor(&src_mat, dst_mat, CV_YUV2BGR_UYVY);
	    return dst_mat;

	case PIXEL_FORMAT_NV12:
	    src_mat = cvMat(height+height/2, width, CV_8UC1, (void *)buffer);
	    cvCvtColor(&src_mat, dst_mat, CV_YUV2BGR_NV12);
	    return dst_mat;

	case PIXEL_FORMAT_NONE:
	default:
	    break;
    }

    if (dst_mat)
	cvReleaseMat(&dst_mat);

    return NULL;
}


static int
DrawText(CvMat *mat, Annotation *a)
{
    double alpha = 0;
    CvRect roi;
    CvFont font;
    CvMat src_mat, *dst_mat;
    int base_line = 0;
    CvSize text_size;


    cvInitFont(&font, CV_FONT_HERSHEY_PLAIN, a->scale, a->scale, 0, a->bold, CV_AA);
    cvGetTextSize(a->label, &font, &text_size, &base_line);

    /*
     * Draw it on src image directly if there is no alpha blend or background color applied
     */
    if (a->argb[0] == 255 && a->fill[0] == 0) {
	cvPutText(mat, a->label, cvPoint(a->roi[0].x, a->roi[0].y + text_size.height + base_line), &font,
		  CV_RGB(a->argb[1], a->argb[2], a->argb[3]));
	return 0;
    }

    /*
     * Get dimension of the ROI
     */
    roi.x = a->roi[0].x;
    roi.y = a->roi[0].y;
    roi.width = text_size.width;
    roi.height = text_size.height + 2*base_line;
    if (roi.x + roi.width > mat->cols)
	roi.width -= roi.x +roi.width - mat->cols;
    if (roi.y + roi.height > mat->rows)
	roi.width -= roi.y +roi.height - mat->rows;

    /*
     * Create source image
     */
    cvGetSubRect(mat, &src_mat, roi);

#if 0
    printf("##### subrect width = %d, subrec height = %d\n", src_mat.cols, src_mat.rows);
    cvShowImage("src", &src_mat);
    cvWaitKey(0);
#endif

    /*
     * Create target image
     */
    dst_mat = cvCloneMat(&src_mat);
    if (a->fill[0]) {
	/*
	 * Alpha belend background first
	 */
	cvSet(dst_mat, CV_RGB(a->fill[1], a->fill[2], a->fill[3]), NULL);
	alpha = (double)a->fill[0] / 255;
	cvAddWeighted(&src_mat, 1 - alpha, dst_mat, alpha, 0.0, &src_mat);
    }

#if 0
    cvShowImage("src_bg", &src_mat);
    cvWaitKey(0);
#endif

    /*
     * Draw and alpha belend foreground objects
     */
    if (a->argb[0] != 255) {
	cvCopy(&src_mat, dst_mat, NULL);
	cvPutText(dst_mat, a->label, cvPoint(0, text_size.height + base_line), &font,
		  CV_RGB(a->argb[1], a->argb[2], a->argb[3]));
	alpha = (double)a->argb[0] / 255;
	cvAddWeighted(&src_mat, 1 - alpha, dst_mat, alpha, 0.0, &src_mat);
	cvReleaseMat(&dst_mat);
    } else {
	cvPutText(mat, a->label, cvPoint(a->roi[0].x, a->roi[0].y + text_size.height + base_line), &font,
		  CV_RGB(a->argb[1], a->argb[2], a->argb[3]));
    }

#if 0
    cvShowImage("src_bg_fg", &src_mat);
    cvWaitKey(0);
#endif

    return 0;
}


static int
DrawRectangle(CvMat *mat, Annotation *a)
{
    double alpha = 0;
    CvRect roi;
    CvMat src_mat, *dst_mat;

    /*
     * Check anchor point
     */
    if (a->roi[1].x < a->roi[0].x || a->roi[1].y < a->roi[0].y)
	return -1;

    /*
     * Draw it on src image directly if there is no alpha blend or background color applied
     */
    if (a->argb[0] == 255 && a->fill[0] == 0) {
	cvRectangle(mat, cvPoint(a->roi[0].x + a->bold/2, a->roi[0].y + a->bold/2),
		    cvPoint(a->roi[1].x - a->bold/2, a->roi[1].y - a->bold/2), 
		    CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);
	return 0;
    }

    /*
     * Get dimension of the ROI
     */
    roi.x = a->roi[0].x;
    roi.y = a->roi[0].y;
    roi.width = a->roi[1].x - a->roi[0].x;
    roi.height = a->roi[1].y - a->roi[0].y;
    if (roi.x + roi.width > mat->cols)
	roi.width -= roi.x +roi.width - mat->cols;
    if (roi.y + roi.height > mat->rows)
	roi.width -= roi.y +roi.height - mat->rows;

    /*
     * Create source image
     */
    cvGetSubRect(mat, &src_mat, roi);

#if 0
    printf("##### subrect width = %d, subrec height = %d\n", src_mat.cols, src_mat.rows);
    cvShowImage("src", &src_mat);
    cvWaitKey(0);
#endif

    /*
     * Create target image
     */
    dst_mat = cvCloneMat(&src_mat);
    if (a->fill[0]) {
	/*
	 * Alpha belend background first
	 */
	cvSet(dst_mat, CV_RGB(a->fill[1], a->fill[2], a->fill[3]), NULL);
	alpha = (double)a->fill[0] / 255;
	cvAddWeighted(&src_mat, 1 - alpha, dst_mat, alpha, 0.0, &src_mat);
    }

#if 0
    cvShowImage("src_bg", &src_mat);
    cvWaitKey(0);
#endif

    /*
     * Draw and alpha belend foreground objects
     */
    if (a->argb[0] != 255) {
	cvCopy(&src_mat, dst_mat, NULL);
	cvRectangle(dst_mat, cvPoint(a->bold/2, a->bold/2),
		    cvPoint(a->roi[1].x - a->roi[0].x - a->bold/2, a->roi[1].y - a->roi[0].y - a->bold/2), 
		    CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);
	alpha = (double)a->argb[0] / 255;
	cvAddWeighted(&src_mat, 1 - alpha, dst_mat, alpha, 0.0, &src_mat);
	cvReleaseMat(&dst_mat);
    } else {
	cvRectangle(mat, cvPoint(a->roi[0].x + a->bold/2, a->roi[0].y + a->bold/2),
		    cvPoint(a->roi[1].x - a->bold/2, a->roi[1].y - a->bold/2), 
		    CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);
    }

#if 0
    cvShowImage("alpha_0", &src_mat);
    cvWaitKey(0);
#endif

    return 0;
}


static int
DrawLine(CvMat *mat, Annotation *a)
{
    double alpha = 0;
    CvRect roi;
    CvMat src_mat, *dst_mat;


    /*
     * Check anchor point
     */
    if (a->roi[1].x < a->roi[0].x || a->roi[1].y < a->roi[0].y)
	return -1;

    /*
     * Draw it on src image directly if there is no alpha blend
     */
    if (a->argb[0] == 255) {
	cvLine(mat, cvPoint(a->roi[0].x + a->bold/2, a->roi[0].y + a->bold/2),
	       cvPoint(a->roi[1].x - a->bold/2, a->roi[1].y - a->bold/2), 
	       CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);

	return 0;
    }

    /*
     * Get dimension of the ROI
     */
    roi.x = a->roi[0].x;
    roi.y = a->roi[0].y;
    roi.width = a->roi[1].x - a->roi[0].x;
    roi.height = a->roi[1].y - a->roi[0].y;
    if (roi.x + roi.width > mat->cols)
	roi.width -= roi.x +roi.width - mat->cols;
    if (roi.y + roi.height > mat->rows)
	roi.width -= roi.y +roi.height - mat->rows;

    /*
     * Create source image
     */
    cvGetSubRect(mat, &src_mat, roi);

#if 0
    printf("##### subrect width = %d, subrec height = %d\n", src_mat.cols, src_mat.rows);
    cvShowImage("src", &src_mat);
    cvWaitKey(0);
#endif

    /*
     * Create target image
     */
    dst_mat = cvCloneMat(&src_mat);
    if (a->argb[0] != 255)
	cvLine(dst_mat, cvPoint(a->bold/2, a->bold/2), 
	       cvPoint(a->roi[1].x - a->roi[0].x -a->bold/2, a->roi[1].y - a->roi[0].y - a->bold/2), 
	       CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);

#if 0
    cvShowImage("dst", dst_mat);
    cvWaitKey(0);
#endif

    /*
     * Alpha belend 
     */
    alpha = (double)a->argb[0] / 255;
    cvAddWeighted(&src_mat, 1 - alpha, dst_mat, alpha, 0.0, &src_mat);
    cvReleaseMat(&dst_mat);

#if 0
    cvShowImage("src_fg", &src_mat);
    cvWaitKey(0);
#endif

    return 0;
}


static int
DrawEllipse(CvMat *mat, Annotation *a)
{
    double alpha = 0;
    CvRect roi;
    CvMat src_mat, *dst_mat;

    /*
     * Check axes
     */
    if (a->roi[1].x <= 0 || a->roi[1].y <= 0)
	return -1;

    /*
     * Draw it on src image directly if there is no alpha blend or background color applied
     */
    if (a->argb[0] == 255 && a->fill[0] == 0) {
	cvEllipse(mat, cvPoint(a->roi[0].x, a->roi[0].y), cvSize(a->roi[1].x - a->bold/2, a->roi[1].y - a->bold/2),
		  a->phi[0], 0, 360, CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);

	return 0;
    }

    /*
     * Get dimension of the ROI
     */
    roi.x = 0;
    roi.y = 0;
    roi.width = mat->cols;
    roi.height = mat->rows;

    /*
     * Create source image
     */
    cvGetSubRect(mat, &src_mat, roi);

#if 0
    printf("##### subrect width = %d, subrec height = %d\n", src_mat.cols, src_mat.rows);
    cvShowImage("src", &src_mat);
    cvWaitKey(0);
#endif

    /*
     * Create target image
     */
    dst_mat = cvCloneMat(mat);
    if (a->fill[0]) {
	/*
	 * Alpha belend background first
	 */
	cvEllipse(dst_mat, cvPoint(a->roi[0].x, a->roi[0].y), cvSize(a->roi[1].x, a->roi[1].y),
		  a->phi[0], 0, 360, CV_RGB(a->fill[1], a->fill[2], a->fill[3]), CV_FILLED, CV_AA, 0);
	alpha = (double)a->fill[0] / 255;
	cvAddWeighted(&src_mat, 1 - alpha, dst_mat, alpha, 0.0, &src_mat);
    }

#if 0
    cvShowImage("src_bg", &src_mat);
    cvWaitKey(0);
#endif

    /*
     * Draw and alpha belend foreground objects
     */
    if (a->argb[0] != 255) {
	cvCopy(&src_mat, dst_mat, NULL);
	cvEllipse(dst_mat, cvPoint(a->roi[0].x, a->roi[0].y), cvSize(a->roi[1].x - a->bold/2, a->roi[1].y - a->bold/2),
		  a->phi[0], 0, 360, CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);
	alpha = (double)a->argb[0] / 255;
	cvAddWeighted(&src_mat, 1 - alpha, dst_mat, alpha, 0.0, &src_mat);
	cvReleaseMat(&dst_mat);
    } else {
	cvEllipse(mat, cvPoint(a->roi[0].x, a->roi[0].y), cvSize(a->roi[1].x - a->bold/2, a->roi[1].y - a->bold/2),
		  a->phi[0], 0, 360, CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);
    }

#if 0
    cvShowImage("src_bg_fg", &src_mat);
    cvWaitKey(0);
#endif

    return 0;
}


static int
DrawPolygon(CvMat *mat, Annotation *a)
{
    int i;
    double alpha = 0;
    CvRect roi;
    CvMat src_mat, *dst_mat;
    CvPoint *pts;


    pts = (CvPoint *)malloc(a->np*sizeof(CvPoint));
    if (!pts) 
	return -1;

    for (i = 0; i < a->np; i++) {
	pts[i] = cvPoint(a->roi[i].x, a->roi[i].y);
    }

    /*
     * Draw it on src image directly if there is no alpha blend or background color applied
     */
    if (a->argb[0] == 255 && a->fill[0] == 0) {
	cvPolyLine(mat, &pts, &a->np, 1, 1, CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);

	return 0;
    }

    /*
     * Get dimension of the ROI
     */
    roi.x = 0;
    roi.y = 0;
    roi.width = mat->cols;
    roi.height = mat->rows;

    /*
     * Create source image
     */
    cvGetSubRect(mat, &src_mat, roi);

#if 0
    printf("##### subrect width = %d, subrec height = %d\n", src_mat.cols, src_mat.rows);
    cvShowImage("src", &src_mat);
    cvWaitKey(0);
#endif

    /*
     * Create target image
     */
    dst_mat = cvCloneMat(mat);
    if (a->fill[0]) {
	/*
	 * Alpha belend background first
	 */
	cvFillPoly(dst_mat, &pts, &a->np, 1, CV_RGB(a->fill[1], a->fill[2], a->fill[3]), CV_AA, 0);
	alpha = (double)a->fill[0] / 255;
	cvAddWeighted(&src_mat, 1 - alpha, dst_mat, alpha, 0.0, &src_mat);
    }

#if 0
    cvShowImage("src_bg", &src_mat);
    cvWaitKey(0);
#endif

    /*
     * Draw and alpha belend foreground objects
     */
    if (a->argb[0] != 255) {
	cvCopy(&src_mat, dst_mat, NULL);
	cvPolyLine(dst_mat, &pts, &a->np, 1, 1, CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);
	alpha = (double)a->argb[0] / 255;
	cvAddWeighted(&src_mat, 1 - alpha, dst_mat, alpha, 0.0, &src_mat);
	cvReleaseMat(&dst_mat);
    } else {
	cvPolyLine(mat, &pts, &a->np, 1, 1, CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);
    }

#if 0
    cvShowImage("src_bg_fg", &src_mat);
    cvWaitKey(0);
#endif

    return 0;
}


static int
DrawCircle(CvMat *mat, Annotation *a)
{
    double alpha = 0;
    CvRect roi;
    CvMat src_mat, *dst_mat;


    /*
     * Check radius
     */
    if (a->roi[1].x - a->roi[0].x <= 0)
	return -1;

    /*
     * Draw it on src image directly if there is no alpha blend or background color applied
     */
    if (a->argb[0] == 255 && a->fill[0] == 0) {
	cvCircle(mat, cvPoint(a->roi[0].x, a->roi[0].y), (int)(a->roi[1].x - a->roi[0].x - a->bold/2),
		 CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);

	return 0;
    }

    /*
     * Get dimension of the ROI
     */
    roi.x = 0;
    roi.y = 0;
    roi.width = mat->cols;
    roi.height = mat->rows;

    /*
     * Create source image
     */
    cvGetSubRect(mat, &src_mat, roi);

#if 0
    printf("##### subrect width = %d, subrec height = %d\n", src_mat.cols, src_mat.rows);
    cvShowImage("src", &src_mat);
    cvWaitKey(0);
#endif

    /*
     * Create target image
     */
    dst_mat = cvCloneMat(mat);
    if (a->fill[0]) {
	/*
	 * Alpha belend background first
	 */
	cvCircle(dst_mat, cvPoint(a->roi[0].x, a->roi[0].y), (int)(a->roi[1].x - a->roi[0].x),
		 CV_RGB(a->fill[1], a->fill[2], a->fill[3]), CV_FILLED, CV_AA, 0);
	alpha = (double)a->fill[0] / 255;
	cvAddWeighted(&src_mat, 1 - alpha, dst_mat, alpha, 0.0, &src_mat);
    }

#if 0
    cvShowImage("src_bg", &src_mat);
    cvWaitKey(0);
#endif

    /*
     * Draw and alpha belend foreground objects
     */
    if (a->argb[0] != 255) {
	cvCopy(&src_mat, dst_mat, NULL);
	cvCircle(dst_mat, cvPoint(a->roi[0].x, a->roi[0].y), (int)(a->roi[1].x - a->roi[0].x - a->bold/2),
		 CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);
	alpha = (double)a->argb[0] / 255;
	cvAddWeighted(&src_mat, 1 - alpha, dst_mat, alpha, 0.0, &src_mat);
	cvReleaseMat(&dst_mat);
    } else {
	cvCircle(mat, cvPoint(a->roi[0].x, a->roi[0].y), (int)(a->roi[1].x - a->roi[0].x - a->bold/2),
		 CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);
    }

#if 0
    cvShowImage("src_bg_fg", &src_mat);
    cvWaitKey(0);
#endif

    return 0;
}


int
AnnotateImage(CvMat *mat, char *commands)
{
    int i, n;
    json_object *jobj, *obj;
    json_object *annotations;
    int count;
    Annotation a;


    if (!commands) {
	fprintf(stderr, "Error: No annotation input\n");
	return -1;
    }

    d_printf("Annotation string: %s\n", commands);

    jobj = json_tokener_parse(commands);
    if (!jobj) {
	fprintf(stderr, "Error: No annotation was specified\n");
	return -1;
    }

    d_printf("JSON string: \n %s\n", json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PRETTY));

    json_object_object_get_ex(jobj, "annotations", &annotations);
    count = json_object_array_length(annotations);
    d_printf("##### Number of annotations = %d\n", count);

    for (i = 0; i < count; i++) {
	obj = json_object_array_get_idx(annotations, i);
	if (!parse_annotation(obj, &a)) {
	    /*
	     * Covert relative coordinates to pixel coordinates
	     */
	    for (n = 0; n < a.np; n++) {
		a.roi[n].x *= mat->cols;
		a.roi[n].y *= mat->rows;
		if (a.roi[n].x < 0) a.roi[n].x = 0;
		if (a.roi[n].x > mat->cols) a.roi[n].x = mat->cols;
		if (a.roi[n].y < 0) a.roi[n].y = 0;
		if (a.roi[n].y > mat->rows) a.roi[n].y = mat->rows;
	    }

	    /*
	     * Clamp the color
	     */
	    for (n = 0; n < 4; n++) {
		a.argb[n] = (a.argb[n] < 0)? 0 : a.argb[n];
		a.argb[n] = (a.argb[n] > 255)? 255 : a.argb[n];
		a.fill[n] = (a.fill[n] < 0)? 0 : a.fill[n];
		a.fill[n] = (a.fill[n] > 255)? 255 : a.fill[n];
	    }

	    d_printf("\n");
	    d_printf("##### op      = %d\n", a.op);
	    d_printf("##### roi     = [ (%lf, %lf), (%lf, %lf) ]\n", a.roi[0].x, a.roi[0].y, a.roi[1].x, a.roi[1].y);
	    d_printf("##### phi     = [ %lf, %lf, %lf ]\n", a.phi[0], a.phi[1], a.phi[2]);
	    d_printf("##### argb    = [ %d, %d, %d, %d ]\n", a.argb[0], a.argb[1], a.argb[2], a.argb[3]);
	    d_printf("##### fill    = [ %d, %d, %d, %d ]\n", a.fill[0], a.fill[1], a.fill[2], a.fill[3]);
	    d_printf("##### label   = %s\n", a.label);
	    d_printf("##### scale   = %lf\n", a.scale);
	    d_printf("##### bold    = %d\n", a.bold);
	    d_printf("\n");

	    switch (a.op) {
		case OL_LABEL:
		    DrawText(mat, &a);
		    break;

		case OL_RECTANGLE:
		    DrawRectangle(mat, &a);
		    break;

		case OL_LINE:
		    DrawLine(mat, &a);
		    break;

		case OL_ELLIPSE:
		    DrawEllipse(mat, &a);
		    break;

		case OL_CIRCLE:
		    DrawCircle(mat, &a);
		    break;

		case OL_POLYGON:
		    DrawPolygon(mat, &a);
		    break;

		default:
		    break;
	    }

	    if (a.roi)
		free(a.roi);
	}
    }

    return 0;
}

