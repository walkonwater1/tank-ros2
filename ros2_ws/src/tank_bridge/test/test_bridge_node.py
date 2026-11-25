#!/usr/bin/env python3
"""tank_bridge 虚拟串口测试（S1-06）。

不依赖真实小车：用 pty 模拟串口，启动 bridge_node，
发布 /cmd_vel 后校验下发的 W 帧和超时停车。

运行：
    source /opt/ros/humble/setup.bash
    source ~/tank-ros2/ros2_ws/install/setup.bash
    python3 test/test_bridge_node.py
"""

import os
import pty
import re
import select
import subprocess
import sys
import time

import rclpy
from ament_index_python.packages import get_package_prefix
from geometry_msgs.msg import Twist
from rclpy.node import Node

FRAME_RE = re.compile(r"^W,(-?\d+),(-?\d+)$")


def _bridge_executable():
    prefix = get_package_prefix("tank_bridge")
    path = os.path.join(prefix, "lib", "tank_bridge", "bridge_node")
    if not os.path.isfile(path):
        raise RuntimeError(f"bridge_node 未构建: {path}")
    return path


def _drain(master_fd, out):
    while True:
        ready, _, _ = select.select([master_fd], [], [], 0)
        if not ready:
            return
        try:
            data = os.read(master_fd, 4096)
        except OSError:
            return
        if not data:
            return
        for line in data.decode(errors="replace").replace("\r", "\n").split("\n"):
            line = line.strip()
            if line:
                out.append(line)


def test_bridge_node():
    master_fd, slave_fd = pty.openpty()
    slave_name = os.ttyname(slave_fd)

    cmd = [
        _bridge_executable(),
        "--ros-args",
        "-p", f"port:={slave_name}",
        "-p", "baud:=921600",
        "-p", "wheel_separation:=0.20",
        "-p", "max_linear:=0.40",
        "-p", "max_angular:=1.50",
        "-p", "timeout:=300",
        "-p", "publish_rate:=50.0",
    ]
    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    rclpy.init()
    node = Node("bridge_test")
    pub = node.create_publisher(Twist, "/cmd_vel", 10)
    frames = []

    try:
        deadline = time.monotonic() + 5.0
        while pub.get_subscription_count() == 0 and time.monotonic() < deadline:
            time.sleep(0.05)
        assert pub.get_subscription_count() > 0, "bridge_node 未订阅 /cmd_vel"

        def publish(v, w, duration):
            msg = Twist()
            msg.linear.x = v
            msg.angular.z = w
            end = time.monotonic() + duration
            while time.monotonic() < end:
                pub.publish(msg)
                _drain(master_fd, frames)
                time.sleep(0.02)

        publish(0.2, 0.0, 0.6)   # 期望 W,50,50
        publish(0.0, 1.0, 0.6)   # 期望 W,-25,25

        before_timeout = len(frames)
        _drain(master_fd, frames)
        time.sleep(0.8)          # timeout=300ms，留出余量
        _drain(master_fd, frames)
        after_timeout = frames[before_timeout:]

        if proc.poll() is not None:
            output = proc.stdout.read() if proc.stdout else ""
            raise AssertionError(f"bridge_node 退出，code={proc.returncode}\n{output}")

        parsed = [FRAME_RE.match(f) for f in frames]
        valid = [m for m in parsed if m]
        assert valid, f"没有收到合法帧: {frames[:20]}"

        pairs = {(int(m.group(1)), int(m.group(2))) for m in valid}
        assert (50, 50) in pairs, f"直线解算错误: {sorted(pairs)}"
        assert (-25, 25) in pairs, f"转向解算错误: {sorted(pairs)}"

        stop_pairs = [
            (int(m.group(1)), int(m.group(2)))
            for m in (FRAME_RE.match(f) for f in after_timeout) if m
        ]
        assert (0, 0) in stop_pairs, f"超时未停车: {after_timeout}"

        print(f"PASS: 收到 {len(frames)} 帧，帧值 {sorted(pairs)}，超时停车 OK")
    finally:
        node.destroy_node()
        rclpy.shutdown()
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
        os.close(slave_fd)
        os.close(master_fd)


if __name__ == "__main__":
    try:
        test_bridge_node()
    except Exception as exc:  # noqa: BLE001
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
