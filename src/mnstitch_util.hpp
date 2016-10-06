#ifndef _MNSTITCH_UTIL_HPP_
#define _MNSTITCH_UTIL_HPP_

cv::Rect rectWithinMask(cv::Mat& mask);
cv::Rect AspectRectWithinMask(cv::Mat& mask, double outRatio, int cropped_width, int cropped_height);
cv::Rect cropToAspectRatio(cv::Rect& in, double outRatio);

#endif //_MNSTITCH_UTIL_HPP_
