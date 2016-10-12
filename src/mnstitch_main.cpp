#include <iostream>
#include <fstream>

using namespace std;

#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"

using namespace cv;

#include "mnstitch.hpp"
#include "mnstitch_util.hpp"


void help();
int parseCmdArgs(int argc, char** argv);

vector<string> img_names;
string result_name = "result.jpg";
string stitching_map = "";
bool rgba = false;
bool mapgen = false;
int scaled_dim_x = 0;
int scaled_dim_y = 0;
int dim_x = 0;
int dim_y = 0;

#define ASPECT_RATIO	32.0/9.0
#define TARGET_WIDTH	3840
#define TARGET_HEIGHT	1080

Mat
load_rgba_image(char *filename, int width, int height, int scaled_width, int scaled_height)
{
    Mat img, converted;
    FILE *fp = NULL;
    char *imagedata = NULL;
    int n, framesize = width * height * 4;

    if (!width || !height) {
	cout << "Failed to load RGBA image, invalid dimension size." << endl;
	return img;
    }

    fp = fopen(filename, "rb");
    if (!fp)
	return img;

    imagedata = (char *)malloc(framesize);
    if (!imagedata)
	return img;

    n = fread(imagedata, 1, framesize, fp);
    if (n != framesize) {
	cout << "Failed to load RGBA image, invalid file size." << endl;
	return img;
    }

    img = Mat(cvSize(width, height), CV_8UC4, imagedata, Mat::AUTO_STEP);
    converted = Mat(cvSize(width, height), CV_8UC3);
    cvtColor(img, converted, CV_RGBA2BGR);

    if (scaled_width > 0 && scaled_width < width && 
	scaled_height > 0 && scaled_height < height) {
	Mat resized_img;

	cout << "RGBA image " << filename << " is loaded with scaling ... " << endl;
	resize(converted, resized_img, Size(scaled_width, scaled_height));
	return resized_img;
    } else {
	cout << "RGBA image " << filename << " is loaded without scaling ... " << endl;
	scaled_width = width;
	scaled_height = height;
	return converted;
    }
}


