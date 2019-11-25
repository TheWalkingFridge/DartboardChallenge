/////////////////////////////////////////////////////////////////////////////
//
// COMS30121 - face.cpp
//
/////////////////////////////////////////////////////////////////////////////

// header inclusion
#include <stdio.h>
#include <algorithm>

// Extra
#include <fstream>

#include "opencv2/objdetect/objdetect.hpp"
#include "opencv2/core/core.hpp"

// #include "opencv2/opencv.hpp"
#include <opencv2/opencv.hpp>

// #include "opencv2/highgui/highgui.hpp"
#include <opencv2/highgui.hpp>
#include <opencv2/highgui/highgui_c.h>

// #include "opencv2/imgproc/imgproc.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgproc/imgproc_c.h>

#include <iostream>
#include <stdio.h>

#include <Sobel.hpp>

using namespace std;
using namespace cv;

/** Function Headers */
vector<DartboardLocation> loadGroundTruth(String path, Mat frame, int ind);
vector<Rect> detectAndDisplay( Mat frame, Mat frame_gray, vector<DartboardLocation> groundTruth, int imageIndex);

map<int, float> calculateIOU(vector<DartboardLocation> trueFaces, vector<Rect> faces);

int getCorrectFaceCount(map<int, float> IOU, float IOUThreshold);

tuple<float, float> TPRandF1(int correctFaceCount, int groundTruthFaces, int predictedFaces);
tuple<float, float> calculatePerformance(Mat frame, Mat frame_gray, vector<DartboardLocation> groundTruth, vector<Rect> faces);

vector<DartboardLocation> calculateHoughSpace(Mat frame_gray);
vector<DartboardLocation> getFacesPoints(vector<Rect> faces);
vector<DartboardLocation> calculateEstimatedPoints(vector<DartboardLocation> facePoints, vector<DartboardLocation> houghPoints);
void displayDetections(vector<DartboardLocation> locations, Mat frame);

/** Global variables */
String input_image_path = "images/";
String output_image_path = "output/";
String face_path = "GroundTruth_Face/";
String dart_path = "GroundTruth_Dart/";

String DartboardLocation_classifier = "dartcascade/cascade.xml";
String face_classifier = "frontalface.xml";

CascadeClassifier cascade;

/** @function main */
int main( int argc, const char** argv )
{
	bool isDartboardLocation = true;

	String groundTruthPath = isDartboardLocation ? dart_path : face_path;

	// The average TPR and F1 of all images
	float overallTPR = 0;
	float overallF1  = 0;

	// Load the Strong Classifier in a structure called `Cascade'
	String cascadeName = isDartboardLocation ? DartboardLocation_classifier : face_classifier;
	if( !cascade.load( cascadeName ) ){ printf("--(!)Error loading\n"); return -1; };

	int ind = 6;
	for(int imageIndex = 0; imageIndex < 16; imageIndex++) {
		// Prepare Image by turning it into Grayscale and normalising lighting
		String name = "dart" + to_string(imageIndex) + ".jpg";
		String image_path = input_image_path + name;
		Mat frame = imread(image_path, IMREAD_COLOR);
		Mat frame_gray;
		cvtColor( frame, frame_gray, CV_BGR2GRAY );
		equalizeHist( frame_gray, frame_gray );

		// Detect Faces and Display Result
		vector<DartboardLocation> groundTruth = loadGroundTruth(groundTruthPath, frame, imageIndex);
		vector<Rect> faces = detectAndDisplay(frame, frame_gray, groundTruth, imageIndex);
		vector<DartboardLocation> facePoints = getFacesPoints(faces);

		// Calculate perfomance
		tuple<float, float> performance = calculatePerformance(frame, frame_gray, groundTruth, faces);
		float TPR = get<0>(performance);
		float F1  = get<1>(performance);
		cout << "Image: " << imageIndex << ", TPR: " << TPR << "%, F1: " << F1 << "%\n";

		vector<DartboardLocation> houghPoints = calculateHoughSpace(frame_gray);
		vector<DartboardLocation> estimatedPoints = calculateEstimatedPoints(facePoints, houghPoints);

		displayDetections(estimatedPoints, frame);

		// 4. Save Result Image
		String outputName = isDartboardLocation ? "dart_" : "face_";
		imwrite(output_image_path + outputName + name, frame );
	}
	// overallTPR /= 16;
	// overallF1  /= 16;

	// printf("Overall TRP: %f%, overall F1: %f%", overallTPR, overallF1);

	return 0;
}

