#include "theialib.hpp"

#include <chrono> 
using namespace std;
using namespace chrono;

using namespace cv;
using namespace cv::xfeatures2d;

#define use_theia 1

template<typename _Tp>
std::vector<_Tp> ConvertMat2Vector(const cv::Mat &mat)
{
    return (std::vector<_Tp>)(mat.reshape(1, 1));//通道数不变，按行转为一行
}

void processWithCpu(string objectInputFile, string sceneInputFile, std::vector<float>& obj_desc, std::vector<float>& sce_desc, vector<KeyPoint> &keypoints_object, vector<KeyPoint> &keypoints_scene, int dim, int minHessian = 100)
{
	printf("CPU::Processing object: %s and scene: %s ...\n", objectInputFile.c_str(), sceneInputFile.c_str());

	// Load the image from the disk
    auto start = system_clock::now();
	Mat img_object = cv::imread( objectInputFile, cv::IMREAD_GRAYSCALE ); // surf works only with grayscale images
	Mat img_scene = cv::imread( sceneInputFile, cv::IMREAD_GRAYSCALE );

	if( !img_object.data || !img_scene.data ) {
		std::cout<< "Error reading images." << std::endl;
		return;
	}

    //static const cv::Rect roi(40, 40, 220, 220);

    //cv::Mat sample_object, sample_scene;

    //cv::resize(img_object, sample_object, cv::Size(300, 300), INTER_NEAREST);
    //cv::resize(img_scene, sample_scene, cv::Size(300, 300), INTER_NEAREST);

    //img_object = sample_object(roi);
    //img_scene = sample_scene(roi);
    //cv::resize(img_object, img_object, cv::Size(img_object.cols/2, img_object.rows/2), INTER_NEAREST);
    //cv::resize(img_scene, img_scene, cv::Size(img_scene.cols/2, img_scene.rows/2), INTER_NEAREST);
    auto end = system_clock::now();
    auto duration = duration_cast<microseconds>(end - start);
    LOG(INFO) << "cost_loadandresizeimage"
        << double(duration.count()) * microseconds::period::num / microseconds::period::den
        << " sec" << endl; 
	//vector<KeyPoint> keypoints_obj, keypoints_sce; // keypoints
	Mat descriptors_object, descriptors_scene; // descriptors (features)

	//-- Steps 1 + 2, detect the keypoints and compute descriptors, both in one method
    bool extended = (dim == 128);
	Ptr<SURF> surf = SURF::create( minHessian, 4, 3, extended);
	surf->detectAndCompute( img_object, noArray(), keypoints_object, descriptors_object );
	surf->detectAndCompute( img_scene, noArray(), keypoints_scene, descriptors_scene );
     
    std::cout << "keypoints_obj " << keypoints_object.size() << std::endl;
    std::cout << "keypoints_sce " << keypoints_scene.size() << std::endl;
    obj_desc = ConvertMat2Vector<float>(descriptors_object);
    sce_desc = ConvertMat2Vector<float>(descriptors_scene);
    
    std::cout << "obj   convert to : " << obj_desc.size() << std::endl;
    std::cout << "scene convert to : " << sce_desc.size() << std::endl;
}

int main( int argc, char** argv )
{
	//string fileId = std::to_string(1);
	string objname = "../data/CAMERA_LEFT_1226322307.261530.jpg";
    string scenename = "../data/CAMERA_LEFT_1226322120.504112.jpg";
	string objectInputFile = objname; 
	string sceneInputFile = scenename;
    const int dim = 128;
    TheiaTool theia(dim);


    std::vector<float> obj_desc, sce_desc;
    vector<KeyPoint> keypoints_object, keypoints_scene;
	processWithCpu(objectInputFile, sceneInputFile, obj_desc, sce_desc, keypoints_object, keypoints_scene, dim, 100);

#if use_theia
    auto start = system_clock::now();
    int loops = 1;
    std::vector< DMatch > good_matches;
    for(int i = 0; i != loops; ++i)
    {
        //TheiaTool::FeatType obj_feat = std::make_pair(keypoints_object, obj_desc);
        //std::vector<TheiaTool::FeatType> sce_feat = {std::make_pair(keypoints_scene, sce_desc)};
        //TheiaTool::FeatType sce_feat = std::make_pair(keypoints_scene, sce_desc);
        //theia.Match(obj_feat, sce_feat, good_matches);
        std::vector<IndexedFeatureMatch> result = theia.Match(theia.CreateHashedDescriptors(obj_desc), theia.CreateHashedDescriptors(sce_desc)); 
        for(int j = 0; j < result.size(); j++)
        {
            DMatch temp = DMatch(result[j].feature1_ind, result[j].feature2_ind, result[j].distance);
            good_matches.push_back(temp);
        }
    }
    auto end = system_clock::now();
    auto duration = duration_cast<microseconds>(end - start);
    LOG(INFO) << "Match cost"
        << double(duration.count()) * microseconds::period::num / microseconds::period::den / loops
        << " sec" << " with goodMatches: " << good_matches.size() << endl;

    start = system_clock::now();

    int ransacPoints = 0;
    std::vector< DMatch > good_matches_after_ransac; 
    if(good_matches.size() > 4)
    {
        std::vector<cv::Point2f> points1;
        std::vector<cv::Point2f> points2;
        for(int i = 0; i < good_matches.size(); i++)
        {
            points1.push_back(keypoints_object[good_matches[i].queryIdx].pt);
            points2.push_back(keypoints_scene[good_matches[i].trainIdx].pt);
        }
        std::vector<uchar> inliers(points1.size(),0);
        cv::Mat fundemental= cv::findFundamentalMat(cv::Mat(points1),cv::Mat(points2),inliers,CV_FM_RANSAC);
        for(int i = 0; i < inliers.size(); i++)
        {
            if((unsigned int)inliers[i])
            {
                ransacPoints++;
                good_matches_after_ransac.push_back(good_matches[i]);
            }
        }
    }
    std::cout << "RANSAC inlier points: " << ransacPoints<< std::endl; 
    end = system_clock::now();
    duration = duration_cast<microseconds>(end - start);
    LOG(INFO) << "RANSAC cost"
        << double(duration.count()) * microseconds::period::num / microseconds::period::den / loops
        << " sec" << endl;

    //draw matches
    Mat img_object = cv::imread( objectInputFile, cv::IMREAD_GRAYSCALE); 
    Mat img_scene = cv::imread( sceneInputFile, cv::IMREAD_GRAYSCALE);
    Mat img_matches;
    drawMatches( img_object, keypoints_object, img_scene, keypoints_scene,
            good_matches_after_ransac, img_matches, Scalar::all(-1), Scalar::all(-1),
            vector<char>(), DrawMatchesFlags::DEFAULT );
    imwrite("matches.jpg", img_matches);
#endif
	return 0;
}



