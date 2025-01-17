#include <fstream>
#include <iostream>

#include <Eigen/Core>
#include <mav_msgs/conversions.h>
#include <mav_msgs/default_topics.h>
#include <std_msgs/Bool.h>
#include <std_msgs/Empty.h>
#include <ros/ros.h>
#include <trajectory_msgs/MultiDOFJointTrajectory.h>
#include <geometry_msgs/Twist.h>
#include <geometry_msgs/Transform.h>
#include <geometry_msgs/Vector3.h>
#include <unistd.h>



#include <nav_msgs/Odometry.h>
#include <sensor_msgs/Joy.h>

#include <eigen_conversions/eigen_msg.h>

//#include "rotors_joy_interface/fake_driver.h"

#define CLAMP(x, l, h) (((x) > (h)) ? (h) : (((x) < (l)) ? (l) : (x)))


// Use the structure definitions from the rotors_joy_interface 
// #include "rotors_joy_interface/joy.h"

#define DEG2RAD(x) ((x) / 180.0 * M_PI)

void traj_callback(
      const trajectory_msgs::MultiDOFJointTrajectoryConstPtr& trajectory_reference_msg);
void joy_callback(const sensor_msgs::JoyConstPtr& msg);
void odom_callback(const nav_msgs::OdometryConstPtr& msg);
void joy_enable_callback(const std_msgs::Bool& msg);
void TakeoffCallback(const std_msgs::Empty& msg);
void LandCallback(const std_msgs::Empty& msg);
void StopCallback(const std_msgs::Empty& msg);
void MoveCallback(const geometry_msgs::Twist& bebop_twist_);
void reset_pose_callback(const std_msgs::Empty& msg);
void StopMav();
void ResetTwist(geometry_msgs::Twist& t);



ros::Publisher trajectory_pub;
ros::Subscriber odom_sub, joy_sub, joy_enable_sub, takeoff_sub, land_sub, stop_sub, move_sub, traj_sub, reset_pose_sub;
nav_msgs::Odometry odom_msg;
sensor_msgs::Joy joy_msg;
geometry_msgs::Twist prev_bebop_twist_;

ros::Time prev_twist_stamp_ = ros::Time(0);

bool joy_msg_ready = false;
bool joy_enable = true;
bool init_pose_set = false;

// Cette variable permettra le reset propre de la simulation
bool init_takeoff = true;

bool start = false;
bool move = false;

bool takeoff = false;
bool emergency = false;
bool land = false;

static const double eps = 1.0e-6;

float linear_x = 0.0;
float linear_y = 0.0;
float linear_z = 0.0;
float angular_x = 0.0;
float angular_y = 0.0;
float angular_z = 0.0;
float loc = 0.0;

double param_cmd_vel_timeout_ = 0.2;

int axis_roll  , axis_pitch, 
    axis_thrust, axis_yaw;
int axis_direction_roll, 
    axis_direction_pitch, 
    axis_direction_thrust, 
    axis_direction_yaw;
 
double max_vel,
       max_yawrate;





int main(int argc, char** argv) {

  ros::init(argc, argv, "fake_driver");

  ros::NodeHandle nh("~");
  // Continuously publish waypoints.
  trajectory_pub = nh.advertise<trajectory_msgs::MultiDOFJointTrajectory>(
                    mav_msgs::default_topics::COMMAND_TRAJECTORY, 100);

  traj_sub = nh.subscribe("command/trajectory", 1, &traj_callback);

 

  // Subscribe to message for enabling/disabling joystick control
  joy_enable_sub = nh.subscribe("joy_enable", 10, &joy_enable_callback);

  // Subscribe to joystick messages
  joy_sub = nh.subscribe("joy", 10, &joy_callback);

  // Subscribe to gt odometry messages
  odom_sub = nh.subscribe("odom", 10, &odom_callback);



  takeoff_sub = nh.subscribe("takeoff", 10, &TakeoffCallback);
  land_sub = nh.subscribe("land", 10, &LandCallback);
  stop_sub = nh.subscribe("reset", 10, &StopCallback);
  move_sub = nh.subscribe("cmd_vel", 10, &MoveCallback);
  reset_pose_sub = nh.subscribe("reset_pose", 10, &reset_pose_callback);



  ROS_INFO("Started velocity_control_with_joy.");

  nh.param("axis_roll_"  , axis_roll, 3);
  nh.param("axis_pitch_" , axis_pitch, 2);
  nh.param("axis_yaw_"   , axis_yaw, 0);
  nh.param("axis_thrust_", axis_thrust, 1);

  nh.param("axis_direction_roll_"  , axis_direction_roll, -1);
  nh.param("axis_direction_pitch_" , axis_direction_pitch, 1);
  nh.param("axis_direction_yaw_"   , axis_direction_yaw, 1);
  nh.param("axis_direction_thrust_", axis_direction_thrust, -1);
 
  nh.param("max_vel", max_vel, 1.0);
  nh.param("max_yawrate", max_yawrate, DEG2RAD(45));
  
  ros::spin();
}


