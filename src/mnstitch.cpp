#include <iostream>
#include <fstream>

using namespace std;

#include "opencv2/stitching.hpp"
#include "opencv2/stitching/detail/autocalib.hpp"
#include "opencv2/stitching/detail/blenders.hpp"
#include "opencv2/stitching/detail/exposure_compensate.hpp"
#include "opencv2/stitching/detail/motion_estimators.hpp"
#include "opencv2/stitching/detail/seam_finders.hpp"
#include "opencv2/stitching/detail/util.hpp"
#include "opencv2/stitching/detail/warpers.hpp"
#include "opencv2/stitching/warpers.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/calib3d.hpp"

using namespace cv;

#include "mnstitch.hpp"


#define ENABLE_BLENDING		0
#define USE_REMAP		1


double seam_megapix = 1.0;
float conf_thresh = 1.0f;
float match_conf = 0.3f;
detail::WaveCorrectKind wave_correct = detail::WAVE_CORRECT_HORIZ;
float blend_strength = 5;


# if 0
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
findFeatures(const vector<Mat>& imgs, vector<detail::ImageFeatures>& features)
{
    detail::OrbFeaturesFinder finder;
    //detail::SurfFeaturesFinder finder(1000,3,4,3,4);

    features.resize(imgs.size());
    for (size_t i = 0; i < imgs.size(); ++i) {
        finder(imgs[i], features[i]);
        features[i].img_idx = i;
    }

    finder.collectGarbage();
}


void
registerImages(const vector<detail::ImageFeatures>& features, vector<detail::CameraParams>& cameras, Timing& time)
{
    vector<detail::MatchesInfo> pairwise_matches;
    detail::BestOf2NearestMatcher matcher(false, match_conf);
    detail::BundleAdjusterRay adjuster;
    adjuster.setConfThresh(conf_thresh);
    //uchar refine_mask_data[] = {1, 1, 1, 0, 1, 1, 0, 0, 0};
    uchar refine_mask_data[] = {1, 1, 1, 0, 1, 0, 0, 0, 0};
    Mat refine_mask(3, 3, CV_8U, refine_mask_data);

    // feature matching
    int64 t = getTickCount();
    matcher(features, pairwise_matches);
    time.matcher = (getTickCount() - t) / getTickFrequency();

    matcher.collectGarbage();

    detail::HomographyBasedEstimator estimator;
    estimator(features, pairwise_matches, cameras);

    for (size_t i = 0; i < cameras.size(); ++i) {
        Mat R;

#if 1 // hardcodes with camera parameters from preliminary imx274 camera clibration result
	std::cout << "##### camera matrix[" << i << "] from estimator: " << cameras[i].K() << std::endl;
	cameras[i].focal = 1317.52;
	cameras[i].aspect = 1;
	cameras[i].ppx = 1437;
	cameras[i].ppy = 729;
	cout << "+++++ focal = " << cameras[i].focal << endl;
	cout << "+++++ aspect = " << cameras[i].aspect << endl;
	cout << "+++++ ppx = " << cameras[i].ppx << endl;
	cout << "+++++ ppy = " << cameras[i].ppy << endl;
	std::cout << "##### camera matrix[" << i << "] applied: " << cameras[i].K() << std::endl;
#endif
        cameras[i].R.convertTo(R, CV_32F);
        cameras[i].R = R;
    }

    // bundle adjustment
    t = getTickCount();
    adjuster.setRefinementMask(refine_mask);
    adjuster(features, pairwise_matches, cameras);
    time.adjuster = (getTickCount() - t) / getTickFrequency();

#if 1
    for (size_t i = 0; i < cameras.size(); ++i)
	std::cout << "##### camera matrix[" << i << "] adjusted: " << cameras[i].K() << std::endl;
#endif

    // horizon correction
    vector<Mat> rmats;
    for (size_t i = 0; i < cameras.size(); ++i)
        rmats.push_back(cameras[i].R);
    waveCorrect(rmats, wave_correct);
    for (size_t i = 0; i < cameras.size(); ++i)
        cameras[i].R = rmats[i];
}



