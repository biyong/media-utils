#include <iostream>
#include <fstream>

#include "opencv2/opencv_modules.hpp"
#include "opencv2/stitching.hpp"
#include "opencv2/stitching/detail/autocalib.hpp"
#include "opencv2/stitching/detail/blenders.hpp"
#include "opencv2/stitching/detail/exposure_compensate.hpp"
#include "opencv2/stitching/detail/motion_estimators.hpp"
#include "opencv2/stitching/detail/seam_finders.hpp"
#include "opencv2/stitching/detail/util.hpp"
#include "opencv2/stitching/detail/warpers.hpp"
#include "opencv2/stitching/warpers.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/calib3d.hpp"


#include "mnstitch.hpp"

using namespace std;
using namespace cv;


// By default SurfFeaturesFinder is used.
// Uncomment the following line to use OrbFeaturesFinder.
//
// #define USE_FEATURE_TYPE_ORB        1

// By default BestOf2NearestMatcher is used.
// Uncomment the following line to use BestOf2NearestRangeMatcher
//
//#define USE_NEAREST_RANGE_MATCHER    1

// By default BundleAdjusterReproj is used.
// Uncomment the following line to use BundleAdjusterRay.
//
// #define USE_RAY_ADJUSTER            1

// By default GraphCutSeamFinder(COST_COLOR) is used.
// Uncomment the following line to use VoronoiSeamFinder.
//
// #define USE_VORONOI_SEAM_FINDER     1



#if 0
string
type2str(int type) {
	string r;

	uchar depth = type & CV_MAT_DEPTH_MASK;
	uchar chans = 1 + (type >> CV_CN_SHIFT);

	switch ( depth ) {
		case CV_8U:  r = "8U"; break;
		case CV_8S:  r = "8S"; break;
		case CV_16U: r = "16U"; break;
		case CV_16S: r = "16S"; break;
		case CV_32S: r = "32S"; break;
		case CV_32F: r = "32F"; break;
		case CV_64F: r = "64F"; break;
		default:     r = "User"; break;
	}

	r += "C";
	r += (chans+'0');

	return r;
}
#endif


void
findFeatures(const vector<Mat>& images, vector<detail::ImageFeatures>& features)
{
#ifdef USE_FEATURE_TYPE_ORB
	detail::OrbFeaturesFinder finder;
#else
	detail::SurfFeaturesFinder finder;
	//detail::SurfFeaturesFinder finder(300,3,4,3,4);
#endif

	features.resize(images.size());
	for (int i = 0; i < images.size(); ++i) {
		finder(images[i], features[i]);
		features[i].img_idx = i;
		cout << "Features in image #" << i+1 << ": " << features[i].keypoints.size() << endl;
	}

	finder.collectGarbage();
}


bool
registerImages(const vector<detail::ImageFeatures>& features, vector<detail::CameraParams>& cameras)
{
	vector<detail::MatchesInfo> pairwise_matches;
#ifdef USE_NEAREST_RANGE_MATCHER
	detail::BestOf2NearestRangeMatcher matcher;
#else
	detail::BestOf2NearestMatcher matcher(false, match_conf);
	//detail::BestOf2NearestMatcher matcher(false, match_conf, 10, 10);
#endif
	uchar refine_mask_data[] = {1, 1, 1, 0, 1, 1, 0, 0, 0};
	Mat refine_mask(3, 3, CV_8U, refine_mask_data);

	// feature matching
	matcher(features, pairwise_matches);
	matcher.collectGarbage();

	// estimate camera matrix
	detail::HomographyBasedEstimator estimator;
	estimator(features, pairwise_matches, cameras);

	for (int i = 0; i < cameras.size(); i++) {
		Mat R;
		cameras[i].R.convertTo(R, CV_32F);
		cameras[i].R = R;
		cout << "##### Initial intrinsics #" << i+1 << ": " << endl << cameras[i].K() << endl;
		cout << "##### Camera matrix #" << i+1 << " from estimator: " << endl;
		cout << "      focal = " << cameras[i].focal << endl;
		cout << "      aspect = " << cameras[i].aspect << endl;
		cout << "      ppx = " << cameras[i].ppx << endl;
		cout << "      ppy = " << cameras[i].ppy << endl;
	}

#ifdef USE_RAY_ADJUSTER
	detail::BundleAdjusterRay adjuster;
#else
	detail::BundleAdjusterReproj adjuster;
#endif
	adjuster.setConfThresh(conf_thresh);
	adjuster.setRefinementMask(refine_mask);
	if (!adjuster(features, pairwise_matches, cameras))	{
		cout << "Error: Camera parameters adjusting failed.\n";
		return false;
	}

	// horizon correction
	vector<Mat> rmats;
	detail::WaveCorrectKind wave_correct = detail::WAVE_CORRECT_HORIZ;
	for (int i = 0; i < cameras.size(); i++)
		rmats.push_back(cameras[i].R);
	waveCorrect(rmats, wave_correct);
	for (int i = 0; i < cameras.size(); i++)
		cameras[i].R = rmats[i];

	return true;
}


