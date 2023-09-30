#include "utility.h"
#include "lio_sam/cloud_info.h"

#include <lanelet2_core/primitives/Lanelet.h>
#include <lanelet2_io/Io.h>
#include <lanelet2_io/io_handlers/Factory.h>
#include <lanelet2_io/io_handlers/Writer.h>
#include <lanelet2_projection/UTM.h>
#include <lanelet2_core/geometry/BoundingBox.h>
#include <lanelet2_core/geometry/Lanelet.h>
#include <lanelet2_core/geometry/Point.h>
#include <lanelet2_core/primitives/Lanelet.h>

#include <cstdio>
#include <iostream>
#include <dirent.h>
#include <utility> //pair
#include <chrono>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <Eigen/Dense>

using namespace lanelet;
using namespace std;

projection::UtmProjector projector(Origin({37.528444, 122.0780557}));

class liauto :public ParamServer
{
public:
    ros::Subscriber subCloud;
    ros::Subscriber subGPS;
    std::deque<nav_msgs::Odometry> gpsQueue;
    lio_sam::cloud_info cloudInfo;
    //Global Varibal
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloudMap;
    pcl::PointCloud<pcl::PointXYZ>::Ptr globalCornerMap;
    pcl::PointCloud<pcl::PointXYZ>::Ptr globalSurfaceMap;
    pcl::PointCloud<pcl::PointXYZ>::Ptr localCornerMap;
    pcl::PointCloud<pcl::PointXYZ>::Ptr localSurfaceMap;
    pcl::PointCloud<pcl::PointXYZ>::Ptr trajPoint;
    // 当前激光帧角点集合
    pcl::PointCloud<PointType>::Ptr laserCloudCornerLast; 
    // 当前激光帧平面点集合
    pcl::PointCloud<PointType>::Ptr laserCloudSurfLast; 

    pcl::KdTreeFLANN<pcl::PointXYZ>::Ptr kdtreeGlobalCornerMap;
    pcl::KdTreeFLANN<pcl::PointXYZ>::Ptr kdtreeGlobalSurfaceMap;
    pcl::KdTreeFLANN<pcl::PointXYZ>::Ptr kdtreeLocalCornerMap;
    pcl::KdTreeFLANN<pcl::PointXYZ>::Ptr kdtreeLocalSurfaceMap;
    pcl::KdTreeFLANN<pcl::PointXYZ>::Ptr kdtreeTrajPoint;

    ros::Publisher pubCloudMap;
    ros::Publisher pubGlobalCornerMap;
    ros::Publisher pubGlobalSurfaceMap;
    ros::Publisher pubLocalCornerMap;
    ros::Publisher pubLocalSurfaceMap;
    ros::Publisher pubIcpResult;
    ros::Publisher pubCurrScan;
    ros::Publisher pubTmpCloud;

    ros::Publisher pubLaneletMap;
    ros::Subscriber subGps;
    ros::Subscriber subOriGps;
    visualization_msgs::Marker currGpsPath;
    ros::Publisher pubCurrGpsPath;
    
    pcl::PointXYZ currGpsPoint;
    bool gpsVaild=false;
    ros::Subscriber subCurrLidarScan;
    std::deque<sensor_msgs::PointCloud2> lidarMsgDeque;
    ros::Subscriber subImu;
    std::deque<sensor_msgs::Imu> imuQueue;
    pcl::PointCloud<pcl::PointXYZ> lidarScan;
    double lidarScanTime=0;
    Eigen::AngleAxisd magnOffset;
    Eigen::AngleAxisd negPidiv2;
    double currGpsTime=0.0;
    double timeLaserInfoCur=0.0;
    double timeLaserInfoLast=0.0;
    float oriGpsPoseConv=0.0;
    ros::Time timeLaserInfoStamp;
    float transformTobeMapped[6];//rpyxyz
    bool initialDone=false;

    string keyMapDir="/home/limy/roscode/map(copy)/keyMap/";
    vector<string> keyMapFiles;
    vector<pair<pcl::PointCloud<pcl::PointXYZ>,pcl::PointCloud<pcl::PointXYZ>>> keyMap;
    vector<pair<pcl::KdTreeFLANN<pcl::PointXYZ>,pcl::KdTreeFLANN<pcl::PointXYZ>>> keyMapKdtree;

    pcl::PointCloud<pcl::PointXYZ>::Ptr laserCloudSurfFromMap;//use in scan to map 
    pcl::PointCloud<pcl::PointXYZ>::Ptr laserCloudCornerFromMap;
    pcl::KdTreeFLANN<pcl::PointXYZ>::Ptr kdtreeCornerFromMap;
    pcl::KdTreeFLANN<pcl::PointXYZ>::Ptr kdtreeSurfFromMap;


    // 当前帧与局部map匹配上了的角点、平面点，加入同一集合；后面是对应点的参数
    pcl::PointCloud<pcl::PointXYZ>::Ptr laserCloudOri;
    pcl::PointCloud<PointType>::Ptr coeffSel;

    // 当前帧与局部map匹配上了的角点、参数、标记
    std::vector<pcl::PointXYZ> laserCloudOriCornerVec; 
    std::vector<PointType> coeffSelCornerVec;
    std::vector<bool> laserCloudOriCornerFlag;
    // 当前帧与局部map匹配上了的平面点、参数、标记
    std::vector<pcl::PointXYZ> laserCloudOriSurfVec; 
    std::vector<PointType> coeffSelSurfVec;
    std::vector<bool> laserCloudOriSurfFlag;
    cv::Mat matP;
    bool isDegenerate = false;

