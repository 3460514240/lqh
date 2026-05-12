#include "ai_msgs/msg/perception_targets.hpp"
#include "dnn_node/dnn_node.h"
#include "dnn_node/util/image_proc.h"
#include "sensor_msgs/msg/image.hpp"

#include "include/parser.h"

#include <cv_bridge/cv_bridge.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

int BGRToNV12(const cv::Mat& bgr, std::vector<uint8_t>& nv12_data) {
  if (bgr.empty()) return -1;
  if (bgr.cols % 2 != 0 || bgr.rows % 2 != 0) return -1;

  int width = bgr.cols;
  int height = bgr.rows;

  cv::Mat yuv_i420;
  cv::cvtColor(bgr, yuv_i420, cv::COLOR_BGR2YUV_I420);

  nv12_data.resize(width * height * 3 / 2);

  uint8_t* dst_y = nv12_data.data();
  uint8_t* dst_uv = nv12_data.data() + width * height;

  const uint8_t* src_y = yuv_i420.data;
  const uint8_t* src_u = src_y + width * height;
  const uint8_t* src_v = src_u + (width * height) / 4;

  memcpy(dst_y, src_y, width * height);
  for (int i = 0; i < (width * height) / 4; ++i) {
    dst_uv[2 * i] = src_u[i];
    dst_uv[2 * i + 1] = src_v[i];
  }
  return 0;
}

static cv::Mat LetterboxBGR(const cv::Mat& src,
                            int dst_w,
                            int dst_h,
                            float& ratio,
                            float& pad_x,
                            float& pad_y) {
  int src_h = src.rows;
  int src_w = src.cols;

  ratio = std::min(static_cast<float>(dst_w) / src_w,
                   static_cast<float>(dst_h) / src_h);

  int new_w = static_cast<int>(std::round(src_w * ratio));
  int new_h = static_cast<int>(std::round(src_h * ratio));

  cv::Mat resized;
  cv::resize(src, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

  pad_x = (dst_w - new_w) / 2.0f;
  pad_y = (dst_h - new_h) / 2.0f;

  int left = static_cast<int>(std::round(pad_x - 0.1f));
  int right = static_cast<int>(std::round(pad_x + 0.1f));
  int top = static_cast<int>(std::round(pad_y - 0.1f));
  int bottom = static_cast<int>(std::round(pad_y + 0.1f));

  cv::Mat out;
  cv::copyMakeBorder(
      resized, out, top, bottom, left, right,
      cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));

  return out;
}

struct DNNNodeSampleOutput : public hobot::dnn_node::DnnNodeOutput {
  float ratio = 1.0f;
  float pad_x = 0.0f;
  float pad_y = 0.0f;
  cv::Mat original_bgr;
};

