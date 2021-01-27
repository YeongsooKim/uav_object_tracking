#ifndef __GENERATE_WAYPOINTS_H__
#define __GENERATE_WAYPOINTS_H__

#include "ros/ros.h"
// #include "airsim_ros_wrapper.h"
#include <ros/spinner.h>
#include "Eigen/Dense"
#include <deque>
#include <geometry_msgs/PoseArray.h>
#include "offboard_control/CarState.h"

using Vector3f = Eigen::Vector3f;


class GenerateWaypoints
{
private:
    // Node Handler
	ros::NodeHandle m_nh;

	
public:
    GenerateWaypoints();
    virtual ~GenerateWaypoints();

    void GetInputWaypoints();
    void GetDesiredWaypoints();

private:
    // subscriber
	ros::Subscriber m_car_state_sub;

	// publisher
	ros::Publisher m_input_waypoints_pub;
    ros::Publisher m_desired_waypoints_pub;

    // service client
    ros::ServiceClient m_desired_waypoints_srv_client;

    // param
    std::string m_vehicle_name_param;
    float m_x_offset_m_param;
    float m_z_offset_m_param;

private: // function
    void SetParam();

    void CarStateCallback(const offboard_control::CarState::ConstPtr &car_state);
    bool ConvertStateToWaypoints(geometry_msgs::Pose pose);

    geometry_msgs::Pose CreateDesiredWaypoint(geometry_msgs::Pose pose);
    float Distance (geometry_msgs::Point point1, geometry_msgs::Point point2);
    double Yaw(geometry_msgs::Pose pose);

private: // attribute
    std::vector<geometry_msgs::Pose> m_input_waypoints;
    std::vector<geometry_msgs::Pose> m_desired_waypoints;
};

#endif // __GENERATE_WAYPOINTS_H__