    // 当前帧位姿
    Eigen::Affine3f transPointAssociateToMap;
    // 前一帧位姿 //用于计算相对增量，累计到全局变换中
    Eigen::Affine3f incrementalOdometryAffineFront;
    // 当前帧位姿
    Eigen::Affine3f incrementalOdometryAffineBack;
    //imu的预积分量，每时刻累加起来更新transformToBe;
    Eigen::Affine3f lastImuPreTransformation;
    ros::Publisher pubOdom;
    nav_msgs::Odometry laserOdomIncremental;
    ros::Publisher pubLaserOdometryIncremental;
    Eigen::Affine3f increOdomAffine; 
    ros::Publisher pubPose;
    // 降采样
    pcl::VoxelGrid<pcl::PointXYZ> downSizeFilterCorner;
    pcl::VoxelGrid<pcl::PointXYZ> downSizeFilterSurf; 
    pcl::PointCloud<pcl::PointXY> centerLanePoints;
    ros::Publisher pubCenterLane;
    liauto()//构造函数
    {
        pubCloudMap=nh.advertise<sensor_msgs::PointCloud2> ("cloudMap", 1);
        pubGlobalCornerMap=nh.advertise<sensor_msgs::PointCloud2> ("GlobalCornerMap", 1);
        pubGlobalSurfaceMap=nh.advertise<sensor_msgs::PointCloud2> ("GlobalSurfaceMap", 1);
        pubLocalCornerMap=nh.advertise<sensor_msgs::PointCloud2> ("LocalCornerMap", 1);
        pubLocalSurfaceMap=nh.advertise<sensor_msgs::PointCloud2> ("LocalSurfaceMap", 1);
        pubIcpResult=nh.advertise<sensor_msgs::PointCloud2>("icpResult",1);
        pubCurrScan=nh.advertise<sensor_msgs::PointCloud2>("currScan",1);
        pubTmpCloud=nh.advertise<sensor_msgs::PointCloud2>("tmpCloud",1);
        pubLaneletMap=nh.advertise<visualization_msgs::MarkerArray>("laneletMap",1);
        pubCenterLane=nh.advertise<sensor_msgs::PointCloud2>("centerLane",1);
        pubCurrGpsPath=nh.advertise<visualization_msgs::Marker>("currGpsPath",1);
        pubOdom=nh.advertise<nav_msgs::Odometry> ("lio_sam/mapping/odometry", 1);
        // 发布激光里程计，它与上面的激光里程计基本一样
        pubLaserOdometryIncremental = nh.advertise<nav_msgs::Odometry> ("lio_sam/mapping/odometry_incremental", 1);
        //定位用的和激光同频率的定位信息
        pubPose=nh.advertise<nav_msgs::Odometry> ("currPose", 1);
        // 订阅当前激光帧点云信息，来自featureExtraction
        subCloud = nh.subscribe<lio_sam::cloud_info>("lio_sam/feature/cloud_info", 1, &liauto::laserCloudInfoHandler, this, ros::TransportHints().tcpNoDelay());
        // 订阅GPS里程计
        // subGPS   = nh.subscribe<nav_msgs::Odometry> (gpsTopic, 200, &liauto::gpsHandler, this, ros::TransportHints().tcpNoDelay());
        subOriGps=nh.subscribe<sensor_msgs::NavSatFix> ("/gps/fix", 200, &liauto::OriGpsHandler, this, ros::TransportHints().tcpNoDelay());
        allocateMemory();
        loadCloudMap();
        loadLanelet2Map();
        // while(!initialDone)
        // {
        //     relocation();
        //     if(!initialDone)
        //     {
        //         cout<<"Relocation failed, please move around the map rode. "<<endl;
        //     }
        // }

    }
    void allocateMemory()
    {
        cloudMap.reset(new pcl::PointCloud<pcl::PointXYZ>());
        globalCornerMap.reset(new pcl::PointCloud<pcl::PointXYZ>());
        globalSurfaceMap.reset(new pcl::PointCloud<pcl::PointXYZ>());
        localCornerMap.reset(new pcl::PointCloud<pcl::PointXYZ>());
        localSurfaceMap.reset(new pcl::PointCloud<pcl::PointXYZ>());
        trajPoint.reset(new pcl::PointCloud<pcl::PointXYZ>());
        laserCloudCornerLast.reset(new pcl::PointCloud<PointType>());
        laserCloudSurfLast.reset(new pcl::PointCloud<PointType>());
        kdtreeGlobalCornerMap.reset(new pcl::KdTreeFLANN<pcl::PointXYZ>());
        kdtreeGlobalSurfaceMap.reset(new pcl::KdTreeFLANN<pcl::PointXYZ>());
        kdtreeLocalCornerMap.reset(new pcl::KdTreeFLANN<pcl::PointXYZ>());
        kdtreeLocalSurfaceMap.reset(new pcl::KdTreeFLANN<pcl::PointXYZ>());
        kdtreeTrajPoint.reset(new pcl::KdTreeFLANN<pcl::PointXYZ>());
        magnOffset=Eigen::AngleAxisd(0.141720733,Eigen::Vector3d(0,0,1));
        negPidiv2=Eigen::AngleAxisd(-3.1415926/2.0,Eigen::Vector3d(0,0,1));
        currGpsPath.header.frame_id="map";
        currGpsPath.header.stamp=ros::Time::now();
        currGpsPath.ns="currGpsPath_";//命名空间，防止和其他消息冲突
        currGpsPath.id=1;
        currGpsPath.type=visualization_msgs::Marker::LINE_STRIP;//画线
        currGpsPath.action = visualization_msgs::Marker::ADD;//相当于clear清除之前的数据
        currGpsPath.pose.position.x = 0.0;
        currGpsPath.pose.position.y = 0.0;
        currGpsPath.pose.position.z = 0.0;
        currGpsPath.pose.orientation.x = 0.0;
        currGpsPath.pose.orientation.y = 0.0;
        currGpsPath.pose.orientation.z = 0.0;
        currGpsPath.pose.orientation.w = 1.0;
        currGpsPath.scale.x = 1;
        currGpsPath.scale.y = 1;
        currGpsPath.scale.z = 1;
        currGpsPath.color.a = 1.0;
        currGpsPath.color.r = 0.0;
        currGpsPath.color.g = 1.0;
        laserCloudSurfFromMap.reset(new pcl::PointCloud<pcl::PointXYZ>());//
        laserCloudCornerFromMap.reset(new pcl::PointCloud<pcl::PointXYZ>());
        kdtreeSurfFromMap.reset(new pcl::KdTreeFLANN<pcl::PointXYZ>());
        kdtreeCornerFromMap.reset(new pcl::KdTreeFLANN<pcl::PointXYZ>());
        laserCloudOri.reset(new pcl::PointCloud<pcl::PointXYZ>());
        coeffSel.reset(new pcl::PointCloud<PointType>());
        laserCloudOriCornerVec.resize(N_SCAN * Horizon_SCAN);
        coeffSelCornerVec.resize(N_SCAN * Horizon_SCAN);
        laserCloudOriCornerFlag.resize(N_SCAN * Horizon_SCAN);
        laserCloudOriSurfVec.resize(N_SCAN * Horizon_SCAN);
        coeffSelSurfVec.resize(N_SCAN * Horizon_SCAN);
        laserCloudOriSurfFlag.resize(N_SCAN * Horizon_SCAN);
        // 清空标记
        std::fill(laserCloudOriCornerFlag.begin(), laserCloudOriCornerFlag.end(), false);
        std::fill(laserCloudOriSurfFlag.begin(), laserCloudOriSurfFlag.end(), false);
        matP = cv::Mat(6, 6, CV_32F, cv::Scalar::all(0));
        downSizeFilterCorner.setLeafSize(mappingCornerLeafSize, mappingCornerLeafSize, mappingCornerLeafSize);
        downSizeFilterSurf.setLeafSize(mappingSurfLeafSize, mappingSurfLeafSize, mappingSurfLeafSize);
    }
    
    static bool customerSort(string& s1,string& s2)
    {
        if(s1[3]=='C'&&s2[3]=='C')
        {
            return stoi(s1.substr(9))<stoi(s2.substr(9));
        }
        else if(s1[3]=='S'&&s2[3]=='S')
        {
            return stoi(s1.substr(10))<stoi(s2.substr(10));
        }
        else if(s1[3]=='C'&&s2[3]=='S')
        {
            return true;
        }
        return false;
    }
    
    void readkeyMap()
    {
        pcl::io::loadPCDFile("/home/limy/roscode/map(copy)/trajectory.pcd",*trajPoint);
        cout<<"trajectory num: "<<trajPoint->points.size()<<endl;
        kdtreeTrajPoint->setInputCloud(trajPoint);


        DIR* dir=opendir(keyMapDir.c_str());
        if(dir==nullptr)
        {
            cout<<"failed to open keyMapDir "<<keyMapDir<<endl;
            return ;
        }
        dirent* entry=readdir(dir);
        while(entry!=nullptr)
        {
            if(entry->d_type==DT_REG)
            {
                keyMapFiles.push_back(entry->d_name);
            }
            entry=readdir(dir);
        }
        sort(keyMapFiles.begin(),keyMapFiles.end());
        sort(keyMapFiles.begin(),keyMapFiles.end(),customerSort);
        cout<<"total keyMap num: "<<keyMapFiles.size()/2-1<<endl;
        // if((keyMapFiles.size()/2-1)!=trajPoint->points.size())
        // {
        //     cout<<"trajectory not match keyMap!"<<endl;
        //     return ;
        // }
        closedir(dir);
        pcl::PointCloud<pcl::PointXYZ> tempCloud;
        pair<pcl::PointCloud<pcl::PointXYZ>,pcl::PointCloud<pcl::PointXYZ>> tempPair;
        // pcl::KdTreeFLANN<pcl::PointXYZ> tempKd;
        // pair<pcl::KdTreeFLANN<pcl::PointXYZ>,pcl::KdTreeFLANN<pcl::PointXYZ>> tempKdPair;
        int keymapSize=trajPoint->points.size();
        for(int i=0;i<=keymapSize;i++)
        {
            

            pcl::io::loadPCDFile(keyMapDir+keyMapFiles[i],tempCloud);
            tempPair.first=tempCloud;
            // tempKd.setInputCloud(tempCloud.makeShared());
            // tempKdPair.first=tempKd;


            pcl::io::loadPCDFile(keyMapDir+keyMapFiles[i+keymapSize+1],tempCloud);
            tempPair.second=tempCloud;
            // tempKd.setInputCloud(tempCloud.makeShared());
            // tempKdPair.second=tempKd;

            keyMap.push_back(tempPair);
            // keyMapKdtree.push_back(tempKdPair);
            // cout<<keyMapFiles[i]<<"->"<<keyMapFiles[i+keymapSize+1]<<endl;

            
            
        }

        cout<<"key Map loaded! "<<endl;

    }
    
