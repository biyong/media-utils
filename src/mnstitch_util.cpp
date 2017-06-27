// Code originally created by Kent
//
// AspectRectWithinMask() is adapted from Kent's rectWithinMask(), which intend to 
// find the largest rect in the picture, for creating the desired rect cut off with
// proper aspect ratio.

#include <iostream>
#include <fstream>
#include <string>

using namespace std;

#include "opencv2/imgproc.hpp"

using namespace cv;


bool sortX(Point a, Point b) {
	bool ret = false;
	if (a.x == a.x)
		if (b.x==b.x)
			ret = a.x < b.x;

	return ret;
}

bool sortY(Point a, Point b) {
	bool ret = false;
	if (a.y == a.y)
		if (b.y == b.y)
			ret = a.y < b.y;

	return ret;
}

// Count how many pixels on x line are in the mask between minY and maxY
int countXInMask (int x, int minY, int maxY, Mat& mask) {
	int count = 0;
	for (int y = minY; y < maxY; y++)
		if (mask.at<uint8_t>(Point(x, y)))
			count++;

	return count;
}

int countYInMask (int y, int minX, int maxX, Mat& mask) {
	int count = 0;
	for (int x = minX; x < maxX; x++)
		if (mask.at<uint8_t>(Point(x, y)))
			count++;

	return count;
}

// Find the largest non-rotated rectangle that fits withing mask.
// Assumes mask is a single connected component with no holes.
// This may not work for strange non-convex shapes. It works ok for stitched
// images.
Rect rectWithinMask (Mat& mask) {
	// Ideas:
	// http://stackoverflow.com/questions/21410449/how-do-i-crop-to-largest-interior-bounding-box-in-opencv/21479072#21479072
	// http://cgm.cs.mcgill.ca/~athens/cs507/Projects/2003/DanielSud/
	// http://d3plus.org/blog/behind-the-scenes/2014/07/08/largest-rect/

	// Find the edge of the mask. Result is vector of points.
	vector<vector<Point> > contours;
	vector<Vec4i> hierarchy;
	findContours(mask, contours, hierarchy, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);
	cout << "Found " << contours.size() << " contours" << endl;
	// Better not be more than one contour. If there is, the mask is not
	// connected.
	auto& boundary = contours[0];

	// sort contour in x/y directions
	vector<Point> cSortedX = boundary;
	sort(cSortedX.begin(), cSortedX.end(), sortX);

	vector<Point> cSortedY = boundary;
	sort(cSortedY.begin(), cSortedY.end(), sortY);

	// Track indexes into vectors.
	int minXIdx = 0;
	int maxXIdx = cSortedX.size()-1;

	int minYIdx = 0;
	int maxYIdx = cSortedY.size()-1;

	// Resulting rectangle components (initialized in while loop)
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;

	while (minXIdx < maxXIdx && minYIdx < maxYIdx) {
		// For each side of the rectangle, count how many pixels are in the mask.
		int minXCount = countXInMask(cSortedX[minXIdx].x,
									 cSortedY[minYIdx].y, cSortedY[maxYIdx].y, mask);
		int maxXCount = countXInMask(cSortedX[maxXIdx].x,
									 cSortedY[minYIdx].y, cSortedY[maxYIdx].y, mask);

		int minYCount = countYInMask(cSortedY[minYIdx].y,
									 cSortedX[minXIdx].x, cSortedX[maxXIdx].x, mask);
		int maxYCount = countYInMask(cSortedY[maxYIdx].y,
									 cSortedX[minXIdx].x, cSortedX[maxXIdx].x, mask);

		// Calculate width and height of the current rectangle.
		x = cSortedX[minXIdx].x;
		y = cSortedY[minYIdx].y;
		int xMax = cSortedX[maxXIdx].x;
		int yMax = cSortedY[maxYIdx].y;
		width = xMax - x;
		height = yMax - y;

#ifdef DEBUG_TRACE
		cout << "xMin   = " << x << endl;
		cout << "yMin   = " << y << endl;
		cout << "xMax   = " << xMax << endl;
		cout << "yMax   = " << yMax << endl;
		cout << "width  = " << width << endl;
		cout << "height = " << height << endl;
#endif
 
		// Check to see if all the sides are in the mask now.
		bool xFull = (minYCount == maxYCount && minYCount == width);
		bool yFull = (minXCount == maxXCount && minXCount == height);
		if (xFull && yFull)
			// done
			break;

		// Greedy algorithm
		// Choose the line with the worst deficit and move it toward the center.
		// Deficit will be (width-count)/width or (height-count)/height so that
		// the deficits are normalized to the side length.
		float minXDeficit = (float)(height-minXCount) / (float)height;
		float maxXDeficit = (float)(height-maxXCount) / (float)height;
		float minYDeficit = (float)(width-minYCount) / (float)width;
		float maxYDeficit = (float)(width-maxYCount) / (float)width;

#ifdef DEBUG_TRACE
		cout << "minXDeficit = " << minXDeficit << endl;
		cout << "maxXDeficit = " << maxXDeficit << endl;
		cout << "minYDeficit = " << minYDeficit << endl;
		cout << "maxYDeficit = " << maxYDeficit << endl;
#endif

		// Find the largest deficit and move that side in to the next contour point.
		if (minXDeficit > maxXDeficit &&
			minXDeficit > minYDeficit &&
			minXDeficit > maxYDeficit) {
			// minXDeficit is worst
			minXIdx++;
		} else if (maxXDeficit > minYDeficit &&
				   maxXDeficit > maxYDeficit) {
			// maxXDeficit is worst
			maxXIdx--;
		} else if (minYDeficit > maxYDeficit) {
			// minYDeficit is worst
			minYIdx++;
		} else {
			// maxYDeficit is worst.
			maxYIdx--;
		}
	}

	Rect r(x, y, width, height);
	//cout << "result=" << r << endl;
	return r;
}