void
findSeams(detail::CylindricalWarper& warper_full,
	  detail::CylindricalWarper& warper_downscaled,
	  //detail::TransverseMercatorWarper& warper_full,
	  //detail::TransverseMercatorWarper& warper_downscaled,
	  const vector<Mat>& imgs,
	  const vector<Mat>& maps,
	  const vector<detail::CameraParams>& cameras,
	  float seam_scale,
	  vector<UMat> &images_warped,
	  vector<UMat> &masks_warped,
	  vector<Mat> &maps_warped)
{
    vector<UMat> images_downscaled(imgs.size());
    vector<UMat> images_downscaled_warped(imgs.size());
    vector<UMat> masks(imgs.size());
    vector<Point> corners(imgs.size());

    detail::VoronoiSeamFinder seam_finder;

    for (size_t i = 0; i < imgs.size(); ++i)
        resize(imgs[i], images_downscaled[i], Size(), seam_scale, seam_scale);

    // Preapre images masks
    for (size_t i = 0; i < imgs.size(); ++i) {
        masks[i].create(images_downscaled[i].size(), CV_8U);
        masks[i].setTo(Scalar::all(255));
    }

    // Warp downscaled images and their masks
    Mat_<float> K;
    for (size_t i = 0; i < images_downscaled.size(); ++i) {
        cameras[i].K().convertTo(K, CV_32F);

        K(0,0) *= (float)seam_scale;
        K(0,2) *= (float)seam_scale;
        K(1,1) *= (float)seam_scale;
        K(1,2) *= (float)seam_scale;

        corners[i] = warper_downscaled.warp(images_downscaled[i], K, cameras[i].R,
                                            INTER_LINEAR, BORDER_REFLECT,
                                            images_downscaled_warped[i]);
        warper_downscaled.warp(masks[i], K, cameras[i].R, INTER_NEAREST,
                               BORDER_CONSTANT, masks_warped[i]);
    }

    vector<UMat> sizes(images_downscaled_warped.size());
    for (size_t i = 0; i < images_downscaled_warped.size(); ++i)
	images_downscaled_warped[i].convertTo(sizes[i], CV_32F);
    seam_finder.find(sizes, corners, masks_warped);

    // upscale to the original resolution
    Mat dilated_mask;
    for (size_t i = 0; i < masks_warped.size(); i++)
    {
        // images - warp as is
        cameras[i].K().convertTo(K, CV_32F);
        warper_full.warp(imgs[i], K, cameras[i].R, INTER_LINEAR, BORDER_REFLECT, images_warped[i]);
        //warper_full.warp(imgs[i], K, cameras[i].R, INTER_LINEAR, BORDER_CONSTANT, images_warped[i]);

	// wap the maps
	if (!maps[i].empty())
	    warper_full.warp(maps[i], K, cameras[i].R, INTER_LINEAR, BORDER_REFLECT, maps_warped[i]);

        // masks - upscale after seaming
        dilate(masks_warped[i], dilated_mask, Mat());
        resize(dilated_mask, masks_warped[i], images_warped[i].size());
    }
}