    /**
     * Eigen格式的位姿变换
    */
    Eigen::Isometry3d trans2Isometry3d(float transformIn[])
    {
        Eigen::Isometry3d temp;
        // Eigen::Quaterniond rotation=Eigen::AngleAxisd(transformIn[2], Eigen::Vector3d::UnitZ()) *
        //             Eigen::AngleAxisd(transformIn[1], Eigen::Vector3d::UnitY()) *
        //             Eigen::AngleAxisd(transformIn[0], Eigen::Vector3d::UnitX());
        Eigen::Quaterniond rotation=Eigen::AngleAxisd(M_PI/3.0, Eigen::Vector3d::UnitZ()) *
                    Eigen::AngleAxisd(M_PI/4.0, Eigen::Vector3d::UnitY()) *
                    Eigen::AngleAxisd(0, Eigen::Vector3d::UnitX());
        temp.pretranslate(Eigen::Vector3d(transformIn[3],transformIn[4],transformIn[5]));
        temp.rotate(rotation);
        return temp;
    }

    Eigen::Affine3f trans2Affine3f(float transformIn[])
    {
        return pcl::getTransformation(transformIn[3], transformIn[4], transformIn[5], transformIn[0], transformIn[1], transformIn[2]);
    }


    void laserCloudInfoHandler(const lio_sam::cloud_infoConstPtr& msgIn)
    {
        // 当前激光帧时间戳
        timeLaserInfoStamp = msgIn->header.stamp;
        timeLaserInfoCur = msgIn->header.stamp.toSec();

        // 提取当前激光帧角点、平面点集合
        cloudInfo = *msgIn;
        pcl::fromROSMsg(msgIn->cloud_corner,  *laserCloudCornerLast);
        pcl::fromROSMsg(msgIn->cloud_surface, *laserCloudSurfLast);
        if(initialDone&&(timeLaserInfoCur-timeLaserInfoLast)>scanToMapGap)
        {
            auto startTime=std::chrono::steady_clock::now();
            //update init pose ，scan to map之前的位姿
            incrementalOdometryAffineFront=trans2Affine3f(transformTobeMapped);
            increOdomAffine=trans2Affine3f(transformTobeMapped);

            scan2MapOptimization();
            auto endTime=std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

            // Print the elapsed time
            if(elapsed.count()>100)
            {
                std::cout << "Elapsed time over 100 ms !: " << elapsed.count() << " milliseconds" << std::endl;
            }
            
            // 更新当前帧位姿的roll, pitch, z坐标；因为是小车，roll、pitch是相对稳定的，不会有很大变动，一定程度上可以信赖imu的数据，z是进行高度约束
            transformTobeMapped[0] = constraintTransformation(transformTobeMapped[0], rotation_tollerance);
            transformTobeMapped[1] = constraintTransformation(transformTobeMapped[1], rotation_tollerance);
            transformTobeMapped[5] = constraintTransformation(transformTobeMapped[5], z_tollerance);

            // 当前帧位姿scan to map之后的位姿
            incrementalOdometryAffineBack = trans2Affine3f(transformTobeMapped);
            //把下面imu的当前变换也更新下
            lastImuPreTransformation=trans2Affine3f(transformTobeMapped);
            timeLaserInfoLast=timeLaserInfoCur;
            // cout<<"xyzrpy:"<<transformTobeMapped[3]<<" "<<transformTobeMapped[4]<<" "<<transformTobeMapped[5]<<" "<<transformTobeMapped[0]<<" "<<transformTobeMapped[1]<<" "<<transformTobeMapped[2]<<endl;
            publishOdom();//imu 预积分需要这个



            
        }
        else if(initialDone&&cloudInfo.odomAvailable)
        {
            // 当前帧的初始估计位姿（来自imu里程计），后面用来计算增量位姿变换
            Eigen::Affine3f transBack = pcl::getTransformation(cloudInfo.initialGuessX,    cloudInfo.initialGuessY,     cloudInfo.initialGuessZ, 
                                                               cloudInfo.initialGuessRoll, cloudInfo.initialGuessPitch, cloudInfo.initialGuessYaw);
            // cout<<"initGassXYZ:"<<cloudInfo.initialGuessX<<" "<<cloudInfo.initialGuessY<<" "<<cloudInfo.initialGuessZ<<" "<<endl;
            // 当前帧相对于前一帧的位姿变换，imu里程计计算得到
            Eigen::Affine3f transIncre = lastImuPreTransformation.inverse() * transBack;
            // 前一帧的位姿
            Eigen::Affine3f transTobe = trans2Affine3f(transformTobeMapped);
            // 当前帧的位姿
            Eigen::Affine3f transFinal = transTobe * transIncre;
            // 更新当前帧位姿transformTobeMapped
            pcl::getTranslationAndEulerAngles(transFinal, transformTobeMapped[3], transformTobeMapped[4], transformTobeMapped[5], 
                                                            transformTobeMapped[0], transformTobeMapped[1], transformTobeMapped[2]);
            // 赋值给前一帧
            lastImuPreTransformation = transBack;
        }
        nav_msgs::Odometry curPose;
        curPose.header.stamp = timeLaserInfoStamp;
        curPose.header.frame_id = "/map";
        curPose.pose.pose.position.x = transformTobeMapped[3];
        curPose.pose.pose.position.y = transformTobeMapped[4];
        curPose.pose.pose.position.z = transformTobeMapped[5];
        curPose.pose.pose.orientation = tf::createQuaternionMsgFromRollPitchYaw(transformTobeMapped[0], transformTobeMapped[1], transformTobeMapped[2]);
        pubPose.publish(curPose);
        
        
    }
    //经过nvsate节点滤波后的GPS数据
    void gpsHandler(const nav_msgs::Odometry::ConstPtr& gpsMsg)
    {
        gpsQueue.push_back(*gpsMsg);
        
        // if(gpsMsg->pose.covariance[0]<100)
        // {
            
        //     currGpsPoint.x=gpsMsg->pose.pose.position.x;
        //     currGpsPoint.y=gpsMsg->pose.pose.position.y;
        //     currGpsPoint.z=gpsMsg->pose.pose.position.z;
            
        //     gpsVaild=true;
        //     currGpsTime=gpsMsg->header.stamp.toSec();


        //     //可视化
        //     geometry_msgs::Point pointVisual;
        //     pointVisual.x=gpsMsg->pose.pose.position.x;
        //     pointVisual.y=gpsMsg->pose.pose.position.y;
        //     pointVisual.z=gpsMsg->pose.pose.position.z;
        //     currGpsPath.points.push_back(pointVisual);
        //     pubCurrGpsPath.publish(currGpsPath);

        // }
    }
    //原始GPS数据，没经过滤波
    void OriGpsHandler(const sensor_msgs::NavSatFix::ConstPtr& gpsMsg)
    {
        // cout<<"gps revieve"<<endl;
        //只用在重定位中
        if(initialDone)
        {
            return ;
        }
        oriGpsPoseConv=gpsMsg->position_covariance[0];
        if(gpsMsg->position_covariance[0]<100)//10m置信度内
        {
            geometry_msgs::Point tmp;//可视化的点
            GPSPoint gp;//lanelet计算相对位置的点
            gp.lat=gpsMsg->latitude;
            gp.lon=gpsMsg->longitude;
            BasicPoint3d p=projector.forward(gp);//用forward可以把当前GPS点转换到以projection::UtmProjector projector(Origin({37.528444, 122.0780557}))为原点的局部坐标系，也就是现在我们的点云坐标系
            tmp.x=p.x();
            tmp.y=p.y();
            tmp.z=0.0;
            Eigen::Matrix3d rotationMatrix =Eigen::AngleAxisd(-M_PI/2.0,Eigen::Vector3d(0,0,1)).toRotationMatrix();//建图的时候多转了PI/2
            Eigen::Vector3d res=rotationMatrix*Eigen::Vector3d(tmp.x,tmp.y,tmp.z);
            tmp.x=res.x();
            tmp.y=res.y();
            tmp.z=res.z();
            currGpsPath.points.push_back(tmp);
            pubCurrGpsPath.publish(currGpsPath);
            currGpsPoint.x=res.x();
            currGpsPoint.y=res.y();
            currGpsPoint.z=res.z();

            gpsVaild=true;
            currGpsTime=gpsMsg->header.stamp.toSec();

        }
    }


