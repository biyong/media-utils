#ifndef _MNSTITCH_HPP_
#define _MNSTITCH_HPP_

#include "opencv2/stitching/detail/matchers.hpp"
#include "opencv2/stitching/detail/camera.hpp"

//
// Use these parameters to configure pipeline
//

struct Timing
{
    float registration;
    float adjuster;
    float matcher;
    float find_features;
    float blending;
    float find_seams;
    float composing;
    float total;
};

void findFeatures(const std::vector<cv::Mat>& imgs,
                  std::vector<cv::detail::ImageFeatures>& features);

void registerImages(const std::vector<cv::detail::ImageFeatures>& features,
                    std::vector<cv::detail::CameraParams>& cameras,
                    Timing& time);

cv::Mat composePano(const std::vector<cv::Mat>& imgs,
		    std::vector<cv::detail::CameraParams>& cameras,
		    float warped_image_scale,
		    string stitching_map,
		    bool mapgen,
		    cv::Mat& mask,
		    cv::Mat& map,
		    Timing& time);

float FocalLengthMedian(std::vector<cv::detail::CameraParams>& cameras);

#endif // _MNSTITCH_HPP_