class DNNNodeSample : public hobot::dnn_node::DnnNode {
 public:
  DNNNodeSample(const std::string& node_name = "dnn_node_sample",
                const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

 protected:
  int SetNodePara() override;
  int PostProcess(const std::shared_ptr<hobot::dnn_node::DnnNodeOutput>&
                      node_output) override;

 private:
  int model_input_width_ = -1;
  int model_input_height_ = -1;

  rclcpp::Publisher<ai_msgs::msg::PerceptionTargets>::SharedPtr msg_publisher_ =
      nullptr;

  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_sub_ =
      nullptr;

  void ImageCallback(const sensor_msgs::msg::Image::SharedPtr msg);
  void RunImage(const cv::Mat& bgr, const std_msgs::msg::Header& header);
};

DNNNodeSample::DNNNodeSample(const std::string& node_name,
                             const rclcpp::NodeOptions& options)
    : hobot::dnn_node::DnnNode(node_name, options) {
  if (Init() != 0 ||
      GetModelInputSize(0, model_input_width_, model_input_height_) < 0) {
    RCLCPP_ERROR(rclcpp::get_logger("dnn_node_sample"), "Node init fail!");
    rclcpp::shutdown();
    return;
  }

  msg_publisher_ = this->create_publisher<ai_msgs::msg::PerceptionTargets>(
      "/dnn_node_sample", 10);

  image_sub_ = this->create_subscription<sensor_msgs::msg::Image>(
      "/camera/color/image_raw",
      rclcpp::SensorDataQoS(),
      std::bind(&DNNNodeSample::ImageCallback, this, std::placeholders::_1));

  RCLCPP_INFO(rclcpp::get_logger("dnn_node_sample"),
              "Subscribed image topic: /camera/color/image_raw");
}

int DNNNodeSample::SetNodePara() {
  if (!dnn_node_para_ptr_) return -1;

  dnn_node_para_ptr_->model_file =
      "/home/sunrise/example/src/hobot_dnn-develop/dnn_node_sample/config/yolov8_x5_640x640_nv12_v2.bin";

  RCLCPP_INFO(rclcpp::get_logger("dnn_node_sample"),
              "model_file = %s",
              dnn_node_para_ptr_->model_file.c_str());

  dnn_node_para_ptr_->model_task_type =
      hobot::dnn_node::ModelTaskType::ModelInferType;
  dnn_node_para_ptr_->task_num = 4;
  return 0;
}

void DNNNodeSample::ImageCallback(const sensor_msgs::msg::Image::SharedPtr msg) {
  cv::Mat bgr;

  try {
    auto cv_ptr = cv_bridge::toCvCopy(msg, "bgr8");
    bgr = cv_ptr->image;
  } catch (const std::exception& e) {
    RCLCPP_ERROR(rclcpp::get_logger("dnn_node_sample"),
                 "cv_bridge convert failed: %s", e.what());
    return;
  }

  if (bgr.empty()) {
    RCLCPP_ERROR(rclcpp::get_logger("dnn_node_sample"),
                 "empty image from topic");
    return;
  }

  RunImage(bgr, msg->header);
}

void DNNNodeSample::RunImage(const cv::Mat& bgr,
                             const std_msgs::msg::Header& header) {
  if (bgr.empty()) {
    RCLCPP_ERROR(rclcpp::get_logger("dnn_node_sample"),
                 "input bgr image is empty");
    return;
  }

  auto dnn_output = std::make_shared<DNNNodeSampleOutput>();
  dnn_output->msg_header = std::make_shared<std_msgs::msg::Header>(header);
  dnn_output->original_bgr = bgr.clone();

  cv::Mat letterbox_bgr = LetterboxBGR(
      bgr,
      model_input_width_,
      model_input_height_,
      dnn_output->ratio,
      dnn_output->pad_x,
      dnn_output->pad_y);

  if (letterbox_bgr.empty()) {
    RCLCPP_ERROR(rclcpp::get_logger("dnn_node_sample"), "Letterbox failed");
    return;
  }

  if (letterbox_bgr.cols % 2 != 0 || letterbox_bgr.rows % 2 != 0) {
    RCLCPP_ERROR(rclcpp::get_logger("dnn_node_sample"),
                 "Letterbox image size must be even, got %d x %d",
                 letterbox_bgr.cols, letterbox_bgr.rows);
    return;
  }

  std::vector<uint8_t> nv12_data;
  if (BGRToNV12(letterbox_bgr, nv12_data) != 0) {
    RCLCPP_ERROR(rclcpp::get_logger("dnn_node_sample"),
                 "Convert BGR to NV12 failed");
    return;
  }

  auto pyramid = hobot::dnn_node::ImageProc::GetNV12PyramidFromNV12Img(
      reinterpret_cast<const char*>(nv12_data.data()),
      model_input_height_,
      model_input_width_,
      model_input_height_,
      model_input_width_);

  if (!pyramid) {
    RCLCPP_ERROR(rclcpp::get_logger("dnn_node_sample"), "Get pyramid failed");
    return;
  }

  auto inputs =
      std::vector<std::shared_ptr<hobot::dnn_node::DNNInput>>{pyramid};

  if (Run(inputs, dnn_output, nullptr, false) < 0) {
    RCLCPP_ERROR(rclcpp::get_logger("dnn_node_sample"),
                 "Run image predict failed!");
  } else {
    RCLCPP_INFO(rclcpp::get_logger("dnn_node_sample"),
                "Run image predict success from topic");
  }
}

int DNNNodeSample::PostProcess(
    const std::shared_ptr<hobot::dnn_node::DnnNodeOutput>& node_output) {
  if (!rclcpp::ok()) {
    return 0;
  }

  auto tp_start = std::chrono::system_clock::now();

  ai_msgs::msg::PerceptionTargets::UniquePtr pub_data(
      new ai_msgs::msg::PerceptionTargets());
  pub_data->set__header(*node_output->msg_header);

  std::vector<std::shared_ptr<hobot::dnn_node::dnn_node_sample::YoloV5Result>>
      results;

  if (hobot::dnn_node::dnn_node_sample::Parse(node_output, results) < 0) {
    RCLCPP_ERROR(rclcpp::get_logger("dnn_node_sample"),
                 "Parse node_output fail!");
    return -1;
  }

  auto sample_node_output =
      std::dynamic_pointer_cast<DNNNodeSampleOutput>(node_output);
  if (!sample_node_output) {
    RCLCPP_ERROR(rclcpp::get_logger("dnn_node_sample"),
                 "Cast dnn node output fail!");
    return -1;
  }

  cv::Mat vis = sample_node_output->original_bgr.clone();
  if (vis.empty()) {
    RCLCPP_ERROR(rclcpp::get_logger("dnn_node_sample"),
                 "original image is empty for visualization");
    return -1;
  }

  for (auto& rect : results) {
    if (!rect) continue;

    float x1 = rect->xmin;
    float y1 = rect->ymin;
    float x2 = rect->xmax;
    float y2 = rect->ymax;

    x1 = (x1 - sample_node_output->pad_x) / sample_node_output->ratio;
    x2 = (x2 - sample_node_output->pad_x) / sample_node_output->ratio;
    y1 = (y1 - sample_node_output->pad_y) / sample_node_output->ratio;
    y2 = (y2 - sample_node_output->pad_y) / sample_node_output->ratio;

    x1 = std::max(0.0f, std::min(x1, static_cast<float>(vis.cols - 1)));
    y1 = std::max(0.0f, std::min(y1, static_cast<float>(vis.rows - 1)));
    x2 = std::max(0.0f, std::min(x2, static_cast<float>(vis.cols - 1)));
    y2 = std::max(0.0f, std::min(y2, static_cast<float>(vis.rows - 1)));

    if (x2 <= x1 || y2 <= y1) continue;

    std::stringstream ss;
    ss << "det rect: " << x1 << " " << y1 << " " << x2 << " " << y2
       << ", det type: " << rect->class_name
       << ", score:" << rect->score;
    RCLCPP_INFO(rclcpp::get_logger("dnn_node_sample"), "%s", ss.str().c_str());

    ai_msgs::msg::Roi roi;
    roi.rect.set__x_offset(static_cast<int>(x1));
    roi.rect.set__y_offset(static_cast<int>(y1));
    roi.rect.set__width(static_cast<int>(x2 - x1));
    roi.rect.set__height(static_cast<int>(y2 - y1));
    roi.set__confidence(rect->score);

    ai_msgs::msg::Target target;
    target.set__type(rect->class_name);
    target.rois.emplace_back(roi);
    pub_data->targets.emplace_back(std::move(target));
  }

  for (const auto& target : pub_data->targets) {
    for (const auto& roi : target.rois) {
      int x = static_cast<int>(roi.rect.x_offset);
      int y = static_cast<int>(roi.rect.y_offset);
      int w = static_cast<int>(roi.rect.width);
      int h = static_cast<int>(roi.rect.height);

      if (w <= 0 || h <= 0) continue;
      if (x < 0 || y < 0 || x >= vis.cols || y >= vis.rows) continue;
      if (x + w > vis.cols) w = vis.cols - x;
      if (y + h > vis.rows) h = vis.rows - y;
      if (w <= 0 || h <= 0) continue;

      cv::rectangle(vis, cv::Rect(x, y, w, h), cv::Scalar(0, 255, 0), 2);

      std::ostringstream label_ss;
      label_ss << target.type << " " << std::fixed << std::setprecision(3)
               << roi.confidence;
      cv::putText(vis,
                  label_ss.str(),
                  cv::Point(x, std::max(20, y - 5)),
                  cv::FONT_HERSHEY_SIMPLEX,
                  0.7,
                  cv::Scalar(0, 255, 0),
                  2);
    }
  }

  // 实时显示窗口
  cv::imshow("dnn_node_sample_view", vis);
  cv::waitKey(1);

  std::string save_path = "./config/result.jpg";
  if (cv::imwrite(save_path, vis)) {
    RCLCPP_INFO(rclcpp::get_logger("dnn_node_sample"),
                "Saved result image to: %s",
                save_path.c_str());
  } else {
    RCLCPP_ERROR(rclcpp::get_logger("dnn_node_sample"),
                 "Failed to save result image!");
  }

  if (node_output->rt_stat) {
    pub_data->set__fps(round(node_output->rt_stat->output_fps));
    if (node_output->rt_stat->fps_updated) {
      auto tp_now = std::chrono::system_clock::now();
      auto interval = std::chrono::duration_cast<std::chrono::milliseconds>(
                          tp_now - tp_start)
                          .count();
      RCLCPP_WARN(rclcpp::get_logger("dnn_node_sample"),
                  "input fps: %.2f, out fps: %.2f, infer time ms: %d, "
                  "post process time ms: %d",
                  node_output->rt_stat->input_fps,
                  node_output->rt_stat->output_fps,
                  node_output->rt_stat->infer_time_ms,
                  interval);
    }
  }

  msg_publisher_->publish(std::move(pub_data));
  return 0;
}

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<DNNNodeSample>());
  cv::destroyAllWindows();
  rclcpp::shutdown();
  return 0;
}