    void loadCloudMap()
    {

        // cout<<"loading cloud map, this may take a long time. "<<endl;
        // pcl::PointCloud<pcl::PointXYZ>::Ptr tempCloud(new pcl::PointCloud<pcl::PointXYZ>());
        // pcl::io::loadPCDFile("/home/limy/roscode/map(copy)/CornerMap.pcd",*tempCloud);
        // downSizeFilterCorner.setInputCloud(tempCloud);
        // downSizeFilterCorner.filter(*laserCloudCornerFromMap);

        // sensor_msgs::PointCloud2 tempCloudMag;
        // pcl::toROSMsg(*laserCloudCornerFromMap, tempCloudMag);
        // tempCloudMag.header.stamp = ros::Time::now();
        // tempCloudMag.header.frame_id = "/map";
        // for(int i=0;i<2;i++)
        // {
        //     pubGlobalCornerMap.publish(tempCloudMag);
        //     ros::Duration(1.0).sleep();
        // }
        

        // tempCloud->clear();
        
        // pcl::io::loadPCDFile("/home/limy/roscode/map(copy)/SurfMap.pcd",*tempCloud);
        // cout<<"befor downsize num :"<<tempCloud->points.size();
        // downSizeFilterSurf.setInputCloud(tempCloud);
        // downSizeFilterSurf.filter(*laserCloudSurfFromMap);
        // cout<<" after downsize num :"<<laserCloudSurfFromMap->points.size()<<endl;



        pcl::io::loadPCDFile("/home/limy/roscode/map(copy)/trajectory.pcd",*trajPoint);

        // kdtreeTrajPoint->setInputCloud(trajPoint);
        // kdtreeCornerFromMap->setInputCloud(laserCloudCornerFromMap);
        // kdtreeSurfFromMap->setInputCloud(laserCloudSurfFromMap);
        // cout<<"map loaded. "<<endl;
        pcl::PointCloud<pcl::PointXYZ>::Ptr transedTraj;
        transedTraj.reset(new pcl::PointCloud<pcl::PointXYZ>());
        float tempTransform[6];
        tempTransform[0]=10.0/180.0*3.14;
        tempTransform[1]=50.0/180.0*3.14;
        tempTransform[2]=61.0/180.0*3.14;
        tempTransform[3]=3.845;
        tempTransform[4]=8.451;
        tempTransform[5]=4.125;
        Eigen::Affine3f tempTransformAffine=trans2Affine3f(tempTransform);
        Eigen::Vector3f tmp;
        pcl::PointXYZ tp;
        for(int i=0;i<trajPoint->size();i++)
        {
            tmp[0]=trajPoint->points[i].x;
            tmp[1]=trajPoint->points[i].y;
            tmp[2]=trajPoint->points[i].z;
            tmp=tempTransformAffine*tmp;
            tp.x=tmp[0];
            tp.y=tmp[1];
            tp.z=tmp[2];
            transedTraj->points.push_back(tp);
        }
        *transedTraj+=*trajPoint;
        pcl::io::savePCDFile("/home/limy/roscode/paperData/combile.pcd",*transedTraj);


    }
    static bool centerLaneComp(lanelet::Lanelet& l1,lanelet::Lanelet& l2)
    {
        return l1.id()<l2.id();
    }
    void loadLanelet2Map()
    {
    // loading a map requires two things: the path and either an origin or a projector that does the lat/lon->x/y
    // conversion.
    std::string exampleMapPath =  "/home/limy/roscode/map(copy)/new_lanelet2_maps.osm";
        // we will go into details later
    // projection::SphericalMercatorProjector
    LaneletMapPtr map = load(exampleMapPath, projector);
    PointLayer& points = map->pointLayer;
    LineStringLayer& linestrings = map->lineStringLayer;
    LaneletLayer& laneletLayers=map->laneletLayer;
    visualization_msgs::MarkerArray laneletMap;
    laneletMap.markers.resize(linestrings.size());
    int i=0;
    //可视化lanelet道路
    for(auto ll : linestrings)
    {
        laneletMap.markers[i].header.frame_id="map";
        laneletMap.markers[i].header.stamp=ros::Time::now();
        laneletMap.markers[i].ns="laneletMap___";//命名空间，防止和其他消息冲突
        laneletMap.markers[i].id=i;
        laneletMap.markers[i].type=visualization_msgs::Marker::LINE_STRIP;//画线
        laneletMap.markers[i].action = visualization_msgs::Marker::ADD;//相当于clear清除之前的数据
        laneletMap.markers[i].pose.position.x = 0.0;
        laneletMap.markers[i].pose.position.y = 0.0;
        laneletMap.markers[i].pose.position.z = 0.0;
        laneletMap.markers[i].pose.orientation.x = 0.0;
        laneletMap.markers[i].pose.orientation.y = 0.0;
        laneletMap.markers[i].pose.orientation.z = 0.0;
        laneletMap.markers[i].pose.orientation.w = 1.0;
        laneletMap.markers[i].scale.x = 1;
        laneletMap.markers[i].scale.y = 1;
        laneletMap.markers[i].scale.z = 1;
        laneletMap.markers[i].color.a = 1.0;
        laneletMap.markers[i].color.r = 1.0;
        for(auto p:ll)
        {
        
        geometry_msgs::Point tmp;
        tmp.x=stod(p.attribute("local_x").value());
        tmp.y=stod(p.attribute("local_y").value());
        GPSPoint gp=projector.reverse(p);//用projector把UTM点转换回GPS点，这样就能用ele属性了。
        tmp.z=gp.ele; 
        laneletMap.markers[i].points.push_back(tmp);
        }
        i++;
    }
    //
    // sort(laneletLayers.begin(),laneletLayers.end(),centerLaneComp);
    // for(auto sublane:laneletLayers)
    // {
    //     auto centerLane=sublane.centerline2d();
    //     pcl::PointXY tempP;
        
    //     for(auto centerLaneP:centerLane)
    //     {
    //         tempP.x=stod(centerLaneP.attribute("local_x").value());
    //         tempP.y=stod(centerLaneP.attribute("local_y").value());
            
    //         centerLanePoints.points.push_back(tempP);
    //         printf("id:%6d,x:%6f,y:%6f\r\n",centerLaneP.id(),tempP.x,tempP.y);
    //     }
    // }
    vector<lanelet::Lanelet> laneLetVector;
    for(auto sublane:laneletLayers)
    {
        laneLetVector.push_back(sublane);
        // printf("sublaneId:%6d\r\n",sublane.id());
    }
    sort(laneLetVector.begin(),laneLetVector.end(),centerLaneComp);
    for(int i=0;i<laneLetVector.size();i++)
    {
        printf("laneLetVector[i].uniqueId():%6d\r\n",laneLetVector[i].id());
        auto centerLane=laneLetVector[i].centerline2d();
        pcl::PointXY tempP;
        
        for(auto centerLaneP:centerLane)
        {
            tempP.x=stod(centerLaneP.attribute("local_x").value());
            tempP.y=stod(centerLaneP.attribute("local_y").value());
            
            centerLanePoints.points.push_back(tempP);
            // printf("id:%6d,x:%6f,y:%6f\r\n",centerLaneP.id(),tempP.x,tempP.y);
        }
    }
    cout<<"total center lane Points: "<<centerLanePoints.points.size()<<endl;
    sensor_msgs::PointCloud2 tempCloudMag;
    pcl::toROSMsg(centerLanePoints, tempCloudMag);
    tempCloudMag.header.stamp = ros::Time::now();
    tempCloudMag.header.frame_id = "/map";
    for(int k=0;k<2;k++)
    {
        pubLaneletMap.publish(laneletMap);
        pubCenterLane.publish(tempCloudMag);
        ros::Duration(1.0).sleep();
    }
        

    }

