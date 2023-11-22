#include <memory>
#include <math.h>
#include <cmath>

#include "agrorob_driver/agrorob_interface.hpp"
#include "agrorob_driver/velocity_controller.hpp"


#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "can_msgs/msg/frame.hpp"
#include "sensor_msgs/msg/joy.hpp"
#include "geometry_msgs/msg/twist.hpp"

#include "agrorob_msgs/msg/engine_state.hpp"
#include "agrorob_msgs/msg/failure_state.hpp"
#include "agrorob_msgs/msg/tool_state.hpp"
#include "agrorob_msgs/msg/remote_state.hpp"
#include "agrorob_msgs/msg/robot_state.hpp"
#include "agrorob_msgs/msg/logs.hpp"


// #include "agrorob_msgs/msg/mode_control.hpp"
// #include "agrorob_msgs/msg/robot_control.hpp"
// #include "agrorob_msgs/msg/sprayer_control.hpp"
// #include "agrorob_msgs/msg/tool_control.hpp"

using namespace std;
using std::placeholders::_1;
 
namespace agrorob_interface
{
  AgrorobInterface::AgrorobInterface() : Node("agrorob_interface"), agrorob_ready_to_move(false), initializing(true), use_velocity_controller(false), 
  rpm_to_rad_s(0.10472), engine_rotation_rpm(140), ii(0), wheelR(0.387), refVelocity(0.0), refRotationVel(0.0), refAcceleration(0.0),
  never_saw_joy_msg(true), never_saw_can_msg(true), last_joy_msg_time_(0), last_can_msg_time_(0), last_control_mode_change(0), last_engine_rpm_change(0),
  joy_connectivity_status_holder(0),  can_connectivity_status_holder(0), connectivity_status_holder(0), velocity(100.0)
  
  {
    joy_msg_ = std::make_shared<sensor_msgs::msg::Joy>();

    can_id1 = initialize_can_frame();
    can_id25 = initialize_can_frame();
    can_id1.id = 1;
    can_id25.id = 25; 

    raw_can_sub_ = this->create_subscription<can_msgs::msg::Frame>("from_can_bus", 10, [this](const can_msgs::msg::Frame::SharedPtr can_msg){
      can_msg_ = can_msg; never_saw_can_msg = false; last_can_msg_time_ = this->get_clock()->now(); can_callback();
    });
    
    joy_sub_ = this->create_subscription<sensor_msgs::msg::Joy>("joy", 10, [this](const sensor_msgs::msg::Joy::SharedPtr joy_msg) {
                joy_msg_ = joy_msg; never_saw_joy_msg = false;   last_joy_msg_time_ = this->get_clock()->now();  joy_callback();} );

    cmd_vel_sub_ = this->create_subscription<geometry_msgs::msg::Twist>("cmd_vel", 10, std::bind(&AgrorobInterface::cmd_vel_callback, this, _1));

    remote_state_pub_ = this->create_publisher<agrorob_msgs::msg::RemoteState>("/agrorob/remote_state", 10);
    robot_state_pub_ = this->create_publisher<agrorob_msgs::msg::RobotState>("/agrorob/robot_state", 10);
    tool_stats_pub_ = this->create_publisher<agrorob_msgs::msg::ToolState>("/agrorob/tool_state", 10);
    engine_stats_pub_ = this->create_publisher<agrorob_msgs::msg::EngineState>("/agrorob/engine_state", 10);
    failure_state_pub_ = this->create_publisher<agrorob_msgs::msg::FailureState>("/agrorob/failure_state", 10);

    logs_pub_ = this->create_publisher<agrorob_msgs::msg::Logs>("/agrorob/logs", 10);

    raw_can_pub_ = this->create_publisher<can_msgs::msg::Frame>("/to_can_bus", 10);

    timer_ = this->create_wall_timer(10ms, std::bind(&AgrorobInterface::timer_callback, this));

  }

