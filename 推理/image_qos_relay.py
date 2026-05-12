#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy, HistoryPolicy
from sensor_msgs.msg import Image


class ImageQosRelay(Node):
    def __init__(self):
        super().__init__('image_qos_relay')

        in_topic = '/camera/color/image_raw'
        out_topic = '/camera/color/image_raw_relay'

        sub_qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
        )

        pub_qos = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
        )

        self.publisher_ = self.create_publisher(Image, out_topic, pub_qos)
        self.subscription = self.create_subscription(
            Image,
            in_topic,
            self.callback,
            sub_qos
        )

        self.get_logger().info(
            f'Relaying {in_topic} (BEST_EFFORT) -> {out_topic} (RELIABLE)'
        )

    def callback(self, msg: Image):
        self.publisher_.publish(msg)


def main(args=None):
    rclpy.init(args=args)
    node = ImageQosRelay()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()