    void transformPointCloud(pcl::PointCloud<PointType>::Ptr cloudin,Eigen::Isometry3d transform,pcl::PointCloud<PointType>::Ptr cloudout)
    {
        Eigen::Vector3d tmp;
        PointType tp;
        for(int i=0;i<cloudin->size();i++)
        {
            tmp[0]=cloudin->points[i].x;
            tmp[1]=cloudin->points[i].y;
            tmp[2]=cloudin->points[i].z;
            tmp=transform*tmp;
            tp.x=tmp[0];
            tp.y=tmp[1];
            tp.z=tmp[2];
            tp.intensity=cloudin->points[i].intensity;
            cloudout->points.push_back(tp);
        }
    }
    void transformPointCloud(pcl::PointCloud<PointType>::Ptr cloudin,Eigen::Isometry3d transform,pcl::PointCloud<pcl::PointXYZ>::Ptr cloudout)
    {
        Eigen::Vector3d tmp;
        pcl::PointXYZ tp;
        for(int i=0;i<cloudin->size();i++)
        {
            tmp[0]=cloudin->points[i].x;
            tmp[1]=cloudin->points[i].y;
            tmp[2]=cloudin->points[i].z;
            tmp=transform*tmp;
            tp.x=tmp[0];
            tp.y=tmp[1];
            tp.z=tmp[2];
            cloudout->points.push_back(tp);
        }
    }
    void transformPointCloud(pcl::PointCloud<PointType>::Ptr cloudin,Eigen::Affine3f transform,pcl::PointCloud<pcl::PointXYZ>::Ptr cloudout)
    {
        Eigen::Vector3f tmp;
        pcl::PointXYZ tp;
        for(int i=0;i<cloudin->size();i++)
        {
            tmp[0]=cloudin->points[i].x;
            tmp[1]=cloudin->points[i].y;
            tmp[2]=cloudin->points[i].z;
            tmp=transform*tmp;
            tp.x=tmp[0];
            tp.y=tmp[1];
            tp.z=tmp[2];
            cloudout->points.push_back(tp);
        }
    }
    /**
     * 激光坐标系下的激光点，通过激光帧位姿，变换到世界坐标系下
    */
    void pointAssociateToMap(pcl::PointXYZ const * const pi, pcl::PointXYZ * const po)
    {
        po->x = transPointAssociateToMap(0,0) * pi->x + transPointAssociateToMap(0,1) * pi->y + transPointAssociateToMap(0,2) * pi->z + transPointAssociateToMap(0,3);
        po->y = transPointAssociateToMap(1,0) * pi->x + transPointAssociateToMap(1,1) * pi->y + transPointAssociateToMap(1,2) * pi->z + transPointAssociateToMap(1,3);
        po->z = transPointAssociateToMap(2,0) * pi->x + transPointAssociateToMap(2,1) * pi->y + transPointAssociateToMap(2,2) * pi->z + transPointAssociateToMap(2,3);
    }
    
    void relocation()
    {
        while(!gpsVaild)
        {
            cout<<"current GPS signal bad. "<<"GPS position_covariance: "<<oriGpsPoseConv<<endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ros::spinOnce();
        }
        cout<<"GPS valid"<<endl;
        //找到离当前gps点最近的轨迹点，把点云变换到该轨迹，再提取该轨迹附近50m的点云地图，和当前扫描做icp配准
        //由于有高度差，先找离GPS点最近的点云点，以这个点找子地图。
        std::vector<int> searchIndex;
        std::vector<float> searchDistance;
        kdtreeTrajPoint->nearestKSearch(currGpsPoint, 1,searchIndex,searchDistance);
        pcl::PointXYZ nearestP=trajPoint->points[searchIndex[0]];
        cout<<"current GPS point: "<<currGpsPoint<<", nearest traj point: "<<nearestP<<endl;

        pcl::PointCloud<pcl::PointXYZ>::Ptr subMap(new pcl::PointCloud<pcl::PointXYZ>());
        searchIndex.clear();
        searchDistance.clear();
        kdtreeCornerFromMap->radiusSearch(currGpsPoint,50,searchIndex,searchDistance);
        for(int i=0;i<searchIndex.size();i++)
        {
            subMap->points.push_back(laserCloudCornerFromMap->points[searchIndex[i]]);
        }
        searchIndex.clear();
        searchDistance.clear();
        kdtreeSurfFromMap->radiusSearch(currGpsPoint,50,searchIndex,searchDistance);

        for(int i=0;i<searchIndex.size();i++)
        {
            subMap->points.push_back(laserCloudSurfFromMap->points[searchIndex[i]]);
        }

        cout<<"subCornerMap size: "<<subMap->size()<<endl;
        sensor_msgs::PointCloud2 subCornerMapMsg;
        pcl::toROSMsg(*subMap,subCornerMapMsg);
        subCornerMapMsg.header.frame_id="/map";//设置frame id要放在pcl::toROSMsg后面，pcl::toROSMsg会重置subCornerMapMsg
        subCornerMapMsg.header.stamp=ros::Time::now();
        pubLocalCornerMap.publish(subCornerMapMsg);

        //将当前激光点云按照imu姿态变换下，加快匹配速度
        while(laserCloudCornerLast->size()==0)
        {
            cout<<"no lidar scan"<<endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ros::spinOnce();
        }
        cout<<"lidar scan ok"<<endl;

        pcl::PointCloud<pcl::PointXYZ>::Ptr tmpCloud(new pcl::PointCloud<pcl::PointXYZ>());
        
        
        transformTobeMapped[0]=0.0;
        transformTobeMapped[1]=0.0;
        transformTobeMapped[2]=0.0;
        if(cloudInfo.imuAvailable)
        {

            transformTobeMapped[0]=cloudInfo.imuRollInit;
            transformTobeMapped[1]=cloudInfo.imuPitchInit;
            transformTobeMapped[2]=cloudInfo.imuYawInit-M_PI_2;
        }
        transformTobeMapped[3]=nearestP.x;
        transformTobeMapped[4]=nearestP.y;
        transformTobeMapped[5]=nearestP.z;
        Eigen::Affine3f T=trans2Affine3f(transformTobeMapped);
        // cout<<"rev init rpy:"<<T.rotation().eulerAngles(0,1,2).transpose()<<endl;
        transformPointCloud(laserCloudCornerLast,T,tmpCloud);
        // sensor_msgs::PointCloud2 tmpCloudMsg;
        // pcl::toROSMsg(*tmpCloud,tmpCloudMsg);
        // tmpCloudMsg.header.frame_id="/map";
        // tmpCloudMsg.header.stamp=ros::Time::now();
        // pubTmpCloud.publish(tmpCloudMsg);
        

        //icp
        pcl::IterativeClosestPoint<pcl::PointXYZ ,pcl::PointXYZ> icp;

        icp.setInputSource(tmpCloud);
        icp.setInputTarget(subMap);
        icp.setMaxCorrespondenceDistance(50);
        icp.setMaximumIterations(100);
        icp.setTransformationEpsilon(1e-6);
        icp.setEuclideanFitnessEpsilon(1e-6);
        icp.setRANSACIterations(0);


        // Perform the alignment
        pcl::PointCloud<pcl::PointXYZ>::Ptr icpResult(new pcl::PointCloud<pcl::PointXYZ>());
        cout<<"start icp !"<<endl;
        icp.align (*icpResult);
        std::cout << "ICP has converged:" << icp.hasConverged ()
                    << " score: " << icp.getFitnessScore () << std::endl;

       
        if(icp.hasConverged())
        {

            cout<<"icp finish"<<endl;
            sensor_msgs::PointCloud2 tmp;
            pcl::toROSMsg(*icpResult,tmp);
            tmp.header.frame_id="/map";
            tmp.header.stamp=ros::Time::now();

            pubIcpResult.publish(tmp);

             // Obtain the transformation that aligned cloud_source to cloud_source_registered
            Eigen::Matrix4f transformation = icp.getFinalTransformation ();
            Eigen::Isometry3f isoM(transformation);
            Eigen::Vector3f eulerAngle=(isoM.rotation()).eulerAngles(0,1,2);//以r p y 的顺序返回
            cout<<"r p y:"<<eulerAngle.transpose()<<endl;

            transformTobeMapped[3]+=transformation(0,3);
            transformTobeMapped[4]+=transformation(1,3);
            transformTobeMapped[5]+=transformation(2,3);

            transformTobeMapped[0]+=eulerAngle[0];
            transformTobeMapped[1]+=eulerAngle[1];
            transformTobeMapped[2]+=eulerAngle[2];
            cout<<"transformTobeMaped:(rpyxyz)"<<endl;
            for(int i=0;i<6;i++)
            {
                cout<<transformTobeMapped[i]<<endl;
            }
            Eigen::Affine3f testTrans=trans2Affine3f(transformTobeMapped);

            pcl::PointCloud<PointType>::Ptr testPointCloud(new pcl::PointCloud<PointType>());
            pcl::PointCloud<pcl::PointXYZ>::Ptr testPointCloudOut(new pcl::PointCloud<pcl::PointXYZ>());
            *testPointCloud+=*laserCloudSurfLast;
            *testPointCloud+=*laserCloudCornerLast;
            transformPointCloud(testPointCloud,testTrans,testPointCloudOut);

            sensor_msgs::PointCloud2 testMsg;
            pcl::toROSMsg(*testPointCloudOut,testMsg);
            testMsg.header.frame_id="/map";
            testMsg.header.stamp=ros::Time::now();

            pubTmpCloud.publish(testMsg);

            lastImuPreTransformation=trans2Affine3f(transformTobeMapped);
            incrementalOdometryAffineFront=trans2Affine3f(transformTobeMapped);
            increOdomAffine=trans2Affine3f(transformTobeMapped);
            initialDone=true;

            
        }
        else
        {
            cout<<"icp failed"<<endl;
        }


    }
    
