#include "control/offb.h"

namespace mavros {
    
Offboard::Offboard() : 
    m_tfListener(m_tfBuffer),
    m_last_request_time(ros::Time::now()),
    m_setpoint_pub_interval_param(NAN)
{
    InitFlag();
    if (!GetParam()) ROS_ERROR_STREAM("Fail GetParam");
    InitROS();
    InitClient();

    m_local_setpoint.header.frame_id = "map";
    m_global_setpoint.header.frame_id = "map";
}

Offboard::~Offboard()
{}

bool Offboard::InitFlag()
{
    m_is_global = false;
    m_is_debugging = false;

    return true;
}

bool Offboard::GetParam()
{
    m_nh.getParam("offb_node/setpoint_pub_interval", m_setpoint_pub_interval_param);
    m_nh.getParam("offb_node/is_debug_mode", m_is_debug_mode_param);

    if (__isnan(m_setpoint_pub_interval_param)) { ROS_ERROR_STREAM("m_setpoint_pub_interval_param is NAN"); return false; }

    m_is_debugging = m_is_debug_mode_param;

    return true;
}

bool Offboard::InitROS()
{
    // package, node, topic name
    std::string node_name_with_namespace = ros::this_node::getName();

    // Initialize subscriber
    m_state_sub = m_nh.subscribe<mavros_msgs::State>("/mavros/state", 10, boost::bind(&Offboard::StatusCallback, this, _1));
    m_target_waypoints_sub = m_nh.subscribe<uav_msgs::TargetWaypoints>
            ("/control/generate_waypoints_node/target_waypoints", 10, boost::bind(&Offboard::TargetWaypointsCallback, this, _1));
    m_chatter_sub = 
        m_nh.subscribe<uav_msgs::Chat>("/control/char_pub_node/chatter", 10, boost::bind(&Offboard::ChatterCallback, this, _1));

    // Initialize publisher
    m_local_pose_pub = m_nh.advertise<geometry_msgs::PoseStamped>("/mavros/setpoint_position/local", 10);
    m_global_pose_pub = m_nh.advertise<geographic_msgs::GeoPoseStamped>("/mavros/setpoint_position/global", 10);
    m_velocity_pub = m_nh.advertise<geometry_msgs::Twist>("/mavros/setpoint_velocity/cmd_vel_unstamped", 10);
    m_offboard_state_pub = m_nh.advertise<uav_msgs::OffboardState>(node_name_with_namespace + "/offboard_state", 1000);

    // Initialize service client
    m_arming_serv_client = m_nh.serviceClient<mavros_msgs::CommandBool>("/mavros/cmd/arming");
    m_set_mode_serv_client = m_nh.serviceClient<mavros_msgs::SetMode>("/mavros/set_mode");
    
    // Time callback
    m_timer = m_nh.createTimer(ros::Duration(m_setpoint_pub_interval_param), &Offboard::OffboardTimeCallback, this);

    return true;
}

bool Offboard::InitClient()
{
    ros::Rate rate(1/m_setpoint_pub_interval_param);
    if (m_setpoint_pub_interval_param == 0.0) return false;

    // wait for FCU
    while(ros::ok() && !m_current_status.connected){
        ros::spinOnce();
        rate.sleep();
    }

    return true;
}

void Offboard::OffboardTimeCallback(const ros::TimerEvent& event)
{
    if (m_is_debugging){
        if (m_debugging_msg == "OFFBOARD"){
            OffboardReConnection();
        }
        // Just hovering in all cases, not just MANUAL mode
        else {
        }
    }
    else {
        if (m_current_status.mode == "OFFBOARD"){
            OffboardReConnection();
        }
        // Just hovering in all cases, not just MANUAL mode
        else {
        }
    }
    Publish();
}

void Offboard::StatusCallback(const mavros_msgs::State::ConstPtr &state_ptr)
{
    m_current_status = *state_ptr;
}

void Offboard::TargetWaypointsCallback(const uav_msgs::TargetWaypoints::ConstPtr &target_wp_ptr)
{
    m_is_global = target_wp_ptr->is_global;
    if (m_is_global){
        geographic_msgs::GeoPath waypoints = target_wp_ptr->global;
        if (waypoints.poses.size() < 1){
            ROS_ERROR_STREAM("[offb] Waypoints is empty");
        }
        else {
            geographic_msgs::GeoPoseStamped waypoint = waypoints.poses.back();

            if (m_utils.IsNan(waypoint.pose.position)){
                ROS_ERROR_STREAM("[offb] Waypoint contains nan");
            }
            else{
                m_global_setpoint.pose = waypoint.pose;
                m_vel_setpoint = target_wp_ptr->velocity;
            }
        }

    }
    else {
        geometry_msgs::PoseArray waypoints = target_wp_ptr->local;
        if (waypoints.poses.size() < 1){
            ROS_ERROR_STREAM("[offb] Waypoints is empty");
        }
        else {
            geometry_msgs::PoseStamped waypoint;
            waypoint.header = waypoints.header;
            waypoint.pose = waypoints.poses.back();

            if (m_utils.IsNan(waypoint.pose.position)){
                ROS_ERROR_STREAM("[offb] Waypoint contains nan");
            }
            else {
                if (m_local_setpoint.header.frame_id != waypoint.header.frame_id){

                    geometry_msgs::TransformStamped transformStamped;
                    geometry_msgs::Pose transformed_pose;
                    try{
                        transformStamped = m_tfBuffer.lookupTransform(m_local_setpoint.header.frame_id, waypoint.header.frame_id, ros::Time(0));
                        tf2::doTransform(waypoint.pose, transformed_pose, transformStamped);
                    }
                    catch (tf2::TransformException &ex) {
                        ROS_WARN("%s", ex.what());
                        ros::Duration(1.0).sleep();
                    }
                    m_local_setpoint.pose = transformed_pose;
                }
                else{
                    m_local_setpoint.pose = waypoint.pose;
                }
                m_vel_setpoint = target_wp_ptr->velocity;
            }
        }
    }
}

void Offboard::ChatterCallback(const uav_msgs::Chat::ConstPtr &chat_ptr)
{
    if (chat_ptr->msg == "offboard") {
        m_debugging_msg = "OFFBOARD";
        m_current_status.mode = "OFFBOARD";
    }
    else if (chat_ptr->msg == "manual"){
        m_debugging_msg = "MANUAL";
        m_current_status.mode = "MANUAL";
    }
    else if (chat_ptr->msg == "debugging") m_is_debugging = true;
    else if (chat_ptr->msg == "rc") m_is_debugging = false;
}

void Offboard::OffboardReConnection()
{
    mavros_msgs::SetMode offb_set_mode;
    offb_set_mode.request.custom_mode = "OFFBOARD";

    mavros_msgs::CommandBool arm_cmd;
    arm_cmd.request.value = true;

    if( m_current_status.mode != "OFFBOARD" &&
        (ros::Time::now() - m_last_request_time > ros::Duration(5.0))){
        if(m_set_mode_serv_client.call(offb_set_mode) &&
            offb_set_mode.response.mode_sent){
            ROS_INFO("Offboard enabled");
        }
        m_last_request_time = ros::Time::now();
    } else {
        if(!m_current_status.armed &&
            (ros::Time::now() - m_last_request_time > ros::Duration(5.0))){
            if( m_arming_serv_client.call(arm_cmd) &&
                arm_cmd.response.success){
                ROS_INFO("Vehicle armed");
            }
            m_last_request_time = ros::Time::now();
        }
    }
}

void Offboard::Publish()
{
    if (m_is_global) m_global_pose_pub.publish(m_global_setpoint);
    else if (!m_is_global) m_local_pose_pub.publish(m_local_setpoint);
    m_velocity_pub.publish(m_vel_setpoint);

    uav_msgs::OffboardState offboard_state_msg;
    offboard_state_msg.px4_mode = m_current_status.mode;
    offboard_state_msg.is_debug_mode = m_is_debugging;
    m_offboard_state_pub.publish(offboard_state_msg);
}
}