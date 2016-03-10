
/***********************************************************************
 * Copyright 2016 Sensity Systems Inc. 
 ***********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "mnannotate.h"

#define d_printf(fmt, args...)    if (debug) fprintf(stderr, fmt, ## args)

static int debug = 0;

static int
annotation_get_op(json_object *obj, int *op) 
{
    *op = json_object_get_int(obj);
    
    return 0;
}

static int
annotation_get_roi(json_object *obj, double *roi) 
{
    json_object *item;
    int i, count;

    count = json_object_array_length(obj);
    if (count > 4)
	count = 4;
    for (i = 0; i < count; i++) {
	item = json_object_array_get_idx(obj, i);
	roi[i] = json_object_get_double(item);
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
		
	    case 'r':
		annotation_get_roi(val, annotation->roi);
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
#if 0
    char window[64];
#endif

    cvInitFont(&font, CV_FONT_HERSHEY_PLAIN, a->scale, a->scale, 0, a->bold, CV_AA);
    cvGetTextSize(a->label, &font, &text_size, &base_line);

    /*
     * Draw it on src image directly if there is no alpha blend or background color applied
     */
    if (a->argb[0] == 255 && a->fill[0] == 0) {
	cvPutText(mat, a->label, cvPoint(a->roi[0], a->roi[1] + text_size.height + base_line), &font,
		  CV_RGB(a->argb[1], a->argb[2], a->argb[3]));
	return 0;
    }

    /*
     * Get dimension of the ROI
     */
    roi.x = a->roi[0];
    roi.y = a->roi[1];
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
    sprintf(window, "src_%d", i);
    cvShowImage(window, &src_mat);
    cvWaitKey(0);
#endif

    /*
     * Create target image
     */
    dst_mat = cvCreateMat(roi.height, roi.width, CV_8UC3);
    if (!a->fill[0])
	cvZero(dst_mat);
    else
	cvSet(dst_mat, CV_RGB(a->fill[1], a->fill[2], a->fill[3]), NULL);
    if (a->argb[0] != 255)
	cvPutText(dst_mat, a->label, cvPoint(0, text_size.height + base_line), &font,
		  CV_RGB(a->argb[1], a->argb[2], a->argb[3]));

#if 0
    sprintf(window, "dst_%d", i);
    cvShowImage(window, dst_mat);
    cvWaitKey(0);
#endif

    /*
     * Alpha belend 
     */
    if (a->fill[0]) {
	alpha = (double)(a->argb[0] > a->fill[0] ? a->fill[0] : a->argb[0]) / 255;
	cvAddWeighted(&src_mat, 1 - alpha, dst_mat, alpha, 0.0, &src_mat);
    } else {
	alpha = (double)a->argb[0] / 255;
	cvAddWeighted(&src_mat, 1, dst_mat, alpha, 0.0, &src_mat);
    }

    cvReleaseMat(&dst_mat);

    if (a->argb[0] == 255)
	cvPutText(mat, a->label, cvPoint(a->roi[0], a->roi[1] + text_size.height + base_line), &font,
		  CV_RGB(a->argb[1], a->argb[2], a->argb[3]));

#if 0
    sprintf(window, "alpha_%d", i);
    cvShowImage(window, &src_mat);
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
#if 0
    char window[64];
#endif

    /*
     * Check anchor point
     */
    if (a->roi[2] <= a->roi[0] || a->roi[3] <= a->roi[1])
	return -1;

    /*
     * Draw it on src image directly if there is no alpha blend or background color applied
     */
    if (a->argb[0] == 255 && a->fill[0] == 0) {
	cvRectangle(mat, cvPoint(a->roi[0], a->roi[1]), cvPoint(a->roi[2], a->roi[3]), 
		    CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);

	return 0;
    }

    /*
     * Get dimension of the ROI
     */
    roi.x = a->roi[0] - a->bold/2;
    roi.y = a->roi[1] - a->bold/2;
    roi.width = a->roi[2] - a->roi[0] + a->bold;
    roi.height = a->roi[3] - a->roi[1] + a->bold;
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
    sprintf(window, "src_%d", i);
    cvShowImage(window, &src_mat);
    cvWaitKey(0);
