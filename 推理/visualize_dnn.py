#!/usr/bin/env python3
import threading

import cv2
import rclpy
from cv_bridge import CvBridge
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy, HistoryPolicy
from sensor_msgs.msg import Image

# 这个消息类型是 Hobot/地平线常见的检测结果类型
from ai_msgs.msg import PerceptionTargets


class DnnVisualizer(Node):
    def __init__(self):
        super().__init__('dnn_visualizer')

        self.bridge = CvBridge()
        self.lock = threading.Lock()

        self.latest_targets = None

        image_qos = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
        )

        target_qos = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
        )

        self.image_sub = self.create_subscription(
            Image,
            '/camera/color/image_raw_relay',
            self.image_callback,
            image_qos
        )

        self.target_sub = self.create_subscription(
            PerceptionTargets,
            '/hobot_dnn_detection',
            self.target_callback,
            target_qos
        )

        self.get_logger().info('Visualizer started')
        self.get_logger().info('Image topic: /camera/color/image_raw_relay')
        self.get_logger().info('Detection topic: /hobot_dnn_detection')

    def target_callback(self, msg: PerceptionTargets):
        with self.lock:
            self.latest_targets = msg

    def image_callback(self, msg: Image):
        frame = self.bridge.imgmsg_to_cv2(msg, desired_encoding='bgr8')

        with self.lock:
            targets_msg = self.latest_targets

        if targets_msg is not None:
            self.draw_targets(frame, targets_msg)

        cv2.imshow('dnn_visualizer', frame)
        cv2.waitKey(1)

    def draw_targets(self, frame, targets_msg: PerceptionTargets):
        if not hasattr(targets_msg, 'targets'):
            return

        for target in targets_msg.targets:
            label = ''
            score = 0.0
            x1 = y1 = x2 = y2 = None

            if hasattr(target, 'type'):
                label = str(target.type)

            if hasattr(target, 'rois') and len(target.rois) > 0:
                roi = target.rois[0]

                if hasattr(roi, 'confidence'):
                    score = float(roi.confidence)

                # 兼容常见字段名
                if all(hasattr(roi, k) for k in ['x_offset', 'y_offset', 'width', 'height']):
                    x1 = int(roi.x_offset)
                    y1 = int(roi.y_offset)
                    x2 = int(roi.x_offset + roi.width)
                    y2 = int(roi.y_offset + roi.height)
                elif all(hasattr(roi, k) for k in ['left', 'top', 'right', 'bottom']):
                    x1 = int(roi.left)
                    y1 = int(roi.top)
                    x2 = int(roi.right)
                    y2 = int(roi.bottom)

            if None not in (x1, y1, x2, y2):
                cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 0), 2)
                text = f'{label} {score:.2f}'.strip()
                cv2.putText(
                    frame,
                    text,
                    (x1, max(y1 - 8, 20)),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.6,
                    (0, 255, 0),
                    2
                )


def main(args=None):
    rclpy.init(args=args)
    node = DnnVisualizer()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        cv2.destroyAllWindows()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()