  void AgrorobInterface::timer_callback()
  {

   
       

    
    RCLCPP_INFO(this->get_logger(), "111111 %f rpm", refVelocity);
    // joy_msg_->axes[3] = 0.5;

    // can_id25.data[1] = (joy_msg_->axes[3] * -90) + 90;  //steering 


    const double PI = 3.14159;
    const int SAMPLE_RATE = 100; // Adjust the sample rate as needed
    const double MAX_AMPLITUDE = 1;

    
    refVelocity = MAX_AMPLITUDE * std::sin(2 * PI * ii / SAMPLE_RATE);
    // if (refVelocity < 0.0)
    //   refVelocity *= 0.0;

    ii++;
    if (ii == 100)
      ii = 0;

  
    can_id1.data[3] = 170; //engine rotation

    double velocity_ms = 1;//(2.0 * M_PI * wheelR * tool_state_msg.wheels_average_rotational_speed_rpm ) / 60.0;
    // double velocity_ms = abs((2.0 * M_PI * wheelR * 20 * std::sin(2 * PI * (ii + 20) / SAMPLE_RATE) ) / 60.0);
    // if (velocity_ms < 0.0)
    //   velocity_ms *= 0.0;

    logs_msg.velocity_ms = velocity_ms;
    velocity.update(filter.update(velocity_ms), refVelocity, refAcceleration);


    logs_msg.direction_input = velocity.getDirection();

    logs_msg.velocity_input = velocity.getThrottle();

    logs_msg.ref_velocity = velocity.getRef();
  




  can_id1.header.stamp = this->get_clock()->now();
  can_id25.header.stamp = this->get_clock()->now();


  raw_can_pub_->publish(can_id1);
  raw_can_pub_->publish(can_id25); 
  logs_pub_->publish(logs_msg);
    


       
  RCLCPP_INFO(this->get_logger(), "ddddd %f rpm", refVelocity);
     

    
    
   
        
    
    
    
  }


 

  can_msgs::msg::Frame AgrorobInterface::initialize_can_frame()
  {
    can_msgs::msg::Frame frame;
    frame.header.frame_id = "can";
    frame.is_rtr = false;
    frame.is_extended = false;
    frame.is_error = false;
    frame.dlc = 8;
    return frame;
  }


   void AgrorobInterface::set_engine_rpm(const sensor_msgs::msg::Joy::SharedPtr joy_msg_)
  {
    if((this->get_clock()->now().seconds() - last_engine_rpm_change.seconds()) > 0.1)
    {
      last_engine_rpm_change = this->get_clock()->now();

      if (joy_msg_->buttons[3] == 1 && joy_msg_->buttons[5] == 1 && engine_rotation_rpm < 180){
      engine_rotation_rpm += 5;
      RCLCPP_INFO(this->get_logger(), "Engine turnning at %d rpm", engine_rotation_rpm);
      }
      if (joy_msg_->buttons[0] == 1 && joy_msg_->buttons[5] == 1 && engine_rotation_rpm > 140){
        engine_rotation_rpm -= 5;
        RCLCPP_INFO(this->get_logger(), "Engine turnning at %d rpm", engine_rotation_rpm);
      }
    }

    
    
  }

  void AgrorobInterface::cmd_vel_callback(const geometry_msgs::msg::Twist & cmd_vel_msg)
  {

    // refVelocity = cmd_vel_msg.linear.x;
    refRotationVel = cmd_vel_msg.angular.z;
  }


  void AgrorobInterface::joy_callback()
  {
    
  
  
    
  }

 