void traj_callback(
      const trajectory_msgs::MultiDOFJointTrajectoryConstPtr& msg){

      loc = msg->points[0].transforms[0].translation.z;
      // ROS_INFO("loc = %f", loc);
}

void reset_pose_callback(const std_msgs::Empty& msg){
  // On créer un topic pour reset init_pose_set. Sans ca, lors du reset world, le drone voudra retourenr a sa position avant le reset

  init_pose_set = false;
  init_takeoff = false;
  takeoff = false;
  start = false;
  land = true;
}

void StopCallback(const std_msgs::Empty& msg){
  emergency = true;
  takeoff = false;
  start = false;
  ROS_INFO("STOP");
} 

void LandCallback(const std_msgs::Empty& msg){
  land = true;
  takeoff = false;
  start = false;
  ROS_INFO("LAND");

} 

void TakeoffCallback(const std_msgs::Empty& msg) 
{
  if (start){
    return;
  }
  takeoff = true;
  emergency = false;
  land = false;
  start = true;
  // ROS_INFO("TAKEOFF");
}


bool CompareTwists(const geometry_msgs::Twist& lhs, const geometry_msgs::Twist& rhs)
{
  return (fabs(lhs.linear.x - rhs.linear.x) < eps) &&
      (fabs(lhs.linear.y - rhs.linear.y) < eps) &&
      (fabs(lhs.linear.z - rhs.linear.z) < eps) &&
      (fabs(lhs.angular.x - rhs.angular.x) < eps) &&
      (fabs(lhs.angular.y - rhs.angular.y) < eps) &&
      (fabs(lhs.angular.z - rhs.angular.z) < eps);
}

void ResetTwist(geometry_msgs::Twist& t)
{
  t.linear.x = 0.0;
  t.linear.y = 0.0;
  t.linear.z = 0.0;
  t.angular.x = 0.0;
  t.angular.y = 0.0;
  t.angular.z = 0.0;
}



void MoveCallback(const geometry_msgs::Twist& bebop_twist_){
  if (takeoff || !start){
    return;
  }

  bool is_bebop_twist_changed = false;

  is_bebop_twist_changed = !CompareTwists(bebop_twist_, prev_bebop_twist_);
  prev_twist_stamp_ = ros::Time::now();
  prev_bebop_twist_ = bebop_twist_;

  // Youcef's changes
  if (is_bebop_twist_changed)
  {
    linear_x = CLAMP(bebop_twist_.linear.x, -1.0, 1.0);
    linear_y = CLAMP(bebop_twist_.linear.y, -1.0, 1.0);
    linear_z = CLAMP(bebop_twist_.linear.z, -1.0, 1.0);
    // angular_x = msg.angular.x;
    // angular_y = msg.angular.y;
    angular_z = CLAMP(bebop_twist_.angular.z, -1.0, 1.0);
  }
} 

void joy_enable_callback(const std_msgs::Bool& msg)
{
  ROS_INFO("Changing joy_enable to %d", msg.data);
  joy_enable = msg.data;

  // If we are re-enabling the joystick the init_position might have changed, so make sure it's updated
  if (joy_enable == true) {
    init_pose_set = false;
  }
}

void joy_callback(const sensor_msgs::JoyConstPtr& msg){
  joy_msg = *msg;
  joy_msg_ready = true;
}

