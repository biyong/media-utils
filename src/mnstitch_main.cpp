#include <iostream>
#include <fstream>

#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"

#include "mnstitch.hpp"
#include "mnstitch_util.hpp"

using namespace std;
using namespace cv;


void help();
int parseOptions(int argc, char** argv);

vector<string> img_names;
string result_name = "result.jpg";
string stitching_map = "result.mat";
bool i420 = false;
bool mapgen = false;
int scaled_dim_x = 2880;
int scaled_dim_y = 1620;
int dim_x = 2880;
int dim_y = 1620;

double work_megapix = 0.6;
double seam_megapix = 0.1;
double compose_megapix = -1;
float conf_thresh = 0.7f;
float match_conf = 0.54f;

#define MNSTITCH_CONF	"/mfgdata/apps/mediaserver/mnstitch.conf"

#define ASPECT_RATIO	(32.0/9.0)
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
		cout << "Error: Failed to load RGBA image, invalid dimension size." << endl;
		return img;
	}

	fp = fopen(filename, "rb");
	if (!fp) {
		cout << "Error: Failed to open image file " << filename << endl;
		return img;
	}

	imagedata = (char *)malloc(framesize);
	if (!imagedata) {
		fclose(fp);
		return img;
	}

	n = fread(imagedata, 1, framesize, fp);
	if (n != framesize) {
		cout << "Error: Failed to load RGBA image, invalid file size." << endl;
		fclose(fp);
		return img;
	}

	fclose(fp);

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