    /**
     * 当前激光帧角点寻找局部map匹配点
     * 1、更新当前帧位姿，将当前帧角点坐标变换到map系下，在局部map中查找5个最近点，距离小于1m，且5个点构成直线（用距离中心点的协方差矩阵，特征值进行判断），则认为匹配上了
     * 2、计算当前帧角点到直线的距离、垂线的单位向量，存储为角点参数
    */
    void cornerOptimization()
    {
        transPointAssociateToMap=trans2Affine3f(transformTobeMapped);

        // 遍历当前帧角点集合
        #pragma omp parallel for num_threads(numberOfCores)
        for (int i = 0; i < laserCloudCornerLast->size(); i++)
        {
            PointType coeff;
            pcl::PointXYZ pointOri, pointSel;
            std::vector<int> pointSearchInd;
            std::vector<float> pointSearchSqDis;

            // 角点（坐标还是lidar系）
            // pointOri = laserCloudCornerLast->points[i];
            pcl::copyPoint(laserCloudCornerLast->points[i],pointOri);
            // 根据当前帧位姿，变换到世界坐标系（map系）下
            pointAssociateToMap(&pointOri, &pointSel);
            // 在局部角点map中查找当前角点相邻的5个角点
            kdtreeCornerFromMap->nearestKSearch(pointSel, 5, pointSearchInd, pointSearchSqDis);

            cv::Mat matA1(3, 3, CV_32F, cv::Scalar::all(0));
            cv::Mat matD1(1, 3, CV_32F, cv::Scalar::all(0));
            cv::Mat matV1(3, 3, CV_32F, cv::Scalar::all(0));
            
            // 要求距离都小于1m
            if (pointSearchSqDis[4] < 1.0) {
                // 计算5个点的均值坐标，记为中心点
                float cx = 0, cy = 0, cz = 0;
                for (int j = 0; j < 5; j++) {
                    cx += laserCloudCornerFromMap->points[pointSearchInd[j]].x;
                    cy += laserCloudCornerFromMap->points[pointSearchInd[j]].y;
                    cz += laserCloudCornerFromMap->points[pointSearchInd[j]].z;
                }
                cx /= 5; cy /= 5;  cz /= 5;

                // 计算协方差
                float a11 = 0, a12 = 0, a13 = 0, a22 = 0, a23 = 0, a33 = 0;
                for (int j = 0; j < 5; j++) {
                    // 计算点与中心点之间的距离
                    float ax = laserCloudCornerFromMap->points[pointSearchInd[j]].x - cx;
                    float ay = laserCloudCornerFromMap->points[pointSearchInd[j]].y - cy;
                    float az = laserCloudCornerFromMap->points[pointSearchInd[j]].z - cz;

                    a11 += ax * ax; a12 += ax * ay; a13 += ax * az;
                    a22 += ay * ay; a23 += ay * az;
                    a33 += az * az;
                }
                a11 /= 5; a12 /= 5; a13 /= 5; a22 /= 5; a23 /= 5; a33 /= 5;

                // 构建协方差矩阵
                matA1.at<float>(0, 0) = a11; matA1.at<float>(0, 1) = a12; matA1.at<float>(0, 2) = a13;
                matA1.at<float>(1, 0) = a12; matA1.at<float>(1, 1) = a22; matA1.at<float>(1, 2) = a23;
                matA1.at<float>(2, 0) = a13; matA1.at<float>(2, 1) = a23; matA1.at<float>(2, 2) = a33;

                // 特征值分解
                cv::eigen(matA1, matD1, matV1);

                // 如果最大的特征值相比次大特征值，大很多，认为构成了线，角点是合格的
                if (matD1.at<float>(0, 0) > 3 * matD1.at<float>(0, 1)) {
                    // 当前帧角点坐标（map系下）
                    float x0 = pointSel.x;
                    float y0 = pointSel.y;
                    float z0 = pointSel.z;
                    // 局部map对应中心角点，沿着特征向量（直线方向）方向，前后各取一个点
                    float x1 = cx + 0.1 * matV1.at<float>(0, 0);
                    float y1 = cy + 0.1 * matV1.at<float>(0, 1);
                    float z1 = cz + 0.1 * matV1.at<float>(0, 2);
                    float x2 = cx - 0.1 * matV1.at<float>(0, 0);
                    float y2 = cy - 0.1 * matV1.at<float>(0, 1);
                    float z2 = cz - 0.1 * matV1.at<float>(0, 2);

                    // area_012，也就是三个点组成的三角形面积*2，叉积的模|axb|=a*b*sin(theta)
                    float a012 = sqrt(((x0 - x1)*(y0 - y2) - (x0 - x2)*(y0 - y1)) * ((x0 - x1)*(y0 - y2) - (x0 - x2)*(y0 - y1)) 
                                    + ((x0 - x1)*(z0 - z2) - (x0 - x2)*(z0 - z1)) * ((x0 - x1)*(z0 - z2) - (x0 - x2)*(z0 - z1)) 
                                    + ((y0 - y1)*(z0 - z2) - (y0 - y2)*(z0 - z1)) * ((y0 - y1)*(z0 - z2) - (y0 - y2)*(z0 - z1)));
                    
                    // line_12，底边边长
                    float l12 = sqrt((x1 - x2)*(x1 - x2) + (y1 - y2)*(y1 - y2) + (z1 - z2)*(z1 - z2));
                    
                    // 两次叉积，得到点到直线的垂线段单位向量，x分量，下面同理
                    float la = ((y1 - y2)*((x0 - x1)*(y0 - y2) - (x0 - x2)*(y0 - y1)) 
                              + (z1 - z2)*((x0 - x1)*(z0 - z2) - (x0 - x2)*(z0 - z1))) / a012 / l12;

                    float lb = -((x1 - x2)*((x0 - x1)*(y0 - y2) - (x0 - x2)*(y0 - y1)) 
                               - (z1 - z2)*((y0 - y1)*(z0 - z2) - (y0 - y2)*(z0 - z1))) / a012 / l12;

                    float lc = -((x1 - x2)*((x0 - x1)*(z0 - z2) - (x0 - x2)*(z0 - z1)) 
                               + (y1 - y2)*((y0 - y1)*(z0 - z2) - (y0 - y2)*(z0 - z1))) / a012 / l12;

                    // 三角形的高，也就是点到直线距离
                    float ld2 = a012 / l12;

                    // 距离越大，s越小，是个距离惩罚因子（权重）
                    float s = 1 - 0.9 * fabs(ld2);

                    // 点到直线的垂线段单位向量
                    coeff.x = s * la;
                    coeff.y = s * lb;
                    coeff.z = s * lc;
                    // 点到直线距离
                    coeff.intensity = s * ld2;

                    if (s > 0.1) {
                        // 当前激光帧角点，加入匹配集合中
                        laserCloudOriCornerVec[i] = pointOri;
                        // 角点的参数
                        coeffSelCornerVec[i] = coeff;
                        laserCloudOriCornerFlag[i] = true;
                    }
                }
            }
        }
    }
    void surfOptimization()
    {
        transPointAssociateToMap=trans2Affine3f(transformTobeMapped);
        // 遍历当前帧平面点集合
        #pragma omp parallel for num_threads(numberOfCores)
        for (int i = 0; i < laserCloudSurfLast->size(); i++)
        {
            PointType coeff;
            pcl::PointXYZ pointOri, pointSel;
            std::vector<int> pointSearchInd;
            std::vector<float> pointSearchSqDis;
            
            // 平面点（坐标还是lidar系）
            pcl::copyPoint(laserCloudSurfLast->points[i],pointOri);
            // 根据当前帧位姿，变换到世界坐标系（map系）下
            pointAssociateToMap(&pointOri, &pointSel); 
            // 在局部平面点map中查找当前平面点相邻的5个平面点
            kdtreeSurfFromMap->nearestKSearch(pointSel, 5, pointSearchInd, pointSearchSqDis);

            Eigen::Matrix<float, 5, 3> matA0;
            Eigen::Matrix<float, 5, 1> matB0;
            Eigen::Vector3f matX0;

            matA0.setZero();
            matB0.fill(-1);
            matX0.setZero();

            // 要求距离都小于1m
            if (pointSearchSqDis[4] < 1.0) {
                for (int j = 0; j < 5; j++) {
                    matA0(j, 0) = laserCloudSurfFromMap->points[pointSearchInd[j]].x;
                    matA0(j, 1) = laserCloudSurfFromMap->points[pointSearchInd[j]].y;
                    matA0(j, 2) = laserCloudSurfFromMap->points[pointSearchInd[j]].z;
                }

                // 假设平面方程为ax+by+cz+1=0，这里就是求方程的系数abc，d=1
                matX0 = matA0.colPivHouseholderQr().solve(matB0);

                // 平面方程的系数，也是法向量的分量
                float pa = matX0(0, 0);
                float pb = matX0(1, 0);
                float pc = matX0(2, 0);
                float pd = 1;

                // 单位法向量
                float ps = sqrt(pa * pa + pb * pb + pc * pc);
                pa /= ps; pb /= ps; pc /= ps; pd /= ps;

                // 检查平面是否合格，如果5个点中有点到平面的距离超过0.2m，那么认为这些点太分散了，不构成平面
                bool planeValid = true;
                for (int j = 0; j < 5; j++) {
                    if (fabs(pa * laserCloudSurfFromMap->points[pointSearchInd[j]].x +
                             pb * laserCloudSurfFromMap->points[pointSearchInd[j]].y +
                             pc * laserCloudSurfFromMap->points[pointSearchInd[j]].z + pd) > 0.2) {
                        planeValid = false;
                        break;
                    }
                }

                // 平面合格
                if (planeValid) {
                    // 当前激光帧点到平面距离
                    float pd2 = pa * pointSel.x + pb * pointSel.y + pc * pointSel.z + pd;

                    float s = 1 - 0.9 * fabs(pd2) / sqrt(sqrt(pointSel.x * pointSel.x
                            + pointSel.y * pointSel.y + pointSel.z * pointSel.z));

                    // 点到平面垂线单位法向量（其实等价于平面法向量）
                    coeff.x = s * pa;
                    coeff.y = s * pb;
                    coeff.z = s * pc;
                    // 点到平面距离
                    coeff.intensity = s * pd2;

                    if (s > 0.1) {
                        // 当前激光帧平面点，加入匹配集合中
                        laserCloudOriSurfVec[i] = pointOri;
                        // 参数
                        coeffSelSurfVec[i] = coeff;
                        laserCloudOriSurfFlag[i] = true;
                    }
                }
            }
        }
    }
    void combineOptimizationCoeffs()
    {
        // 遍历当前帧角点集合，提取出与局部map匹配上了的角点
        for (int i = 0; i < laserCloudCornerLast->size(); ++i){
            if (laserCloudOriCornerFlag[i] == true){
                laserCloudOri->push_back(laserCloudOriCornerVec[i]);
                coeffSel->push_back(coeffSelCornerVec[i]);
            }
        }
        // 遍历当前帧平面点集合，提取出与局部map匹配上了的平面点
        for (int i = 0; i < laserCloudSurfLast->size(); ++i){
            if (laserCloudOriSurfFlag[i] == true){
                laserCloudOri->push_back(laserCloudOriSurfVec[i]);
                coeffSel->push_back(coeffSelSurfVec[i]);
            }
        }
        // 清空标记
        std::fill(laserCloudOriCornerFlag.begin(), laserCloudOriCornerFlag.end(), false);
        std::fill(laserCloudOriSurfFlag.begin(), laserCloudOriSurfFlag.end(), false);
    }
    bool LMOptimization(int iterCount)
    {
        // This optimization is from the original loam_velodyne by Ji Zhang, need to cope with coordinate transformation
        // lidar <- camera      ---     camera <- lidar
        // x = z                ---     x = y
        // y = x                ---     y = z
        // z = y                ---     z = x
        // roll = yaw           ---     roll = pitch
        // pitch = roll         ---     pitch = yaw
        // yaw = pitch          ---     yaw = roll

        // lidar -> camera
        float srx = sin(transformTobeMapped[1]);
        float crx = cos(transformTobeMapped[1]);
        float sry = sin(transformTobeMapped[2]);
        float cry = cos(transformTobeMapped[2]);
        float srz = sin(transformTobeMapped[0]);
        float crz = cos(transformTobeMapped[0]);

        // 当前帧匹配特征点数太少
        int laserCloudSelNum = laserCloudOri->size();
        if (laserCloudSelNum < 50) {
            return false;
        }

        cv::Mat matA(laserCloudSelNum, 6, CV_32F, cv::Scalar::all(0));
        cv::Mat matAt(6, laserCloudSelNum, CV_32F, cv::Scalar::all(0));
        cv::Mat matAtA(6, 6, CV_32F, cv::Scalar::all(0));
        cv::Mat matB(laserCloudSelNum, 1, CV_32F, cv::Scalar::all(0));
        cv::Mat matAtB(6, 1, CV_32F, cv::Scalar::all(0));
        cv::Mat matX(6, 1, CV_32F, cv::Scalar::all(0));

        PointType pointOri, coeff;

        // 遍历匹配特征点，构建Jacobian矩阵
        for (int i = 0; i < laserCloudSelNum; i++) {
            // lidar -> camera todo
            pointOri.x = laserCloudOri->points[i].y;
            pointOri.y = laserCloudOri->points[i].z;
            pointOri.z = laserCloudOri->points[i].x;
            // lidar -> camera
            coeff.x = coeffSel->points[i].y;
            coeff.y = coeffSel->points[i].z;
            coeff.z = coeffSel->points[i].x;
            coeff.intensity = coeffSel->points[i].intensity;
            // in camera
            float arx = (crx*sry*srz*pointOri.x + crx*crz*sry*pointOri.y - srx*sry*pointOri.z) * coeff.x
                      + (-srx*srz*pointOri.x - crz*srx*pointOri.y - crx*pointOri.z) * coeff.y
                      + (crx*cry*srz*pointOri.x + crx*cry*crz*pointOri.y - cry*srx*pointOri.z) * coeff.z;

            float ary = ((cry*srx*srz - crz*sry)*pointOri.x 
                      + (sry*srz + cry*crz*srx)*pointOri.y + crx*cry*pointOri.z) * coeff.x
                      + ((-cry*crz - srx*sry*srz)*pointOri.x 
                      + (cry*srz - crz*srx*sry)*pointOri.y - crx*sry*pointOri.z) * coeff.z;

            float arz = ((crz*srx*sry - cry*srz)*pointOri.x + (-cry*crz-srx*sry*srz)*pointOri.y)*coeff.x
                      + (crx*crz*pointOri.x - crx*srz*pointOri.y) * coeff.y
                      + ((sry*srz + cry*crz*srx)*pointOri.x + (crz*sry-cry*srx*srz)*pointOri.y)*coeff.z;
            // lidar -> camera
            matA.at<float>(i, 0) = arz;
            matA.at<float>(i, 1) = arx;
            matA.at<float>(i, 2) = ary;
            matA.at<float>(i, 3) = coeff.z;
            matA.at<float>(i, 4) = coeff.x;
            matA.at<float>(i, 5) = coeff.y;
            // 点到直线距离、平面距离，作为观测值
            matB.at<float>(i, 0) = -coeff.intensity;
        }

        cv::transpose(matA, matAt);
        matAtA = matAt * matA;
        matAtB = matAt * matB;
        // J^T·J·delta_x = -J^T·f 高斯牛顿
        cv::solve(matAtA, matAtB, matX, cv::DECOMP_QR);

        // 首次迭代，检查近似Hessian矩阵（J^T·J）是否退化，或者称为奇异，行列式值=0 todo
        if (iterCount == 0) {

            cv::Mat matE(1, 6, CV_32F, cv::Scalar::all(0));
            cv::Mat matV(6, 6, CV_32F, cv::Scalar::all(0));
            cv::Mat matV2(6, 6, CV_32F, cv::Scalar::all(0));

            cv::eigen(matAtA, matE, matV);
            matV.copyTo(matV2);

            isDegenerate = false;
            float eignThre[6] = {100, 100, 100, 100, 100, 100};
            for (int i = 5; i >= 0; i--) {
                if (matE.at<float>(0, i) < eignThre[i]) {
                    for (int j = 0; j < 6; j++) {
                        matV2.at<float>(i, j) = 0;
                    }
                    isDegenerate = true;
                } else {
                    break;
                }
            }
            matP = matV.inv() * matV2;
        }

        if (isDegenerate)
        {
            cv::Mat matX2(6, 1, CV_32F, cv::Scalar::all(0));
            matX.copyTo(matX2);
            matX = matP * matX2;
        }

        // 更新当前位姿 x = x + delta_x
        transformTobeMapped[0] += matX.at<float>(0, 0);
        transformTobeMapped[1] += matX.at<float>(1, 0);
        transformTobeMapped[2] += matX.at<float>(2, 0);
        transformTobeMapped[3] += matX.at<float>(3, 0);
        transformTobeMapped[4] += matX.at<float>(4, 0);
        transformTobeMapped[5] += matX.at<float>(5, 0);

        float deltaR = sqrt(
                            pow(pcl::rad2deg(matX.at<float>(0, 0)), 2) +
                            pow(pcl::rad2deg(matX.at<float>(1, 0)), 2) +
                            pow(pcl::rad2deg(matX.at<float>(2, 0)), 2));
        float deltaT = sqrt(
                            pow(matX.at<float>(3, 0) * 100, 2) +
                            pow(matX.at<float>(4, 0) * 100, 2) +
                            pow(matX.at<float>(5, 0) * 100, 2));

        // delta_x很小，认为收敛
        if (deltaR < 0.05 && deltaT < 0.05) {
            return true; 
        }
        return false; 
    }
    /**
     * 值约束
    */
    float constraintTransformation(float value, float limit)
    {
        if (value < -limit)
            value = -limit;
        if (value > limit)
            value = limit;

        return value;
    }

