#include "utility.h"
#include <tf2/LinearMath/Matrix3x3.h>
using namespace std;

class Drive
{
public:
    ros::NodeHandle nh;
    ros::Subscriber subLane;
    ros::Subscriber subPose;
    pcl::PointCloud<pcl::PointXY> centerLanePoints;
    pcl::KdTreeFLANN<pcl::PointXY> centerLaneKdtree;
    //当前车离centerlan哪几个点最近
    std::vector<int> searchIndex;
    std::vector<float> searchDistance;
    float pose[6];
    float progTime=0.1;
    float minProgDist=1.0;
    bool laneLoad=false;
    int currentRoadSegemnt=0;
    int numSegment=50;
    vector<pcl::PointCloud<pcl::PointXY>> roadSegment;
    
    
    Drive()
    {
        
        subPose=nh.subscribe<nav_msgs::Odometry>("/currPose", 1, &Drive::poseHandler, this);
        subLane=nh.subscribe<sensor_msgs::PointCloud2>("/centerLane",1,&Drive::centerLaneHandler,this);
        
        // ROS_INFO("\033[1;32m----> Drive Started.\033[0m");
    }
    
    void getCenterLaneNearPoints()
    {
        
    }

    void poseHandler(const nav_msgs::Odometry::ConstPtr& msg)
    {
        // cout<<"?"<<endl;
        // printf("resived");
        tf2::Quaternion quat(msg->pose.pose.orientation.x, msg->pose.pose.orientation.y,
                     msg->pose.pose.orientation.z, msg->pose.pose.orientation.w);
        tf2::Matrix3x3 matrix(quat);
        double roll, pitch, yaw;
        matrix.getRPY(roll, pitch, yaw);
        pose[0]=roll;
        pose[1]=pitch;
        pose[2]=yaw;
        pose[3]=msg->pose.pose.position.x;
        pose[4]=msg->pose.pose.position.y;
        pose[5]=msg->pose.pose.position.z;

        // cout<<"x:"<<pose[3]<<"y:"<<pose[4]<<endl;
        if(laneLoad)
        {
            double steer=getSteering(roadSegment[currentRoadSegemnt],pose[3],pose[4]);
            printf("x:%6f,y:%6f,steer:%6f,currentSegment:%2d\r\n",pose[3],pose[4],steer,currentRoadSegemnt);
        }
    }
    void centerLaneHandler(const sensor_msgs::PointCloud2::ConstPtr& msg)
    {
        pcl::fromROSMsg(*msg,centerLanePoints);
        if(centerLanePoints.points.size()>0)
        {
            centerLaneKdtree.setInputCloud(boost::make_shared<pcl::PointCloud<pcl::PointXY>>(centerLanePoints));
            
            int numPreSegment=centerLanePoints.points.size()/numSegment;
            int j=0;
            for(int i=0;i<numSegment;i++)
            {
                pcl::PointCloud<pcl::PointXY> tempP;
                if(i==numSegment-1)
                {
                    for(;j<centerLanePoints.points.size();j++)
                    {
                        tempP.points.push_back(centerLanePoints.points[j]);
                    }
                }
                else
                {
                    for(j=i*numPreSegment;j<(i+1)*numPreSegment;j++)
                    {
                        tempP.points.push_back(centerLanePoints.points[j]);
                    }
                }
                
                roadSegment.push_back(tempP);
            }
            cout<<"Lane loaded! "<<endl;
            laneLoad=true;
        }
    }
    float getVehicleSpeed()
    {
        return 1.0;
    }
    double getSteering(const pcl::PointCloud<pcl::PointXY>& targetPath, const float posex,const float posey)
    {

        std::vector<float> pts;
        for (size_t i = 0; i < targetPath.points.size(); ++i)
        {
            pts.push_back(pow((posex - (float)targetPath.points[i].x), 2) + pow((posey - (float)targetPath.points[i].y), 2));
        }

        size_t index = std::min_element(pts.begin(), pts.end()) - pts.begin();
        size_t forwardIndex = 0;
        
        float mainVehicleSpeed=getVehicleSpeed();
        float progDist = mainVehicleSpeed * progTime > minProgDist ? mainVehicleSpeed * progTime : minProgDist;

        for (; index < targetPath.size(); ++index)
        {
            forwardIndex = index;
            float distance = sqrtf(((float)pow(targetPath.points[index].x - posex, 2) + pow((float)targetPath.points[index].y - posey, 2)));
            if (distance >= progDist)
            {
                break;
            }
        }
        // printf("targetPath.points.size():%d,index:%d\r\n",targetPath.points.size(),index);
        if(targetPath.points.size()-index<3)
        {
            currentRoadSegemnt++;
            if(currentRoadSegemnt>=(numSegment-1))//final segemnt end
            {
                currentRoadSegemnt=(numSegment-1);
                printf("end of the road\r\n");
            }
        }
        double psi = (double)pose[2];
        double alfa = atan2(targetPath.points[forwardIndex].y - posey, targetPath[forwardIndex].x - posex) - psi;
        double ld = sqrt(pow(targetPath[forwardIndex].y - posey, 2) + pow(targetPath[forwardIndex].x - posex, 2));
        double steering = -atan2(2. * (1.3 + 1.55) * sin(alfa), ld) * 36. / (7. * M_PI);
        
        return steering;
    }
};

int main(int argc, char** argv)
{

    ros::init(argc, argv, "lio_sam");
    
    
    Drive drive;

    ROS_INFO("\033[1;32m----> Drive Started.\033[0m");
    
    
    ros::spin();
  


    return 0;
}