void odom_callback(const nav_msgs::OdometryConstPtr& msg){
  
 // if(joy_msg_ready == false || joy_enable == false)
 //   return;
 
  odom_msg = *msg;
  static ros::Time prev_time = ros::Time::now();
  static Eigen::Vector3d init_position;
  static double init_yaw;


  if(init_pose_set == false){

   
    Eigen::Affine3d eigen_affine;
    tf::poseMsgToEigen(odom_msg.pose.pose, eigen_affine);
    init_position = eigen_affine.matrix().topRightCorner<3, 1>();

    // std::cout << "init position: " << init_position << std::endl;

    //init_yaw = eigen_affine.matrix().topLeftCorner<3, 3>().eulerAngles(0, 1, 2)(2);
    // Direct cosine matrix, a.k.a. rotation matrix

    Eigen::Matrix3d dcm = eigen_affine.matrix().topLeftCorner<3, 3>();

    // std::cout << "dcm: " << dcm << std::endl;

    double phi = asin(dcm(2, 1));
    double cosphi = cos(phi);
    double the = atan2(-dcm(2, 0) / cosphi, dcm(2, 2) / cosphi);
    double psi = atan2(-dcm(0, 1) / cosphi, dcm(1, 1) / cosphi);
    init_yaw = psi;
    init_pose_set = true;
  }

    geometry_msgs::Twist zero_twist;
    ResetTwist(zero_twist);   

    const ros::Time t_now = ros::Time::now();
    // cmd_vel safety
    if ( !CompareTwists(prev_bebop_twist_, zero_twist) &&
        ((t_now - prev_twist_stamp_).toSec() > param_cmd_vel_timeout_)
        )
    {
      ROS_WARN("Input cmd_vel timeout, reseting cmd_vel ...");
      ResetTwist(prev_bebop_twist_);
      linear_x = 0;
      linear_y = 0;
      linear_z = 0;
      angular_z = 0;
    }

  // from joystik
  double roll = linear_y * axis_direction_roll;
  double pitch  = linear_x  * axis_direction_pitch;
  double thrust  = linear_z  * axis_direction_thrust;
  double yaw  = angular_z  * axis_direction_yaw;

  double dt = (ros::Time::now() - prev_time).toSec();
  prev_time = ros::Time::now();

  //double yaw    = joy_msg.axes[axis_yaw]    * axis_direction_yaw;
  //double roll   = joy_msg.axes[axis_roll]   * axis_direction_roll;
  //double pitch  = joy_msg.axes[axis_pitch]  * axis_direction_pitch;
  //double thrust = joy_msg.axes[axis_thrust] * axis_direction_thrust;


  if (emergency){
      static trajectory_msgs::MultiDOFJointTrajectory trajectory_msg;
      Eigen::Vector3d desired_dposition( cos(init_yaw) * pitch - sin(init_yaw) * roll, 
                                      sin(init_yaw) * pitch + cos(init_yaw) * roll, 
                                      -10.0);
      init_position += desired_dposition * max_vel * dt;

      init_yaw = init_yaw + max_yawrate * yaw * dt;

      trajectory_msg.header.stamp = ros::Time::now();
      trajectory_msg.header.seq++;

      mav_msgs::msgMultiDofJointTrajectoryFromPositionYaw(init_position,
          init_yaw, &trajectory_msg);

      trajectory_msg.points[0].time_from_start = ros::Duration(0.01);
      trajectory_pub.publish(trajectory_msg);    
    if (loc < 0.0){
    takeoff = false;
    start = false;        
    emergency = false;
    }
  }


  if (land){
    static trajectory_msgs::MultiDOFJointTrajectory trajectory_msg;

  
    // for loop avec z = 1??
    Eigen::Vector3d desired_dposition( cos(init_yaw) * pitch - sin(init_yaw) * roll, 
                                    sin(init_yaw) * pitch + cos(init_yaw) * roll, 
                                    -1.0);
    init_position += desired_dposition * max_vel * dt;

    init_yaw = init_yaw + max_yawrate * yaw * dt;

    trajectory_msg.header.stamp = ros::Time::now();
    trajectory_msg.header.seq++;

    mav_msgs::msgMultiDofJointTrajectoryFromPositionYaw(init_position,
        init_yaw, &trajectory_msg);

    trajectory_msg.points[0].time_from_start = ros::Duration(0.01);
    trajectory_pub.publish(trajectory_msg);
    if (loc < 0.0){
      start = false;
      land = false;
      takeoff = false;
    }
  }
  if (takeoff){
    static trajectory_msgs::MultiDOFJointTrajectory trajectory_msg;
    Eigen::Vector3d desired_dposition(cos(init_yaw) * pitch - sin(init_yaw) * roll, 
                                    sin(init_yaw) * pitch + cos(init_yaw) * roll, 
                                    1.0);
    init_position += desired_dposition * max_vel * dt;

    init_yaw = init_yaw + max_yawrate * yaw * dt;
    trajectory_msg.header.stamp = ros::Time::now();
    trajectory_msg.header.seq++;

    mav_msgs::msgMultiDofJointTrajectoryFromPositionYaw(init_position,
        init_yaw, &trajectory_msg);

    trajectory_msg.points[0].time_from_start = ros::Duration(0.01);
    trajectory_pub.publish(trajectory_msg);

    

    if (loc > 1.0){
      // std::cout << "FINISHED TAKEOFF YEAH : " << loc <<  std::endl;
      start = true;
      takeoff = false;
    }

  }

  if (start){
    static trajectory_msgs::MultiDOFJointTrajectory trajectory_msg;
    Eigen::Vector3d desired_dposition( cos(init_yaw) * pitch - sin(init_yaw) * roll, 
                                      sin(init_yaw) * pitch + cos(init_yaw) * roll, 
                                      thrust);

    // std::cout << "=======start=======" << std::endl;

    init_position += desired_dposition * max_vel * dt;

    init_yaw = init_yaw + max_yawrate * yaw * dt;
    trajectory_msg.header.stamp = ros::Time::now();
    trajectory_msg.header.seq++;

    mav_msgs::msgMultiDofJointTrajectoryFromPositionYaw(init_position,
        init_yaw, &trajectory_msg);

    trajectory_msg.points[0].time_from_start = ros::Duration(1.0);
    trajectory_pub.publish(trajectory_msg);
  }
}