    void scan2MapOptimization()
    {

        // 迭代30次
        for (int iterCount = 0; iterCount < 30; iterCount++)
        {
            // 每次迭代清空特征点集合
            laserCloudOri->clear();
            coeffSel->clear();

            // 当前激光帧角点寻找局部map匹配点
            // 1、更新当前帧位姿，将当前帧角点坐标变换到map系下，在局部map中查找5个最近点，距离小于1m，且5个点构成直线（用距离中心点的协方差矩阵，特征值进行判断），则认为匹配上了
            // 2、计算当前帧角点到直线的距离、垂线的单位向量，存储为角点参数
            cornerOptimization();

            // 当前激光帧平面点寻找局部map匹配点
            // 1、更新当前帧位姿，将当前帧平面点坐标变换到map系下，在局部map中查找5个最近点，距离小于1m，且5个点构成平面（最小二乘拟合平面），则认为匹配上了
            // 2、计算当前帧平面点到平面的距离、垂线的单位向量，存储为平面点参数
            surfOptimization();

            // 提取当前帧中与局部map匹配上了的角点、平面点，加入同一集合
            combineOptimizationCoeffs();

            // scan-to-map优化
            // 对匹配特征点计算Jacobian矩阵，观测值为特征点到直线、平面的距离，构建高斯牛顿方程，迭代优化当前位姿，存transformTobeMapped
            if (LMOptimization(iterCount) == true)
                break;              
        }

    }