  void AgrorobInterface::can_callback() 
  {
    
    
    switch(can_msg_->id)
    {
      

      case 100: //From REMOTE
      {

        if(remote_state_msg.auto_mode_enabled != can_msg_->data[2])
        {
          if(can_msg_->data[2] == 0) RCLCPP_INFO(this->get_logger(), "REMOTE: Operation mode: --MANUAL--");
          else RCLCPP_INFO(this->get_logger(), "REMOTE: Operation mode: --AUTO--");
        }

        if(remote_state_msg.stop_engine != can_msg_->data[1])
        {
          if(can_msg_->data[1] == 0) RCLCPP_INFO(this->get_logger(), "REMOTE: Engine: --ACTIVE--");
          else RCLCPP_INFO(this->get_logger(), "REMOTE: Engine: --SHUTDOWN--");
        }

        if(remote_state_msg.stop_drivers != can_msg_->data[0])
        {
          if(can_msg_->data[0] == 0) RCLCPP_INFO(this->get_logger(), "REMOTE: Drivers: --ACTIVE--");
          else RCLCPP_INFO(this->get_logger(), "REMOTE: Drivers: --SHUTDOWN--");
        }

        remote_state_msg.stop_drivers       =  can_msg_->data[0]; //0 - inactive; 1 - STOP of all drives active
        remote_state_msg.stop_engine        =  can_msg_->data[1]; //0 - the internal combustion engine can work; 1 - engine shutdown
        remote_state_msg.auto_mode_enabled  =  can_msg_->data[2]; //0 - manual mode; 1 - automatic mode
        break;
      }

      case 31:
      {
        robot_state_msg.left_front_wheel_encoder_imp  = (can_msg_->data[1] << 8) + can_msg_->data[0]; // high << 8 + low
        robot_state_msg.right_front_wheel_encoder_imp = (can_msg_->data[3] << 8) + can_msg_->data[2];
        robot_state_msg.left_rear_wheel_encoder_imp   = (can_msg_->data[5] << 8) + can_msg_->data[4];
        robot_state_msg.right_rear_wheel_encoder_imp  = (can_msg_->data[7] << 8) + can_msg_->data[6];

        robot_state_pub_->publish(robot_state_msg);
        break;
      }

      case 32: 
      {
        robot_state_msg.left_front_wheel_turn_angle_rad  = (can_msg_->data[0] - 90) * (M_PI/180) ; //
        robot_state_msg.right_front_wheel_turn_angle_rad = (can_msg_->data[1] - 90) * (M_PI/180) ;
        robot_state_msg.left_rear_wheel_turn_angle_rad   = (can_msg_->data[2] - 90) * (M_PI/180) ;
        robot_state_msg.right_rear_wheel_turn_angle_rad  = (can_msg_->data[3] - 90) * (M_PI/180) ;

        robot_state_msg.left_front_wheel_rotational_speed_rad_s   = can_msg_->data[4] * rpm_to_rad_s;
        robot_state_msg.right_front_wheel_rotational_speed_rad_s  = can_msg_->data[5] * rpm_to_rad_s;
        robot_state_msg.left_rear_wheel_rotational_speed_rad_s    = can_msg_->data[6] * rpm_to_rad_s;
        robot_state_msg.right_rear_wheel_rotational_speed_rad_s   = can_msg_->data[7] * rpm_to_rad_s;
        robot_state_pub_->publish(robot_state_msg);
        break;
      }

      case 33: 
      {

        tool_state_msg.vertical_actuator_pos             = can_msg_->data[0];
        tool_state_msg.horizental_actuator_pos           = can_msg_->data[1];
        tool_state_msg.tiller_left_limit_enabled         = can_msg_->data[2];
        tool_state_msg.tiller_right_limit_enabled        = can_msg_->data[3];
        tool_state_msg.sowing_shaft_rotational_speed_rpm = can_msg_->data[4];
        tool_state_msg.fertilizer_tank1_level            = can_msg_->data[5];
        tool_state_msg.fertilizer_tank2_level            = can_msg_->data[6];
        tool_stats_pub_->publish(tool_state_msg);
        break;
      }

      case 34: 
      {
        if(can_msg_->data[0] <= 60)  // degrees minus 60(e.g. 65 means 65-60 = 5 degrees)
          tool_state_msg.tool_level_rad = can_msg_->data[0] * (M_PI/180);
        else
          tool_state_msg.tool_level_rad = (can_msg_->data[0] - 60) * (M_PI/180);

        tool_state_msg.hydraulic_oil_tem_celsius           = can_msg_->data[2]; 
        tool_state_msg.hydraulic_oil_pressure              = can_msg_->data[3]; 
        tool_state_msg.hydraulic_oil_under_min_level       = can_msg_->data[4]; 
        tool_state_msg.hydraulic_oil_above_max_level       = can_msg_->data[5]; 
        tool_state_msg.wheels_average_rotational_speed_rpm = can_msg_->data[6];
        tool_stats_pub_->publish(tool_state_msg);
        break; 
      }
      
      case 51: 
      {
        engine_state_msg.engine_rotation_speed_rpm  = (can_msg_->data[1] << 8) + can_msg_->data[0]; // high << 8 + low
        engine_state_msg.engine_running               = can_msg_->data[2];                        // 0 - off; 1 - the engine is running
        engine_state_msg.engine_coolant_temp_celsius  = can_msg_->data[3];                        // 0 - 100 - value determining the temperature ( o C )
        engine_state_msg.engine_fuel_level_percent    = can_msg_->data[5];                        // 0-100 - value specifying% of tank filling
        engine_state_msg.engine_oil_pressure_bar      = can_msg_->data[7];                        // 0 - 100 (100 means 10 bar)
        engine_stats_pub_->publish(engine_state_msg);
        break;
      }

      case 99: 
      {
        failure_state_msg.left_front_turn_failed           = can_msg_->data[0];
        failure_state_msg.right_front_turn_failed          = can_msg_->data[1];
        failure_state_msg.left_rear_turn_failed            = can_msg_->data[2];
        failure_state_msg.right_rear_turn_failed           = can_msg_->data[3];
        failure_state_msg.tool_vertical_actuator_failed    = can_msg_->data[4];
        failure_state_msg.tool_horizontal_actuator_failed  = can_msg_->data[5];
        failure_state_msg.tool_level_sensor_failed         = can_msg_->data[6];

        for(auto const&  any_failure_end : can_msg_->data)
          if(any_failure_end) RCLCPP_INFO(this->get_logger(), "Sensor failure detected!");

        failure_state_pub_->publish(failure_state_msg);
        break;
      }
    }
  }
  
}
