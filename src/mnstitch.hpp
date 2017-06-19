#ifndef _MNSTITCH_HPP_
#define _MNSTITCH_HPP_

#include "opencv2/stitching/detail/matchers.hpp"
#include "opencv2/stitching/detail/camera.hpp"

//
// Use these parameters to stitching tuning
//
extern double work_megapix;
extern double seam_megapix;
extern double compose_megapix;
extern float conf_thresh;
extern float match_conf;


struct Timing
{
	float find_features;
	float registration;
	float find_seams;
	float compose_image;
	float generate_map;
	float total;
};

void findFeatures(const std::vector<cv::Mat>& images, std::vector<cv::detail::ImageFeatures>& features);

bool registerImages(const std::vector<cv::detail::ImageFeatures>& features, std::vector<cv::detail::CameraParams>& cameras);

void findSeams(const std::vector<cv::Mat>& images, const std::vector<cv::detail::CameraParams>& cameras,
			   float seam_scale, float work_scale,
			   std::vector<cv::Point>& corners, std::vector<cv::Size>& sizes,
			   std::vector<cv::UMat> &images_warped, std::vector<cv::UMat> &masks_warped);

void composePano(const std::vector<cv::Mat>& images, const std::vector<cv::detail::CameraParams>& cameras,
				 float compose_scale, float work_scale,
				 std::vector<cv::Point>& corners, std::vector<cv::Size>& sizes, const std::vector<cv::UMat>& masks_warped,
				 cv::Mat& result, cv::Mat& result_mask);

float getFocalLengthMedian(const std::vector<cv::detail::CameraParams>& cameras);

#endif // _MNSTITCH_HPP_