void
findSeams(const vector<Mat>& images, const vector<detail::CameraParams>& cameras, float seam_scale, float work_scale,
		  vector<cv::Point>& corners, vector<Size>& sizes, vector<UMat> &images_warped, vector<UMat> &masks_warped)
{
	// Preapre images masks
	vector<UMat> masks(images.size());
	for (int i = 0; i < images.size(); ++i) {
		masks[i].create(images[i].size(), CV_8U);
		masks[i].setTo(Scalar::all(255));
	}

    // Warp images and their masks
	float warped_image_scale = getFocalLengthMedian(cameras);
	double seam_work_aspect = seam_scale / work_scale;
	detail::CylindricalWarper warper(static_cast<float>(warped_image_scale * seam_work_aspect));
	for (int i = 0; i < images.size(); ++i) {
		Mat_<float> K;
		cameras[i].K().convertTo(K, CV_32F);
		float swa = (float)seam_work_aspect;
		K(0,0) *= swa;
		K(0,2) *= swa;
		K(1,1) *= swa;
		K(1,2) *= swa;

		corners[i] = warper.warp(images[i], K, cameras[i].R, INTER_LINEAR, BORDER_REFLECT, images_warped[i]);
		sizes[i] = images_warped[i].size();

		warper.warp(masks[i], K, cameras[i].R, INTER_NEAREST, BORDER_CONSTANT, masks_warped[i]);
	}

	// find seam
	vector<UMat> images_warped_f(images.size());
	for (int i = 0; i < images.size(); i++)
		images_warped[i].convertTo(images_warped_f[i], CV_32F);

#ifdef USE_VORONOI_SEAM_FINDER
	detail::VoronoiSeamFinder seam_finder;
#else
	detail::GraphCutSeamFinder seam_finder(detail::GraphCutSeamFinderBase::COST_COLOR);
	//detail::GraphCutSeamFinder seam_finder(detail::GraphCutSeamFinderBase::COST_COLOR_GRAD);
#endif

	seam_finder.find(images_warped_f, corners, masks_warped);

	images_warped_f.clear();
	masks.clear();
}


void
composePano(const vector<Mat>& images, const vector<detail::CameraParams>& cameras,
			float compose_scale, float work_scale,
			vector<Point>& corners, vector<Size>& sizes, const vector<UMat>& masks_warped,
			Mat& result, Mat& result_mask)
{
	// Compute relative scales
	double compose_work_aspect = compose_scale / work_scale;

	// Update warped image scale
	float warped_image_scale = getFocalLengthMedian(cameras);
	warped_image_scale *= static_cast<float>(compose_work_aspect);
	detail::CylindricalWarper warper(static_cast<float>(warped_image_scale));

	vector<detail::CameraParams> cams(cameras.size());
	cams[0] = cameras[0];
	cams[1] = cameras[1];

	// Update corners and sizes
	for (int i = 0; i < images.size(); i++) {
		// Update intrinsics

		cams[i].focal *= compose_work_aspect;
		cams[i].ppx *= compose_work_aspect;
		cams[i].ppy *= compose_work_aspect;

		// Update corner and size
		Size sz = images[i].size();
		if (std::abs(compose_scale - 1) > 1e-1) {
			sz.width = cvRound(images[i].size().width * compose_scale);
			sz.height = cvRound(images[i].size().height * compose_scale);
		}

		Mat K;
		cams[i].K().convertTo(K, CV_32F);
		Rect roi = warper.warpRoi(sz, K, cams[i].R);
		corners[i] = roi.tl();
		sizes[i] = roi.size();
	}

	// Use blender to compose image
	Ptr<detail::Blender> blender = detail::Blender::createDefault(detail::Blender::NO, false);
	blender->prepare(corners, sizes);
	for (int i = 0; i < images.size(); i++) {
		cout << "Compositing image #" << i+1 << endl;

		Mat img;
		if (abs(compose_scale - 1) > 1e-1)
			resize(images[i], img, Size(), compose_scale, compose_scale);
		else
			img = images[i];

		Mat K;
		cams[i].K().convertTo(K, CV_32F);

		// Warp the current image
		Mat img_warped;
		warper.warp(img, K, cams[i].R, INTER_LINEAR, BORDER_REFLECT, img_warped);

		// Warp the current image mask
		Mat mask, mask_warped;
		mask.create(img.size(), CV_8U);
		mask.setTo(Scalar::all(255));
		warper.warp(mask, K, cams[i].R, INTER_NEAREST, BORDER_CONSTANT, mask_warped);

		Mat img_warped_s;
		img_warped.convertTo(img_warped_s, CV_16S);
		img_warped.release();
		img.release();
		mask.release();

		Mat dilated_mask, seam_mask;
		dilate(masks_warped[i], dilated_mask, Mat());
		resize(dilated_mask, seam_mask, mask_warped.size());
		mask_warped = seam_mask & mask_warped;
		dilated_mask.release();
		seam_mask.release();

		// Blend the current image
		blender->feed(img_warped_s, mask_warped, corners[i]);
	}

	blender->blend(result, result_mask);
}


float
getFocalLengthMedian(const vector<detail::CameraParams>& cameras)
{
	vector<double> focals;
	for (int i = 0; i < cameras.size(); i++)
		focals.push_back(cameras[i].focal);

	sort(focals.begin(), focals.end());

	float median;
	if (focals.size() % 2 == 1)
		median = static_cast<float>(focals[focals.size() / 2]);
	else
		median = static_cast<float>(focals[focals.size() / 2 - 1] + focals[focals.size() / 2]) * 0.5f;

	return median;
}