Mat
load_i420_image(char *filename, int width, int height, int scaled_width, int scaled_height)
{
	Mat img, converted;
	FILE *fp = NULL;
	char *imagedata = NULL;
	int n, framesize = width * height * 3 / 2;

	if (!width || !height) {
		cout << "Error: Failed to load I420 image, invalid dimension size." << endl;
		return img;
	}

	fp = fopen(filename, "rb");
	if (!fp) {
		cout << "Error: Failed to open image file " << filename << endl;
		return img;
	}

	imagedata = (char *)malloc(framesize);
	if (!imagedata)
		return img;

	n = fread(imagedata, 1, framesize, fp);
	if (n != framesize) {
		cout << "Error: Failed to load I420 image, invalid file size." << endl;
		fclose(fp);
		return img;
	}

	fclose(fp);

	img = Mat(cvSize(width, height * 3 / 2), CV_8UC1, imagedata, Mat::AUTO_STEP);
	converted = Mat(cvSize(width, height), CV_8UC3);
	cvtColor(img, converted, CV_YUV2BGR_IYUV, 3);

	if (scaled_width > 0 && scaled_width < width &&
		scaled_height > 0 && scaled_height < height) {
		Mat resized_img;

		cout << "I420 image " << filename << " is loaded with scaling ... " << endl;
		resize(converted, resized_img, Size(scaled_width, scaled_height));
		return resized_img;
	} else {
		cout << "I420 image " << filename << " is loaded without scaling ... " << endl;
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
	if (parseOptions(argc, argv))
		exit(1);

	//
	// Load config file (if there is one)
	//
	ifstream ifs(MNSTITCH_CONF);
	for (string line; getline(ifs, line); ) {
		char entry[32];
		double value;
		sscanf(line.c_str(), "%s%lf", entry, &value);
		if (string(entry) == "work_megapix")
			work_megapix = min(1.0, value);
		else if (string(entry) == "seam_megapix")
			seam_megapix = min(1.0, value);
		else if (string(entry) == "compose_megapix")
			compose_megapix = min(1.0, value);
		else if (string(entry) == "conf_thresh")
			conf_thresh = (float)min(1.0, value);
		else if (string(entry) == "match_conf")
			match_conf = (float)min(1.0, value);
	}
	ifs.close();

	//
	// Check if have enough images
	//
	size_t num_images = img_names.size();
	if (num_images < 2) {
		cout << "Error: Need two images" << endl;
		exit(1);
	}

	//
	// Reading input images. For now, we support RGBA and I420 image format,
	// and deal with two images only.
	//
	cout << "Reading source images ..." << endl;
	num_images = 2;
	vector<Mat> source_images(num_images);
	for (int i = 0; i < num_images; i++) {
		if (i420)
			source_images[i] = load_i420_image((char *)img_names[i].c_str(), dim_x, dim_y, scaled_dim_x, scaled_dim_y);
		else
			source_images[i] = load_rgba_image((char *)img_names[i].c_str(), dim_x, dim_y, scaled_dim_x, scaled_dim_y);
		if (source_images[i].empty()) {
			cout << "Error: Can't open source image " << img_names[i] <<endl;
			exit(1);
		}
	}

	double work_scale = 1;
	double seam_scale = 1;
	double compose_scale = 1;

	Timing time;
	int64 app_start_time = getTickCount();

	//
	// Find features
	//
	cout << "Finding features ..." << endl;
	vector<Mat> images(num_images);
	vector<detail::ImageFeatures> features;

	if (work_megapix > 0)
		work_scale = min(1.0, sqrt(work_megapix * 1e6 / source_images[0].size().area()));

	for (int i = 0; i < num_images; i++)
		resize(source_images[i], images[i], Size(), work_scale, work_scale);

	int64 t = getTickCount();
	findFeatures(images, features);
	time.find_features = (getTickCount() - t) / getTickFrequency();

	if (seam_megapix > 0)
		seam_scale = min(1.0, sqrt(seam_megapix * 1e6 / source_images[0].size().area()));

	for (int i = 0; i < num_images; i++)
		resize(source_images[i], images[i], Size(), seam_scale, seam_scale);


	//
	// Register images
	//
	cout << "Registering images ..." << endl;
	vector<detail::CameraParams> cameras;

	t = getTickCount();
	if (!registerImages(features, cameras)) {
		cout << "Error: Failed to register images." << endl;
		exit(1);
	}
	time.registration = (getTickCount() - t) / getTickFrequency();


	//
	// Find seams
	//
	cout << "Finding seam and warping images ..." << endl;
	vector<Point> corners(num_images);
	vector<UMat> masks_warped(num_images);
	vector<Size> sizes(num_images);
	vector<UMat> images_warped(num_images);

	t = getTickCount();
	findSeams(images, cameras, seam_scale, work_scale, corners, sizes, images_warped, masks_warped);
	time.find_seams = (getTickCount() - t) / getTickFrequency();


	//
	// Compose stitched image
	//
	cout << "Composing full view image ..." << endl;
	Mat result, result_mask, result_cropped;

	if (compose_megapix > 0)
		compose_scale = min(1.0, sqrt(compose_megapix * 1e6 / source_images[0].size().area()));

	t = getTickCount();
	composePano(source_images, cameras, compose_scale, work_scale, corners, sizes, masks_warped, result, result_mask);
	time.compose_image = (getTickCount() - t) / getTickFrequency();


	//
	// Generate stitching map
	//
	if (mapgen) {
		vector<Mat> maps(num_images);

		cout << "Prepare maps for remapping ..." << endl;
		for (int i = 0; i < num_images; i++) {
			int offset = source_images[i].size().width * i;
			maps[i].create(source_images[i].size(), CV_32FC3);
			float *pxy = (float *)maps[i].data;
			for (int y = 0; y < maps[i].size().height; y++) {
				for (int x = 0; x < maps[i].size().width; x++) {
					*pxy++ = x + offset;
					*pxy++ = y;
					*pxy++ = 0;
				}
			}
		}

		Mat result_map, result_map_mask, result_map_cropped;

		t = getTickCount();
		composePano(maps, cameras, compose_scale, work_scale, corners, sizes, masks_warped, result_map, result_map_mask);

		cout << "Cropping ..." << endl;
		Rect crop = rectWithinMask(result_mask);

		cout << "Cropping with aspect ratio ..." << endl;
		Rect crop_ratio = aspectRectWithinMask(result_mask, ASPECT_RATIO, TARGET_WIDTH, TARGET_HEIGHT);

		cout << "Composing cropped frame ... " << crop_ratio << endl;
		result_cropped = result(crop_ratio).clone();

		rectangle(result, crop, Scalar(0, 0, 255), 2);
		rectangle(result, crop_ratio, Scalar(0, 255, 0), 2);

		cout << "Generate stitching map ..." << endl;
		result_map_cropped = result_map(crop_ratio).clone();

		// TODO:: integer map vs float map
		Mat result_f;
		Mat remap_maps[3];
		Mat map_xy;
		result_map_cropped.convertTo(result_f, CV_32FC3);
		split(result_f, remap_maps);
		merge(remap_maps, 2, map_xy);

        ofstream map_file(stitching_map.c_str(), std::ios::out | std::ios::binary);
		if (map_file.is_open()) {
			int width = scaled_dim_x > 0 ? scaled_dim_x * 2 : dim_x * 2;
			int height = scaled_dim_y > 0 ? scaled_dim_y : dim_y;
			int type = map_xy.type();
			int n;

			map_file.write((const char *)&width, sizeof(int));
			map_file.write((const char *)&height, sizeof(int));
			map_file.write((const char *)&type, sizeof(int));
			n = crop_ratio.x;
			map_file.write((const char *)&n, sizeof(int));
			n = crop_ratio.y;
			map_file.write((const char *)&n, sizeof(int));
			n = crop_ratio.width;
			map_file.write((const char *)&n, sizeof(int));
			n = crop_ratio.height;
			map_file.write((const char *)&n, sizeof(int));

			for (int y = 0; y < crop_ratio.height; y++) {
				float *pxy = (float *)map_xy.ptr<uchar>(y);
				for (int x = 0; x < crop_ratio.width; x++) {
					map_file.write((const char *)pxy++, sizeof(float));
					map_file.write((const char *)pxy++, sizeof(float));
				}
			}

			map_file.close();
		}

		time.generate_map = (getTickCount() - t) / getTickFrequency();
	}

	cout << "##### result:     " << result.cols << "x" << result.rows << endl;
	cout << "##### resultMask: " << result_mask.cols << "x" << result_mask.rows << endl;
	cout << "##### images:     " << source_images[0].cols << "x" << source_images[0].rows << endl;
	cout << "Raw result size:  " << result_cropped.size() << endl;

	imwrite(result_name, result_cropped);
	imwrite("result_raw.jpg", result);
	imwrite("result_mask.jpg", result_mask);
	imwrite("left.jpg", source_images[0]);
	imwrite("right.jpg", source_images[1]);

	time.total = (getTickCount() - app_start_time) / getTickFrequency();

	//
	// Reporting performance statistics
	//
	cout << "Done!" << endl << endl;

	cout << "##### work_megapix:     " << work_megapix << endl;
	cout << "##### seam_megapix:     " << seam_megapix << endl;
	cout << "##### compose_megapix:  " << compose_megapix << endl;
	cout << "##### conf_thresh:      " << conf_thresh << endl;
	cout << "##### match_conf:       " << match_conf << endl << endl;
	cout << "Finding features time:          " << time.find_features << " sec" << endl;
	cout << "Images registration time:       " << time.registration  << " sec" << endl;
	cout << "Finding seams and warping time: " << time.find_seams    << " sec" << endl;
	cout << "Composing time:                 " << time.compose_image << " sec" << endl;
	cout << "Map generation:                 " << time.generate_map  << " sec" << endl;
	cout << "Application total time:         " << time.total         << " sec" << endl;

	exit(0);
}


void
help()
{
	cout <<
		"Image stitching application.\n\n"
		"Usage: mnstitch [options] img1 img2 \n\n"
		"Options:\n"
		"  --help                         Print usage info\n"
		"  --dim <width x height>         Dimension of the original source image, e.g. 2880x1620\n"
		"  --scaled_dim <width x height>  Dimension of the scaled down image, e.g. 2880x1620\n"
		"  --raw                          Flag for raw I420 image format. Default image format is RGBA\n"
		"  --mapgen                       Flag for generating stiching map used in the image stitching\n"
		"  --map <filename>               Filename of sitching map, the default name is 'result.mat'\n"
		"  --output <filename>            Filename of the result image, the default name is 'result.jpg'\n\n"
		"Following options are for performance tuning and internal test:\n\n"
		"  --work_megapix <0.0~1.0>       Default is 0.6\n"
		"  --seam_megapix <0.0~1.0>       Default is 0.1\n"
		"  --compose_megapix <0.0~1.0>    Default is 0.0\n"
		"  --conf_thresh <0.0~1.0>        Default is 0.7\n"
		"  --match_conf <0.0~1.0>         Default is 0.54\n";
}


int
parseOptions(int argc, char** argv)
{
	if (argc == 1) {
		help();
		return -1;
	}

	for (int i = 1; i < argc; i++) {
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
			i420 = true;
		} else if (string(argv[i]) == "--dim") {
			sscanf(argv[i + 1], "%dx%d", &dim_x, &dim_y);
			i++;
		} else if (string(argv[i]) == "--scaled_dim") {
			sscanf(argv[i + 1], "%dx%d", &scaled_dim_x, &scaled_dim_y);
			i++;
		} else if (string(argv[i]) == "--work_megapix") {
			sscanf(argv[i + 1], "%lf", &work_megapix);
			i++;
		} else if (string(argv[i]) == "--seam_megapix") {
			sscanf(argv[i + 1], "%lf", &seam_megapix);
			i++;
		} else if (string(argv[i]) == "--compose_megapix") {
			sscanf(argv[i + 1], "%lf", &compose_megapix);
			i++;
		} else if (string(argv[i]) == "--conf_thresh") {
			sscanf(argv[i + 1], "%f", &conf_thresh);
			i++;
		} else if (string(argv[i]) == "--match_conf") {
			sscanf(argv[i + 1], "%f", &match_conf);
			i++;
		} else {
			img_names.push_back(argv[i]);
		}
	}

	return 0;
}
