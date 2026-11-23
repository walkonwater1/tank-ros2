#include "geometry_msgs/msg/twist.hpp"
#include "rclcpp/rclcpp.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <utility>

namespace tank_bridge
{

namespace
{

speed_t baud_to_speed(int baud)
{
  switch (baud) {
    case 9600:
      return B9600;
    case 19200:
      return B19200;
    case 38400:
      return B38400;
    case 57600:
      return B57600;
    case 115200:
      return B115200;
    case 230400:
      return B230400;
    case 460800:
      return B460800;
    case 921600:
      return B921600;
    default:
      return 0;
  }
}

int clamp_percent(double value)
{
  return static_cast<int>(std::lround(std::clamp(value, -100.0, 100.0)));
}

double clamp_abs(double value, double limit)
{
  return std::clamp(value, -limit, limit);
}

}  // namespace

class BridgeNode : public rclcpp::Node
{
public:
  BridgeNode()
  : Node("tank_bridge"),
    last_cmd_time_(this->now())
  {
    port_ = this->declare_parameter<std::string>("port", "/dev/ttyUSB0");
    baud_ = this->declare_parameter<int>("baud", 921600);
    wheel_separation_ = this->declare_parameter<double>("wheel_separation", 0.20);
    max_linear_ = this->declare_parameter<double>("max_linear", 0.40);
    max_angular_ = this->declare_parameter<double>("max_angular", 1.50);
    timeout_ms_ = this->declare_parameter<int>("timeout", 500);
    const double publish_rate = this->declare_parameter<double>("publish_rate", 20.0);

    if (wheel_separation_ <= 0.0 || max_linear_ <= 0.0 || max_angular_ <= 0.0 ||
      timeout_ms_ <= 0 || publish_rate <= 0.0)
    {
      throw std::invalid_argument("invalid tank_bridge parameters");
    }

    subscription_ = this->create_subscription<geometry_msgs::msg::Twist>(
      "/cmd_vel", 10,
      [this](const geometry_msgs::msg::Twist::SharedPtr msg) {this->on_cmd_vel(msg);});

    const auto period_ms = std::max(1, static_cast<int>(std::lround(1000.0 / publish_rate)));
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(period_ms), [this]() {this->on_timer();});

    this->open_port();
    RCLCPP_INFO(
      this->get_logger(), "bridge ready: port=%s baud=%d timeout=%dms rate=%.1fHz",
      port_.c_str(), baud_, timeout_ms_, publish_rate);
  }

  ~BridgeNode() override
  {
    this->close_port();
  }

private:
  void on_cmd_vel(const geometry_msgs::msg::Twist::SharedPtr msg)
  {
    cmd_linear_ = msg->linear.x;
    cmd_angular_ = msg->angular.z;
    has_cmd_ = true;
    timed_out_ = false;
    last_cmd_time_ = this->now();
  }

  void on_timer()
  {
    if (fd_ < 0 && !this->open_port()) {
      return;
    }

    const double elapsed_ms = (this->now() - last_cmd_time_).nanoseconds() / 1e6;

    if (elapsed_ms >= static_cast<double>(timeout_ms_)) {
      if (!timed_out_) {
        RCLCPP_WARN(
          this->get_logger(), "cmd_vel timeout after %.0fms, sending stop", elapsed_ms);
        cmd_linear_ = 0.0;
        cmd_angular_ = 0.0;
        this->send_wheels(0, 0);
        timed_out_ = true;
      }
      return;
    }

    if (!has_cmd_) {
      return;
    }

    const auto wheels = this->to_wheels(cmd_linear_, cmd_angular_);
    this->send_wheels(wheels.first, wheels.second);
  }

  std::pair<int, int> to_wheels(double linear, double angular) const
  {
    const double v = clamp_abs(linear, max_linear_);
    const double w = clamp_abs(angular, max_angular_);
    const double half = wheel_separation_ / 2.0;
    const double left = v - w * half;
    const double right = v + w * half;

    return {
      clamp_percent(100.0 * left / max_linear_),
      clamp_percent(100.0 * right / max_linear_)
    };
  }

  bool open_port()
  {
    fd_ = ::open(port_.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd_ < 0) {
      RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 5000,
        "open %s failed: %s", port_.c_str(), std::strerror(errno));
      return false;
    }

    struct termios tty;
    std::memset(&tty, 0, sizeof(tty));
    if (::tcgetattr(fd_, &tty) != 0) {
      RCLCPP_ERROR(this->get_logger(), "tcgetattr failed: %s", std::strerror(errno));
      this->close_port();
      return false;
    }

    const speed_t speed = baud_to_speed(baud_);
    if (speed == 0) {
      RCLCPP_ERROR(this->get_logger(), "unsupported baud: %d", baud_);
      this->close_port();
      return false;
    }

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | BRKINT | IGNBRK | ISTRIP |
      INLCR | IGNCR | ICRNL | PARMRK);
    tty.c_oflag &= ~OPOST;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);

    if (::tcsetattr(fd_, TCSAFLUSH, &tty) != 0) {
      RCLCPP_ERROR(this->get_logger(), "tcsetattr failed: %s", std::strerror(errno));
      this->close_port();
      return false;
    }

    ::tcflush(fd_, TCIOFLUSH);
    RCLCPP_INFO(this->get_logger(), "serial port %s opened at %d", port_.c_str(), baud_);
    return true;
  }

  void close_port()
  {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  bool send_wheels(int left, int right)
  {
    char frame[32];
    const int len = std::snprintf(frame, sizeof(frame), "W,%d,%d\n", left, right);
    if (len <= 0 || len >= static_cast<int>(sizeof(frame))) {
      RCLCPP_ERROR(this->get_logger(), "frame encode failed");
      return false;
    }

    const ssize_t written = ::write(fd_, frame, static_cast<size_t>(len));
    if (written != len) {
      RCLCPP_ERROR(
        this->get_logger(), "serial write failed (%zd/%d): %s",
        written, len, std::strerror(errno));
      this->close_port();
      return false;
    }

    RCLCPP_DEBUG(this->get_logger(), "tx: %s", frame);
    return true;
  }

  std::string port_;
  int baud_{921600};
  double wheel_separation_{0.20};
  double max_linear_{0.40};
  double max_angular_{1.50};
  int timeout_ms_{500};

  int fd_{-1};
  double cmd_linear_{0.0};
  double cmd_angular_{0.0};
  bool has_cmd_{false};
  bool timed_out_{false};
  rclcpp::Time last_cmd_time_;

  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace tank_bridge

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    rclcpp::spin(std::make_shared<tank_bridge::BridgeNode>());
  } catch (const std::exception & e) {
    RCLCPP_ERROR(rclcpp::get_logger("tank_bridge"), "startup failed: %s", e.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