Mat
composePano(const vector<Mat>& imgs,
	    vector<detail::CameraParams>& cameras,
	    float warped_image_scale,
	    string stitching_map,
	    bool mapgen,
	    Mat& resultMask,
	    Mat& resultMap,
	    Timing& time)
{
    double seam_scale = min(1.0, sqrt(seam_megapix * 1e6 / imgs[0].size().area()));
    vector<UMat> masks_warped(imgs.size());
    vector<UMat> images_warped(imgs.size());
    vector<Mat> maps_warped(imgs.size());
    vector<Point> corners(imgs.size());
    vector<Size> sizes(imgs.size());
    vector<Mat> maps(imgs.size());

    detail::CylindricalWarper warper_full(static_cast<float>(warped_image_scale));
    detail::CylindricalWarper warper_downscaled(static_cast<float>(warped_image_scale * seam_scale));
    //detail::TransverseMercatorWarper warper_full(static_cast<float>(warped_image_scale));
    //detail::TransverseMercatorWarper warper_downscaled(static_cast<float>(warped_image_scale * seam_scale));

    //
    // Prepare remap maps
    //
    if (mapgen) {
	cout << "Prepare maps for remapping ..." << endl;    
	for (int i = 0; i < imgs.size(); i++) {
	    int offset = imgs[i].size().width * i;
	    maps[i].create(imgs[i].size(), CV_32FC3);
	    float *pxy = (float *)maps[i].data;
	    for (int y = 0; y < maps[i].size().height; y++) {
		for (int x = 0; x < maps[i].size().width; x++) {
		    *pxy++ = x + offset;
		    *pxy++ = y;
		    *pxy++ = 0;
		}
	    }
	}
    }    

    int64 t = getTickCount();
    findSeams(warper_full, warper_downscaled, imgs, maps, cameras, seam_scale, images_warped, masks_warped, maps_warped);
    time.find_seams = (getTickCount() - t) / getTickFrequency();


    t = getTickCount();

    Mat result, result_mask, map_xy;
    if (!mapgen && !stitching_map.empty()) {
	//
	// Compose using user provided map
	//

	// Create combined raw image
	Mat concatImg;
	cout << "Create combined raw image ..." << endl;
	hconcat(imgs[0], imgs[1], concatImg);

#if 0	
	// read Mat from file
	cout << "Loading stitching map ..." << endl;
	cv::FileStorage fs2("stitching.yml", FileStorage::READ);
	fs2["StitchingMap"] >> map_xy;
#else
	// read custom Mat from file
	cout << "Loading stitching map ... " << stitching_map << endl;
	ifstream map_file(stitching_map.c_str(), ios::in | ios::binary);
	if (map_file.is_open()) {
	    int width;
	    int height;
	    int type;
	    int n;
	    Rect roi;

	    map_file.read((char *)&width, sizeof(int));
	    map_file.read((char *)&height, sizeof(int));
	    map_file.read((char *)&type, sizeof(int));
	    map_file.read((char *)&n, sizeof(int));
	    roi.x = n;
	    map_file.read((char *)&n, sizeof(int));
	    roi.y = n;
	    map_file.read((char *)&n, sizeof(int));
	    roi.width = n;
	    map_file.read((char *)&n, sizeof(int));
	    roi.height = n;

	    cout << "##### Dimension: " << width << "x" << height << endl;
	    cout << "##### Roi: " << roi << endl;

	    map_xy = Mat::zeros(roi.height, roi.width, CV_32FC2);
	    map_file.read((char *)map_xy.data, map_xy.total()*map_xy.elemSize());
	}
#endif

	//
	// Stitch using remap
	//
	cout << "Remapping ..." << endl;
	//remap(concatImg, result, remap_maps[0], remap_maps[1], CV_INTER_LINEAR, BORDER_CONSTANT, Scalar(0,0, 0) );
	//remap(concatImg, result, map_xy, map_y, CV_INTER_LINEAR, BORDER_CONSTANT, Scalar(0,0, 0) );
	remap(concatImg, result, map_xy, Mat(), CV_INTER_LINEAR, BORDER_CONSTANT, Scalar(0,0, 0) );
    } else {
	//
	// Compose using blender
	//

	// Update corners and sizes
	for (size_t i = 0; i < cameras.size(); ++i) {
	    Mat K;

	    cameras[i].K().convertTo(K, CV_32F);
	    Rect roi = warper_full.warpRoi(imgs[i].size(), K, cameras[i].R);
	    corners[i] = roi.tl();
	    sizes[i] = roi.size();
	}
	Size result_size = detail::resultRoi(corners, sizes).size();
	float blend_width = sqrt(static_cast<float>(result_size.area())) * blend_strength / 100.f;

	if (mapgen) {
	    //
	    // Create stitching map
	    //
	    Ptr<detail::Blender> blender;
	    blender = detail::Blender::createDefault(detail::Blender::NO, false);

	    Rect destRoi = detail::resultRoi(corners, sizes);
	    blender->prepare(destRoi);

	    Mat map_warped_s;
	    for (size_t img_idx = 0; img_idx < imgs.size(); ++img_idx) {
		maps_warped[img_idx].convertTo(map_warped_s, CV_16S);
		blender->feed(map_warped_s, masks_warped[img_idx], corners[img_idx]);
	    }

	    blender->blend(result, result_mask);

	    Mat result_f;
	    Mat remap_maps[3];
	    Mat map_x, map_y;
	    cout << "Create x,y map ..." << endl;

	    result.convertTo(result_f, CV_32FC3);
	    split(result_f, remap_maps);
	    merge(remap_maps, 2, map_xy);

	    // remap() with integer maps is faster
	    // cout << "Convert map ..." << endl;
	    // convertMaps(remap_maps[0], remap_maps[1], map_x, map_y, CV_16SC2);
	    resultMap = map_xy;
	}

	//
	// Stitching using blending
	//
#if ENABLE_BLENDING
	detail::MultiBandBlender blender(false, static_cast<int>(ceil(log(blend_width)/log(2.)) - 1.));
#else
	Ptr<detail::Blender> blender;
	blender = detail::Blender::createDefault(detail::Blender::NO, false);
#endif

	Rect destRoi = detail::resultRoi(corners, sizes);
#if ENABLE_BLENDING
	blender.prepare(destRoi);
#else
	blender->prepare(destRoi);
#endif

	Mat img_warped_s;
	for (size_t img_idx = 0; img_idx < imgs.size(); ++img_idx) {
#if 0
	    Mat resizedImg, resizedWarpedImg;
	    resize(imgs[img_idx], resizedImg, Size(imgs[img_idx].size().width / 4, imgs[img_idx].size().height / 4));
	    imshow("original", resizedImg);
	    resize(images_warped[img_idx], resizedWarpedImg, Size(images_warped[img_idx].size().width / 4, images_warped[img_idx].size().height / 4));
	    imshow("preblend", resizedWarpedImg);
	    waitKey(0);
#endif

	    images_warped[img_idx].convertTo(img_warped_s, CV_16S);
#if ENABLE_BLENDING
	    blender.feed(img_warped_s, masks_warped[img_idx], corners[img_idx]);
#else
	    blender->feed(img_warped_s, masks_warped[img_idx], corners[img_idx]);
#endif
	}

#if ENABLE_BLENDING
	blender.blend(result, result_mask);
#else
	blender->blend(result, result_mask);
#endif
    }

    resultMask = result_mask;
    
    time.blending = (getTickCount() - t) / getTickFrequency();

    return result;
}


float
FocalLengthMedian(vector<detail::CameraParams>& cameras)
{
    vector<double> focals;
    for (size_t i = 0; i < cameras.size(); ++i)
        focals.push_back(cameras[i].focal);

    sort(focals.begin(), focals.end());

    float median;
    if (focals.size() % 2 == 1)
        median = static_cast<float>(focals[focals.size() / 2]);
    else
        median = static_cast<float>(focals[focals.size() / 2 - 1] +
                             focals[focals.size() / 2]) * 0.5f;

    return median;
}