#endif

    /*
     * Create target image
     */
    dst_mat = cvCreateMat(roi.height, roi.width, CV_8UC3);
    if (!a->fill[0])
	cvZero(dst_mat);
    else
	cvSet(dst_mat, CV_RGB(a->fill[1], a->fill[2], a->fill[3]), NULL);
    
    if (a->argb[0] != 255)
	cvRectangle(dst_mat, cvPoint(a->bold/2, a->bold/2), cvPoint(a->roi[2] - a->roi[0], a->roi[3] - a->roi[1]), 
		    CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);

#if 0
    sprintf(window, "dst_%d", i);
    cvShowImage(window, dst_mat);
    cvWaitKey(0);
#endif

    /*
     * Alpha belend 
     */
    if (a->fill[0]) {
	alpha = (double)(a->argb[0] > a->fill[0] ? a->fill[0] : a->argb[0]) / 255;
	cvAddWeighted(&src_mat, 1 - alpha, dst_mat, alpha, 0.0, &src_mat);
    } else {
	alpha = (double)a->argb[0] / 255;
	cvAddWeighted(&src_mat, 1, dst_mat, alpha, 0.0, &src_mat);
    }

    cvReleaseMat(&dst_mat);

    if (a->argb[0] == 255)
	cvRectangle(mat, cvPoint(a->roi[0], a->roi[1]), cvPoint(a->roi[2], a->roi[3]), 
		    CV_RGB(a->argb[1], a->argb[2], a->argb[3]), a->bold, CV_AA, 0);

#if 0
    sprintf(window, "alpha_%d", i);
    cvShowImage(window, &src_mat);
    cvWaitKey(0);
#endif

    return 0;
}


int
AnnoteImage(CvMat *mat, char *commands)
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
	     * Covert relative coordinates to pixel coordinates if needed
	     */
	    if (a.roi[0] < 1 && a.roi[1] < 1 && a.roi[2] < 1 && a.roi[3] < 1) {
		a.roi[0] *= mat->cols;
		a.roi[1] *= mat->rows;
		a.roi[2] *= mat->cols;
		a.roi[3] *= mat->rows;
	    }

	    /*
	     * Check the anchor points
	     */
	    if (a.roi[0] < 0) a.roi[0] = 0;
	    if (a.roi[0] > mat->cols) a.roi[0] = mat->cols;
	    if (a.roi[1] < 0) a.roi[1] = 0;
	    if (a.roi[1] > mat->rows) a.roi[1] = mat->rows;
	    if (a.roi[2] < 0) a.roi[2] = 0;
	    if (a.roi[2] > mat->cols) a.roi[2] = mat->cols;
	    if (a.roi[3] < 0) a.roi[3] = 0;
	    if (a.roi[3] > mat->rows) a.roi[3] = mat->rows;

	    /*
	     * Clamp the color
	     */
	    for (n = 0; n < 4; n++) {
		a.argb[n] = (a.argb[n] < 0)? 0 : a.argb[n];
		a.argb[n] = (a.argb[n] > 255)? 255 : a.argb[n];
		a.fill[n] = (a.fill[n] < 0)? 0 : a.fill[n];
		a.fill[n] = (a.fill[n] > 255)? 255 : a.fill[n];
	    }

	    d_printf("##### op      = %d\n", a.op);
	    d_printf("##### roi     = [ %lf, %lf, %lf, %lf ]\n", a.roi[0], a.roi[1], a.roi[2], a.roi[3]);
	    d_printf("##### argb    = [ %d, %d, %d, %d ]\n", a.argb[0], a.argb[1], a.argb[2], a.argb[3]);
	    d_printf("##### fill    = [ %d, %d, %d, %d ]\n", a.fill[0], a.fill[1], a.fill[2], a.fill[3]);
	    d_printf("##### label   = %s\n", a.label);
	    d_printf("##### scale   = %lf\n", a.scale);
	    d_printf("##### bold    = %d\n", a.bold);
	    d_printf("\n");

	    switch (a.op) {
		case OL_TEXT:
		    DrawText(mat, &a);
		    break;

		case OL_RECTANGLE:
		    DrawRectangle(mat, &a);
		    break;

		default:
		    break;
	    }
	}
    }

    return 0;
}