int
main(int argc, char* argv[])
{
    //
    // Parse command line options
    //
    int retval = parseCmdArgs(argc, argv);
    if (retval)
        return retval;

    //
    // Check if have enough images
    //
    size_t num_images = img_names.size();
    if (num_images < 2) {
        cout << "Need two images" << endl;
        return -1;
    }

    //
    // Reading input images. For now, we only deal with two images only.
    //
    cout << "Reading source images ..." << endl;
    num_images = 2;
    vector<Mat> source_images(num_images);
    for (int i = 0; i < num_images; i++) {
	source_images[i] = load_rgba_image((char *)img_names[i].c_str(), dim_x, dim_y, scaled_dim_x, scaled_dim_y);
	if (source_images[i].empty()) {
	    cout << "Can't open source image " << img_names[i] <<endl;
	    return -1;
	}
    }

    Timing time;
    int64 app_start_time = getTickCount();

    //
    // Finding features
    //
    cout << "Finding features ..." << endl;
    vector<detail::ImageFeatures> features;
    int64 t = getTickCount();
    findFeatures(source_images, features);
    time.find_features = (getTickCount() - t) / getTickFrequency();

    //
    // Register images
    //
    cout << "Registering images ..." << endl;
    vector<detail::CameraParams> cameras;
    t = getTickCount();
    registerImages(features, cameras, time);
    time.registration = (getTickCount() - t) / getTickFrequency();

    //
    // Composition
    //
    cout << "Composing full view image ..." << endl;
    t = getTickCount();
    float warped_image_scale = FocalLengthMedian(cameras);

    //float warped_image_scale = 1726.96;
    //printf(">>>>> warped_image_scale = %f\n", warped_image_scale);

    Mat result, resultMask, resultMap, croppedResult;
    result = composePano(source_images, cameras, warped_image_scale, stitching_map, mapgen, resultMask, resultMap, time);
    if (mapgen) {
	cout << "Cropping ..." << endl;
	Rect crop = rectWithinMask(resultMask);
	cout << "Cropping with aspect ratio ..." << endl;
	Rect cropRatio = AspectRectWithinMask(resultMask, ASPECT_RATIO, TARGET_WIDTH, TARGET_HEIGHT);
	if (cropRatio.width != TARGET_WIDTH || cropRatio.height != TARGET_HEIGHT) {
	    cout << "Failed to generate stitched frame with desired dimension." << endl;
	    return -1;
	}

	rectangle(result, crop, Scalar(0, 0, 255), 2);
	rectangle(result, cropRatio, Scalar(0, 255, 0), 2);

	cout << "Composing cropped frame ... " << cropRatio << endl;
	croppedResult = result(cropRatio);

#if 0
	cout << "Creating stitching map ..." << endl;
	Mat stitchingMap = Mat(resultMap, cropRatio);
	// write Mat to file
	cv::FileStorage fs("stitching.yml", cv::FileStorage::WRITE);
	fs << "StitchingMap" << stitchingMap;
#else
	if (resultMap.size().area()) {
	    cout << "Creating stitching map ..." << endl;
	    Mat stitchingMap = Mat(resultMap, cropRatio);
	    // write Mat to file
	    ofstream map_file(stitching_map.c_str(), std::ios::out | std::ios::binary);
	    if (map_file.is_open()) {
		int width = scaled_dim_x * 2;
		int height = scaled_dim_y;
		int type = stitchingMap.type();
		int n;

		map_file.write((const char *)&width, sizeof(int));
		map_file.write((const char *)&height, sizeof(int));
		map_file.write((const char *)&type, sizeof(int));
		n = cropRatio.x;
		map_file.write((const char *)&n, sizeof(int));
		n = cropRatio.y;
		map_file.write((const char *)&n, sizeof(int));
		n = cropRatio.width;
		map_file.write((const char *)&n, sizeof(int));
		n = cropRatio.height;
		map_file.write((const char *)&n, sizeof(int));

		for (int y = 0; y < cropRatio.height; y++) {
		    float *pxy = (float *)stitchingMap.ptr<uchar>(y);
		    for (int x = 0; x < cropRatio.width; x++) {
			map_file.write((const char *)pxy++, sizeof(float));
			map_file.write((const char *)pxy++, sizeof(float));
		    }
		}

		map_file.close();
	    }
	}
#endif
    } else {
	cout << "Composing frame ..." << endl;
	croppedResult = result;
    }

    cout << "##### result:     " << result.cols << "x" << result.rows << endl;
    cout << "##### resultMask: " << resultMask.cols << "x" << resultMask.rows << endl;
    cout << "##### resultMap:  " << resultMap.cols << "x" << resultMap.rows << endl;
    cout << "##### imgs:       " << source_images[0].cols << "x" << source_images[0].rows << endl;
    cout << "Raw result size: " << croppedResult.size() << endl;

    imwrite(result_name, croppedResult);
    imwrite("result_raw.jpg", result);
    imwrite("result_mask.jpg", resultMask);
    imwrite("left.jpg", source_images[0]);
    imwrite("right.jpg", source_images[1]);

    time.composing = (getTickCount() - t) / getTickFrequency();
    time.total = (getTickCount() - app_start_time) / getTickFrequency();

    //
    // Reporting performance statistics
    //
    cout << "Done!" << endl << endl;

    cout << "Finding features time: "    << time.find_features << " sec" << endl;
    cout << "Images registration time: " << time.registration  << " sec" << endl;
    cout << "   BAdjuster time: "        << time.adjuster      << " sec" << endl;
    cout << "   Matching time: "         << time.matcher       << " sec" << endl;
    cout << "Composing time: "           << time.composing     << " sec" << endl;
    cout << "   Seam search time: "      << time.find_seams    << " sec" << endl;
    cout << "   Blending time: "         << time.blending      << " sec" << endl;
    cout << "Application total time: "   << time.total         << " sec" << endl;

    return 0;
}

void help()
{
    cout <<
        "Image stitching application.\n\n"
        "  $ ./stitching img1 img2 [...imgN]\n\n"
        "Options:\n"
        "  --output <result_img>\n"
        "      The default name is 'result.jpg'.\n";
}

int parseCmdArgs(int argc, char** argv)
{
    if (argc == 1) {
        help();
        return -1;
    }

    for (int i = 1; i < argc; ++i) {
        if (string(argv[i]) == "--help" ||
            string(argv[i]) == "-h" ||
            string(argv[i]) == "/?") {
            help();
            return -1;
        } else if (string(argv[i]) == "--output") {
            result_name = argv[i + 1];
            i++;
        } else if (string(argv[i]) == "--map") {
            stitching_map = argv[i + 1];
            i++;
        } else if (string(argv[i]) == "--mapgen") {
	    mapgen = true;
        } else if (string(argv[i]) == "--raw") {
	    rgba = true;
        } else if (string(argv[i]) == "--dim") {
	    sscanf(argv[i + 1], "%dx%d", &dim_x, &dim_y);
            i++;
        } else if (string(argv[i]) == "--scaled_dim") {
	    sscanf(argv[i + 1], "%dx%d", &scaled_dim_x, &scaled_dim_y);
            i++;
        } else {
            img_names.push_back(argv[i]);
        }
    }

    return 0;
}
