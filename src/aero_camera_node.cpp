/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (C) 2018 Intel Corporation
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the author nor other contributors may be
*     used to endorse or promote products derived from this software
*     without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

#include <algorithm>
#include <camera_info_manager/camera_info_manager.h>
#include <image_transport/image_transport.h>
#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/fill_image.h>

#include "CameraDevice.h"
#include "CameraDeviceAtomIsp.h"

class AeroCameraNode {
public:
  AeroCameraNode(ros::NodeHandle h, ros::NodeHandle hCam);
  ~AeroCameraNode();
  bool spin();

private:
  ros::NodeHandle mNH;
  ros::NodeHandle mNhCam;
  image_transport::CameraPublisher mImgPub;
  camera_info_manager::CameraInfoManager mCamInfoMgr;
  sensor_msgs::Image mImgMsg;
  CameraDevice *mCamDev;
  int start();
  int stop();
  int pubData();
  int readData(sensor_msgs::Image &image);
};

AeroCameraNode::AeroCameraNode(ros::NodeHandle nh, ros::NodeHandle nhCam)
    : mNH(nh), mNhCam(nhCam), mCamInfoMgr(nhCam) {

  ROS_INFO_STREAM("ROS Node aero_camera");

  // advertise the main image topic
  image_transport::ImageTransport it(mNhCam);
  mImgPub = it.advertiseCamera("image_raw", 1);

  mImgMsg.header.frame_id = "camera";

  mCamDev = new CameraDeviceAtomIsp("/dev/video2");
}

AeroCameraNode::~AeroCameraNode() {
  ROS_INFO_STREAM("~AeroCameraNode");

  stop();

  delete mCamDev;
}

int AeroCameraNode::start() {
  ROS_INFO_STREAM("start");
  int ret = 0;

  ret = mCamDev->init();
  if (ret) {
    ROS_ERROR("Error in init camera");
    return ret;
  }

  ret = mCamDev->setPixelFormat(CameraDevice::PIXEL_FORMAT_GREY);
  if (ret) {
    ROS_ERROR("Error in setting pixel format");
    return ret;
  }

  ret = mCamDev->start();
  if (ret) {
    ROS_ERROR("Error in start camera");
    mCamDev->uninit();
    return ret;
  }

  // Set camera info
  CameraInfo camInfo;
  ret = mCamDev->getInfo(camInfo);
  if (!ret) {
    if (!mCamInfoMgr.isCalibrated()) {
      mCamInfoMgr.setCameraName(camInfo.name);
      sensor_msgs::CameraInfo camera_info;
      camera_info.header.frame_id = mImgMsg.header.frame_id;
      camera_info.width = camInfo.width;
      camera_info.height = camInfo.height;
      mCamInfoMgr.setCameraInfo(camera_info);
    }
  }
  return ret;
}

int AeroCameraNode::stop() {
  ROS_INFO_STREAM("stop");
  int ret = 0;

  ret = mCamDev->stop();
  ret = mCamDev->uninit();
  return ret;
}

int AeroCameraNode::readData(sensor_msgs::Image &image) {

  int ret = 0;
  CameraFrame frame;
  ret = mCamDev->read(frame);
  if (!ret) {
    // Form a sensor_msgs::Image with the camera frame
    if (frame.pixFmt == CameraDevice::PIXEL_FORMAT_GREY)
      sensor_msgs::fillImage(image, "mono8", frame.height, frame.width,
                             frame.width, frame.buf);
    else
      ROS_ERROR("Unhandled Pixel Format");
  } else
    ROS_ERROR("Error in reading camera frame");

  return ret;
}

int AeroCameraNode::pubData() {
  // ROS_INFO_STREAM("pubData");
  int ret = 0;

  // Get the camera frame
  ret = readData(mImgMsg);
  if (ret) {
    return ret;
  }

  mImgMsg.header.stamp = ros::Time::now();

  // Get the camera info
  sensor_msgs::CameraInfoPtr ci(
      new sensor_msgs::CameraInfo(mCamInfoMgr.getCameraInfo()));
  ci->header.frame_id = mImgMsg.header.frame_id;
  ci->header.stamp = mImgMsg.header.stamp;

  // publish the image
  mImgPub.publish(mImgMsg, *ci);
  return ret;
}

bool AeroCameraNode::spin() {
  while (!ros::isShuttingDown())
  {
    if (start() == 0) {
      while (mNH.ok()) {
        if (pubData() < 0)
          ROS_WARN("No Camera Frame.");
        ros::spinOnce();
      }
    } else {
      // retry start
      usleep(1000000);
      ros::spinOnce();
    }
  }

  stop();

  return true;
}

int main(int argc, char **argv) {
  // Initialize the ROS system
  ros::init(argc, argv, "aero_camera");

  // Establish this program as a ROS node.
  ros::NodeHandle nh;
  ros::NodeHandle nhCam("camera");
  AeroCameraNode acn(nh, nhCam);
  acn.spin();

  return 0;
}
