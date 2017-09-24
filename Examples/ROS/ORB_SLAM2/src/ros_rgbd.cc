/**
* This file is part of ORB-SLAM2.
*
* Copyright (C) 2014-2016 Raúl Mur-Artal <raulmur at unizar dot es> (University
* of Zaragoza)
* For more information see <https://github.com/raulmur/ORB_SLAM2>
*
* ORB-SLAM2 is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM2 is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with ORB-SLAM2. If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

#include <cv_bridge/cv_bridge.h>
#include <message_filters/subscriber.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <message_filters/time_synchronizer.h>
#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <geometry_msgs/PoseStamped.h>
#include <std_msgs/String.h>

#include <opencv2/core/core.hpp>

// Eigen
#include <Eigen/Core>
#include <Eigen/Geometry>

#include "../../../../include/System.h"

using namespace std;

// define a publisher to publish pose of camera
ros::Publisher pose_pub;
ros::Publisher depth_pub;
ros::Publisher rgb_pub;

class ImageGrabber
{
public:
  ImageGrabber(ORB_SLAM2::System* pSLAM) : mpSLAM(pSLAM)
  {
  }

  void GrabRGBD(const sensor_msgs::ImageConstPtr& msgRGB,
                const sensor_msgs::ImageConstPtr& msgD);

  ORB_SLAM2::System* mpSLAM;
};

int main(int argc, char** argv)
{
  ros::init(argc, argv, "RGBD");
  ros::start();

  if(argc != 3)
  {
    cerr << endl
         << "Usage: rosrun ORB_SLAM2 RGBD path_to_vocabulary path_to_settings"
         << endl;
    ros::shutdown();
    return 1;
  }

  // Create SLAM system. It initializes all system threads and gets ready to
  // process frames.
  ORB_SLAM2::System SLAM(argv[1], argv[2], ORB_SLAM2::System::RGBD, true);

  ImageGrabber igb(&SLAM);

  ros::NodeHandle nh;

  message_filters::Subscriber<sensor_msgs::Image> rgb_sub(
      nh, "/camera/rgb/image_raw", 1);
  message_filters::Subscriber<sensor_msgs::Image> depth_sub(
      nh, "camera/depth_registered/image_raw", 1);
  typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::Image,
                                                          sensor_msgs::Image>
      sync_pol;
  message_filters::Synchronizer<sync_pol> sync(sync_pol(10), rgb_sub,
                                               depth_sub);
  sync.registerCallback(boost::bind(&ImageGrabber::GrabRGBD, &igb, _1, _2));

  // initialize publisher
  pose_pub= nh.advertise<geometry_msgs::PoseStamped>("xtion/pose", 1);
  rgb_pub=nh.advertise<sensor_msgs::Image>("xtion/rgb",1);
  depth_pub=nh.advertise<sensor_msgs::Image>("xtion/depth",1);
  
  ros::spin();

  // Stop all threads
  SLAM.Shutdown();

  // Save camera trajectory
  SLAM.SaveKeyFrameTrajectoryTUM("KeyFrameTrajectory.txt");

  ros::shutdown();

  return 0;
}

void ImageGrabber::GrabRGBD(const sensor_msgs::ImageConstPtr& msgRGB,
                            const sensor_msgs::ImageConstPtr& msgD)
{
  // Copy the ros image message to cv::Mat.
  cv_bridge::CvImageConstPtr cv_ptrRGB;
  try
  {
    cv_ptrRGB= cv_bridge::toCvShare(msgRGB);
  }
  catch(cv_bridge::Exception& e)
  {
    ROS_ERROR("cv_bridge exception: %s", e.what());
    return;
  }

  cv_bridge::CvImageConstPtr cv_ptrD;
  try
  {
    cv_ptrD= cv_bridge::toCvShare(msgD);
  }
  catch(cv_bridge::Exception& e)
  {
    ROS_ERROR("cv_bridge exception: %s", e.what());
    return;
  }

  cv::Mat pose= mpSLAM->TrackRGBD(cv_ptrRGB->image, cv_ptrD->image,
                                  cv_ptrRGB->header.stamp.toSec());

  /*convert opencv Mat to poseStamped*/
  Eigen::Matrix3f rotation;
  rotation << pose.at<float>(0, 0), pose.at<float>(0, 1), pose.at<float>(0, 2),
      pose.at<float>(1, 0), pose.at<float>(1, 1), pose.at<float>(1, 2),
      pose.at<float>(2, 0), pose.at<float>(2, 1), pose.at<float>(2, 2);
  Eigen::Quaternionf q;
  q= Eigen::Quaternionf(rotation);

  geometry_msgs::PoseStamped psd;
  psd.pose.position.x=pose.at<float>(0,3);
  psd.pose.position.y=pose.at<float>(1,3);
  psd.pose.position.z=pose.at<float>(2,3);
  psd.pose.orientation.w=q.w();
  psd.pose.orientation.x=q.x();
  psd.pose.orientation.y=q.y();
  psd.pose.orientation.z=q.z();
  psd.header.frame_id="odom";

  /*transfer rgb and depth to xtion node*/
  sensor_msgs::Image rgb_image;
  rgb_image.data=msgRGB->data;
  rgb_image.encoding=msgRGB->encoding;
  rgb_image.header.frame_id="odom";
  rgb_image.height=msgRGB->height;
  rgb_image.is_bigendian=msgRGB->is_bigendian;
  rgb_image.step=msgRGB->step;
  rgb_image.width=msgRGB->width;

  sensor_msgs::Image depth_image;
  depth_image.data=msgD->data;
  depth_image.encoding=msgD->encoding;
  depth_image.header.frame_id="odom";
  depth_image.height=msgD->height;
  depth_image.is_bigendian=msgD->is_bigendian;
  depth_image.step=msgD->step;
  depth_image.width=msgD->width;

  /*publish pose and image to xtion*/
  ros::Time current;
  current = ros::Time::now();
  psd.header.stamp=current;
  rgb_image.header.stamp=current;
  depth_image.header.stamp=current;

  pose_pub.publish(psd);
  rgb_pub.publish(rgb_image);
  depth_pub.publish(depth_image);
}