// Use width/height for ratio (e.g. 16/9 = 1.77)
Rect cropToAspectRatio (Rect& in, double outRatio) {
	cout << "in: " << in << endl;
	double inRatio = (double)in.width/(double)in.height;
	double diff = outRatio - inRatio;
	Rect r = in;

	if (diff < 0.0) {
		// Too wide. Shrink to fit.
		// Want ratio = width/height. Fix height, solve for width.
		int widthDiff = in.width - (int)((double)in.height / outRatio);
		int leftShift = widthDiff/2;
		r.x += leftShift;
		r.width -= widthDiff;
	} else if (diff > 0.0) {
		// Too tall
		int heightDiff = in.height - (int)((double)in.width / outRatio);
		int topShift = heightDiff/2;
		r.y += topShift;
		r.height -= heightDiff;
	}

	cout << "out: " << r << endl;
	return r;
}

Rect aspectRectWithinMask (Mat& mask, double outRatio, int cropped_width, int cropped_height) {
	// Ideas:
	// http://stackoverflow.com/questions/21410449/how-do-i-crop-to-largest-interior-bounding-box-in-opencv/21479072#21479072
	// http://cgm.cs.mcgill.ca/~athens/cs507/Projects/2003/DanielSud/
	// http://d3plus.org/blog/behind-the-scenes/2014/07/08/largest-rect/

	// Find the edge of the mask. Result is vector of points.
	vector<vector<Point> > contours;
	vector<Vec4i> hierarchy;
	findContours(mask, contours, hierarchy, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);
	cout << "Found " << contours.size() << " contours" << endl;
	// Better not be more than one contour. If there is, the mask is not
	// connected.
	auto& boundary = contours[0];

	// sort contour in x/y directions
	vector<Point> cSortedX = boundary;
	sort(cSortedX.begin(), cSortedX.end(), sortX);

	vector<Point> cSortedY = boundary;
	sort(cSortedY.begin(), cSortedY.end(), sortY);

	// Track indexes into vectors.
	int minXIdx = 0;
	int maxXIdx = cSortedX.size()-1;

	int minYIdx = 0;
	int maxYIdx = cSortedY.size()-1;

	// Resulting rectangle components (initialized in while loop)
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;

	while (minXIdx < maxXIdx && minYIdx < maxYIdx) {
		// For each side of the rectangle, count how many pixels are in the mask.
		int minXCount = countXInMask(cSortedX[minXIdx].x,
									 cSortedY[minYIdx].y, cSortedY[maxYIdx].y, mask);
		int maxXCount = countXInMask(cSortedX[maxXIdx].x,
									 cSortedY[minYIdx].y, cSortedY[maxYIdx].y, mask);

		int minYCount = countYInMask(cSortedY[minYIdx].y,
									 cSortedX[minXIdx].x, cSortedX[maxXIdx].x, mask);
		int maxYCount = countYInMask(cSortedY[maxYIdx].y,
									 cSortedX[minXIdx].x, cSortedX[maxXIdx].x, mask);

		// Calculate width and height of the current rectangle.
		x = cSortedX[minXIdx].x;
		y = cSortedY[minYIdx].y;
		int xMax = cSortedX[maxXIdx].x;
		int yMax = cSortedY[maxYIdx].y;
		width = xMax - x;
		height = yMax - y;

#ifdef DEBUG_TRACE
		cout << "xMin   = " << x << endl;
		cout << "yMin   = " << y << endl;
		cout << "xMax   = " << xMax << endl;
		cout << "yMax   = " << yMax << endl;
		cout << "width  = " << width << endl;
		cout << "height = " << height << endl;
		cout << "minXCount = " << minXCount << endl;
		cout << "maxXCount = " << maxXCount << endl;
		cout << "minYCount = " << minYCount << endl;
		cout << "maxYCount = " << maxYCount << endl;
		cout << ">>>>> ration deficit: " << height - width / outRatio << endl;
#endif

		// Check to see if all the sides are in the mask now.
		bool xFull = (minYCount == maxYCount && minYCount == width);
		bool yFull = (minXCount == maxXCount && minXCount == height);
		if (xFull && yFull)
			// done
			break;

		// Greedy algorithm
		// Choose the line with the worst deficit and move it toward the center.
		// Deficit will be (width-count)/width or (height-count)/height so that
		// the deficits are normalized to the side length.
		float minXDeficit = (float)(height-minXCount) / (float)height;
		float maxXDeficit = (float)(height-maxXCount) / (float)height;
		float minYDeficit = (float)(width-minYCount) / (float)width;
		float maxYDeficit = (float)(width-maxYCount) / (float)width;

		//cout << "minXDeficit = " << minXDeficit << endl;
		//cout << "maxXDeficit = " << maxXDeficit << endl;
		//cout << "minYDeficit = " << minYDeficit << endl;
		//cout << "maxYDeficit = " << maxYDeficit << endl;

		// Find the largest deficit and move that side in to the next contour point.
		if (minXDeficit > maxXDeficit &&
			minXDeficit > minYDeficit &&
			minXDeficit > maxYDeficit) {
			// minXDeficit is worst
			minXIdx++;
		} else if (maxXDeficit > minYDeficit &&
				   maxXDeficit > maxYDeficit) {
			// maxXDeficit is worst
			maxXIdx--;
		} else if (minYDeficit > maxYDeficit) {
			// minYDeficit is worst
			minYIdx++;
		} else {
			// maxYDeficit is worst.
			maxYIdx--;
		}
	}

	Rect out(x + width/2, y + height/2, 0, 0);

	minXIdx = 0;
	maxXIdx = cSortedX.size()-1;

	minYIdx = 0;
	maxYIdx = cSortedY.size()-1;

	while (out.x > cSortedX[minXIdx].x &&
		   out.x + out.width < cSortedX[maxXIdx].x &&
		   out.y > cSortedY[minYIdx].y &&
		   out.y + out.height < cSortedY[maxYIdx].y &&
		   out.width < cropped_width && out.height < cropped_height) {
		float dy = 9;
		float dx = dy * outRatio;
		if (out.x - dx > 0 && out.y - dy  > 0) {
			out.x -= (int)dx;
			out.width += (int)(2 * dx);
			out.y -= (int)dy;
			out.height += (int)(2 * dy);
		} else {
			break;
		}

	}

	cout << "##### out: " << out << endl;
	return out;
}