void displayDetections(vector<DartboardLocation> locations, Mat frame) {
	for(int i = 0; i < locations.size(); i++){	
		DartboardLocation loc = locations[i];
		int x = loc.x - loc.width/2;
		int y = loc.y - loc.height/2;

		cv::rectangle(frame, Point(x, y), Point(x + loc.width, y + loc.height), Scalar( 255, 255, 255 ), 2);
	}
}

vector<DartboardLocation> calculateEstimatedPoints(vector<DartboardLocation> facePoints, vector<DartboardLocation> houghPoints) {
	vector<DartboardLocation> points;

	for(std::size_t houghIt=0; houghIt<houghPoints.size(); ++houghIt) {
		DartboardLocation houghPoint = houghPoints[houghIt];
		double minDistance = DBL_MAX;
		DartboardLocation bestEstimate;

		for(std::size_t faceIt=0; faceIt<facePoints.size(); ++faceIt) {
			DartboardLocation facePoint = facePoints[faceIt];

			// Calculate euclidian difference
			float dist = DartboardLocation::calculateDistance(houghPoint, facePoint);
			if(dist < minDistance) {
				minDistance = dist;
				bestEstimate = DartboardLocation::getAverageLocation(houghPoint, facePoint);
			}
		}

		cout << "Min dist: " << minDistance << "\n";
		points.insert(points.end(), bestEstimate);
	}
	return points;
}

tuple<float, float> calculatePerformance(Mat frame, Mat frame_gray, vector<DartboardLocation> groundTruth, vector<Rect> faces) {
	// cout << "Calculating IOU for " << imageIndex << "\n";
	map<int, float> IOU = calculateIOU(groundTruth, faces);

	int correctFacesCount = getCorrectFaceCount(IOU, 40.0f);
	int groundTruthFaces = IOU.size();
	int predictedFaces = faces.size();

	tuple<float, float> derivations = TPRandF1(correctFacesCount, groundTruthFaces, predictedFaces);
	float TPR = get<0>(derivations) * 100.0f;
	float F1 =  get<1>(derivations) * 100.0f;

	return {TPR, F1};
}

vector<DartboardLocation> calculateHoughSpace(Mat frame_gray) {
	// Calculating Dx and Dy
	Mat dxImage = calculateDx(frame_gray);
	Mat dyImage = calculateDy(frame_gray); 
	Mat gradientMag = calculateGradientMagnitude(dxImage, dyImage);
	Mat gradientDir = calculateGradientDirection(dxImage, dyImage);

	int rows = frame_gray.rows;
    int cols = frame_gray.cols;
	int rmax = 200;
	int magThreshold = 200; 
	int houghThreshold = 200;

	imageWrite(dxImage, "dx.jpg");
	imageWrite(dyImage, "dy.jpg");
	imageWrite(gradientMag, "gradientMag.jpg");
	imageWrite(gradientDir, "gradientDir.jpg");

	// Mat edges =  Mat(frame_gray.size(), CV_32FC1);
	// Canny(frame_gray, edges, 120, 120*3);
	// Mat e = imageWrite(edges, "Canny.jpg");

	int ***hough = calculateHough(gradientMag, gradientDir, rmax, magThreshold);
	tuple<Mat, int**> flatHoughSpace = flattenHough(hough, rows, cols, rmax);
	Mat houghImage = get<0>(flatHoughSpace);
	int** radiusVotes = get<1>(flatHoughSpace);

	vector<DartboardLocation> points = getCenterPoints(houghImage, radiusVotes, houghThreshold, rmax, rmax);

	return points;
}

vector<DartboardLocation> getFacesPoints(vector<Rect> faces) {
	vector<DartboardLocation> points;

	for(std::size_t i=0; i<faces.size(); ++i) {
		Rect rect = faces[i];

		int center_X = rect.x + rect.width / 2;
		int center_Y = rect.y + rect.height / 2;

		points.insert(points.end(), DartboardLocation(center_X, center_Y, rect.width, rect.height));
	}

	return points;
}