    void publishOdom()
    {
        // 发布激光里程计，odom等价map
        nav_msgs::Odometry laserOdometryROS;
        laserOdometryROS.header.stamp = timeLaserInfoStamp;
        laserOdometryROS.header.frame_id = odometryFrame;
        laserOdometryROS.child_frame_id = "odom_mapping";
        laserOdometryROS.pose.pose.position.x = transformTobeMapped[3];
        laserOdometryROS.pose.pose.position.y = transformTobeMapped[4];
        laserOdometryROS.pose.pose.position.z = transformTobeMapped[5];
        laserOdometryROS.pose.pose.orientation = tf::createQuaternionMsgFromRollPitchYaw(transformTobeMapped[0], transformTobeMapped[1], transformTobeMapped[2]);
        pubOdom.publish(laserOdometryROS);


        laserOdomIncremental=laserOdometryROS;
        if (isDegenerate)
            laserOdomIncremental.pose.covariance[0] = 1;
        else
            laserOdomIncremental.pose.covariance[0] = 0;
        
        pubLaserOdometryIncremental.publish(laserOdomIncremental);
    }
};







int main(int argc, char** argv)
{

    ros::init(argc, argv, "lio_sam");
    
    liauto lo;
    ROS_INFO("\033[1;32m----> paperData Started.\033[0m");
    
    ros::spin();
  


  return 0;
}