vector<DartboardLocation> loadGroundTruth(String path, Mat frame, int picNum) {

	vector<DartboardLocation> locations;
	String name = path + "dart" + to_string(picNum) + ".txt";
	ifstream file(name);

	if(file.is_open()) {
		String line;
		int lineNum = 0;
		while(getline(file, line)) {

			DartboardLocation location;

			stringstream stream(line);
			float input[5] =  {};

			for(int i=0; i<5; i++) {
				stream >> input[i];
			}

			location.width = input[3] * frame.cols;
			location.height = input[4] * frame.rows;
			location.x = input[1] * frame.cols;
			location.y = input[2] * frame.rows;

			locations.insert(locations.end(), location);

			lineNum++;
		}
	}
	file.close();

	return locations;
}

/** @function detectAndDisplay */
vector<Rect> detectAndDisplay( Mat frame, Mat frame_gray, vector<DartboardLocation> groundTruth, int imageIndex)
{
	vector<Rect> faces;

	// 2. Perform Viola-Jones Object Detection 
	cascade.detectMultiScale( frame_gray, faces, 1.1, 1, 0|CASCADE_SCALE_IMAGE, Size(50, 50), Size(500,500) );

	// 3. Print number of Faces found
	// cout << faces.size() << std::endl;

	// 4. Draw box around faces found
	for( int i = 0; i < faces.size(); i++ )
	{
		rectangle(frame, Point(faces[i].x, faces[i].y), Point(faces[i].x + faces[i].width, faces[i].y + faces[i].height), Scalar( 0, 255, 0 ), 2);
	}

	for(int i = 0; i < groundTruth.size(); i++){

		DartboardLocation location = groundTruth[i];
		float x = location.x - location.width / 2.0f;
		float y = location.y - location.height / 2.0f;
		
		rectangle(frame, Point(x, y), Point(x + location.width, y + location.height), Scalar( 0, 0, 255 ), 2);
	}

	return faces;

}

map<int, float> calculateIOU(vector<DartboardLocation> trueFaces, vector<Rect> faces) {

	map<int, float> facesToIou;

	for(int faceNum=0; faceNum < trueFaces.size(); faceNum++) {
		DartboardLocation trueFace = trueFaces[faceNum];

		float maxIOU = -1;

	 	for(int decNum = 0; decNum < faces.size(); decNum++) {

			Rect decFace = faces[decNum];

			int trueRight = trueFace.x + trueFace.width / 2.0;
			int trueLeft = trueFace.x - trueFace.width / 2.0;
			int trueBottom = trueFace.y + trueFace.height / 2.0;
			int trueTop = trueFace.y - trueFace.height / 2.0;
	
			int xOverlap = max(0, min(trueRight, decFace.x + decFace.width) - max(trueLeft, decFace.x));
			int yOverlap = max(0, min(trueBottom, decFace.y + decFace.height) - max(trueTop, decFace.y));

			int overlapArea = xOverlap * yOverlap;

			int unionArea = trueFace.width * trueFace.height + decFace.width * decFace.height - overlapArea;

			float IOU = (float) overlapArea / (float) unionArea;

			if(IOU > maxIOU) maxIOU = IOU;

		}

		facesToIou[faceNum] = maxIOU;

	}

	return facesToIou;
}

int getCorrectFaceCount(map<int, float> IOU, float IOUThreshold){
	int counter = 0;
	for(map<int, float>::iterator it = IOU.begin(); it != IOU.end(); it++){
		if(it->second * 100 > IOUThreshold)  
			counter++;
	}

	return counter;
}

tuple<float, float> TPRandF1(int correctFaceCount, int groundTruthFaces, int predictedFaces){
	float TP = correctFaceCount;
	float TN = 0;
	float FP = predictedFaces - correctFaceCount;
	float FN = groundTruthFaces - correctFaceCount;

	float TPR = TP + FN == 0 ? 1 : TP / (TP + FN);
	float F1 = (2.0f * TP + FP + FN) == 0 ? 1  : 2.0f * TP / (2.0f * TP + FP + FN);

	return {TPR, F1};
}

	