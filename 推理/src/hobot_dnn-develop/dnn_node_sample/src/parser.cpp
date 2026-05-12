#include "include/parser.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

using hobot::dnn_node::DNNTensor;

namespace hobot {
namespace dnn_node {
namespace dnn_node_sample {

struct YoloV8Config {
  int class_num;
  std::vector<std::string> class_names;
  bool score_need_sigmoid;
  bool bbox_is_xywh;
};

float score_threshold_ = 0.2f;
float nms_threshold_ = 0.35f;
int nms_top_k_ = 5000;

YoloV8Config yolo8_config_ = {
    1,
    {"grain_pile"},
    false,  // 当前输出不要再做 sigmoid
    true    // 当前输出前4维按 x,y,w,h
};

static inline float sigmoid(float x) {
  return 1.0f / (1.0f + std::exp(-x));
}

static inline float clampf(float v, float lo, float hi) {
  return std::max(lo, std::min(v, hi));
}

void yolo_nms(std::vector<YoloV5Result> &input,
              float iou_threshold,
              int top_k,
              std::vector<std::shared_ptr<YoloV5Result>> &result,
              bool suppress) {
  std::stable_sort(input.begin(), input.end(), std::greater<YoloV5Result>());

  std::vector<bool> skip(input.size(), false);

  std::vector<float> areas;
  areas.reserve(input.size());
  for (size_t i = 0; i < input.size(); i++) {
    float width = input[i].xmax - input[i].xmin;
    float height = input[i].ymax - input[i].ymin;
    areas.push_back(std::max(0.0f, width) * std::max(0.0f, height));
  }

  int count = 0;
  for (size_t i = 0; count < top_k && i < skip.size(); i++) {
    if (skip[i]) {
      continue;
    }
    skip[i] = true;
    ++count;

    for (size_t j = i + 1; j < skip.size(); ++j) {
      if (skip[j]) {
        continue;
      }
      if (!suppress && input[i].id != input[j].id) {
        continue;
      }

      float xx1 = std::max(input[i].xmin, input[j].xmin);
      float yy1 = std::max(input[i].ymin, input[j].ymin);
      float xx2 = std::min(input[i].xmax, input[j].xmax);
      float yy2 = std::min(input[i].ymax, input[j].ymax);

      if (xx2 > xx1 && yy2 > yy1) {
        float area_intersection = (xx2 - xx1) * (yy2 - yy1);
        float iou_ratio =
            area_intersection / (areas[j] + areas[i] - area_intersection);
        if (iou_ratio > iou_threshold) {
          skip[j] = true;
        }
      }
    }

    auto yolo_res = std::make_shared<YoloV5Result>(input[i].id,
                                                   input[i].xmin,
                                                   input[i].ymin,
                                                   input[i].xmax,
                                                   input[i].ymax,
                                                   input[i].score,
                                                   input[i].class_name);
    result.push_back(yolo_res);
  }
}

// 适配当前模型输出：NCHW [1, 5, 8400, 1]
// 当前按单类检测模型处理：row = [x, y, w, h, score]
void ParseTensor(std::shared_ptr<DNNTensor> tensor,                              //DeepSeek-R1-0528，电脑客户端访问，2026年4月10日 10：00–14:00
                 std::vector<YoloV5Result> &results) {
  hbSysFlushMem(&(tensor->sysMem[0]), HB_SYS_MEM_CACHE_INVALIDATE);

  auto *data = reinterpret_cast<float *>(tensor->sysMem[0].virAddr);
  if (!data) {
    RCLCPP_ERROR(rclcpp::get_logger("Yolo8_detection_parser"),
                 "tensor virAddr is null");
    return;
  }

  const auto &shape = tensor->properties.validShape;
  int dim0 = shape.dimensionSize[0];
  int dim1 = shape.dimensionSize[1];
  int dim2 = shape.dimensionSize[2];
  int dim3 = shape.dimensionSize[3];

  RCLCPP_INFO(rclcpp::get_logger("Yolo8_detection_parser"),
              "tensor layout=%d, shape=[%d, %d, %d, %d]",
              tensor->properties.tensorLayout,
              dim0, dim1, dim2, dim3);

  if (tensor->properties.tensorLayout != HB_DNN_LAYOUT_NCHW) {
    RCLCPP_ERROR(rclcpp::get_logger("Yolo8_detection_parser"),
                 "only support NCHW output now");
    return;
  }

  const int num_classes = yolo8_config_.class_num;
  const int channels = dim1;
  const int num_boxes = dim2 * dim3;

  if (channels != 4 + num_classes) {
    RCLCPP_ERROR(rclcpp::get_logger("Yolo8_detection_parser"),
                 "unexpected channel size: %d, expect: %d",
                 channels, 4 + num_classes);
    return;
  }

  const float *x_ptr = data + 0 * num_boxes;
  const float *y_ptr = data + 1 * num_boxes;
  const float *w_ptr = data + 2 * num_boxes;
  const float *h_ptr = data + 3 * num_boxes;
  const float *cls_ptr = data + 4 * num_boxes;

  float max_raw_score = -1e10f;
  float min_raw_score = 1e10f;
  float max_after_score = -1e10f;
  float min_after_score = 1e10f;
  int max_score_index = -1;

  for (int i = 0; i < num_boxes; ++i) {
    float raw_score = cls_ptr[i];
    if (raw_score > max_raw_score) {
      max_raw_score = raw_score;
      max_score_index = i;
    }
    min_raw_score = std::min(min_raw_score, raw_score);

    float score = raw_score;
    if (yolo8_config_.score_need_sigmoid) {
      score = sigmoid(score);
    }

    max_after_score = std::max(max_after_score, score);
    min_after_score = std::min(min_after_score, score);
  }

  RCLCPP_WARN(rclcpp::get_logger("Yolo8_detection_parser"),
              "raw score range: min=%.6f max=%.6f (idx=%d)",
              min_raw_score, max_raw_score, max_score_index);

  RCLCPP_WARN(rclcpp::get_logger("Yolo8_detection_parser"),
              "score range after process: min=%.6f max=%.6f, "
              "score_need_sigmoid=%d, threshold=%.3f",
              min_after_score, max_after_score,
              static_cast<int>(yolo8_config_.score_need_sigmoid),
              score_threshold_);

  // ===== 新增：打印 Top-K 高分框 =====
  std::vector<int> indices(num_boxes);
  std::iota(indices.begin(), indices.end(), 0);

  std::sort(indices.begin(), indices.end(),
            [&](int a, int b) {
              float sa = cls_ptr[a];
              float sb = cls_ptr[b];
              if (yolo8_config_.score_need_sigmoid) {
                sa = sigmoid(sa);
                sb = sigmoid(sb);
              }
              return sa > sb;
            });

  int top_k_debug = std::min(20, num_boxes);
  RCLCPP_WARN(rclcpp::get_logger("Yolo8_detection_parser"),
              "========== Top %d high-score boxes ==========", top_k_debug);

  for (int k = 0; k < top_k_debug; ++k) {
    int i = indices[k];

    float score = cls_ptr[i];
    if (yolo8_config_.score_need_sigmoid) {
      score = sigmoid(score);
    }

    float xmin = 0.0f;
    float ymin = 0.0f;
    float xmax = 0.0f;
    float ymax = 0.0f;

    if (yolo8_config_.bbox_is_xywh) {
      float cx = x_ptr[i];
      float cy = y_ptr[i];
      float bw = w_ptr[i];
      float bh = h_ptr[i];

      xmin = cx - bw * 0.5f;
      ymin = cy - bh * 0.5f;
      xmax = cx + bw * 0.5f;
      ymax = cy + bh * 0.5f;

      RCLCPP_WARN(rclcpp::get_logger("Yolo8_detection_parser"),
                  "TOP[%d] idx=%d score=%.6f raw_xywh=[%.6f, %.6f, %.6f, %.6f] "
                  "xyxy_before=[%.6f, %.6f, %.6f, %.6f]",
                  k, i, score, x_ptr[i], y_ptr[i], w_ptr[i], h_ptr[i],
                  xmin, ymin, xmax, ymax);
    } else {
      xmin = x_ptr[i];
      ymin = y_ptr[i];
      xmax = w_ptr[i];
      ymax = h_ptr[i];

      RCLCPP_WARN(rclcpp::get_logger("Yolo8_detection_parser"),
                  "TOP[%d] idx=%d score=%.6f raw_xyxy=[%.6f, %.6f, %.6f, %.6f]",
                  k, i, score, xmin, ymin, xmax, ymax);
    }

    float cxmin = clampf(xmin, 0.0f, 639.0f);
    float cymin = clampf(ymin, 0.0f, 639.0f);
    float cxmax = clampf(xmax, 0.0f, 639.0f);
    float cymax = clampf(ymax, 0.0f, 639.0f);

    RCLCPP_WARN(rclcpp::get_logger("Yolo8_detection_parser"),
                "TOP[%d] idx=%d clamp_xyxy=[%.6f, %.6f, %.6f, %.6f] valid=%d",
                k, i, cxmin, cymin, cxmax, cymax,
                (cxmax > cxmin && cymax > cymin) ? 1 : 0);
  }

  RCLCPP_WARN(rclcpp::get_logger("Yolo8_detection_parser"),
              "========== End Top high-score boxes ==========");
  // ===== Top-K 调试结束 =====

  int pass_count = 0;

  for (int i = 0; i < num_boxes; ++i) {
    float score = cls_ptr[i];
    if (yolo8_config_.score_need_sigmoid) {
      score = sigmoid(score);
    }

    if (score < score_threshold_) {
      continue;
    }

    float xmin = 0.0f;
    float ymin = 0.0f;
    float xmax = 0.0f;
    float ymax = 0.0f;

    if (yolo8_config_.bbox_is_xywh) {
      float cx = x_ptr[i];
      float cy = y_ptr[i];
      float bw = w_ptr[i];
      float bh = h_ptr[i];

      xmin = cx - bw * 0.5f;
      ymin = cy - bh * 0.5f;
      xmax = cx + bw * 0.5f;
      ymax = cy + bh * 0.5f;
    } else {
      xmin = x_ptr[i];
      ymin = y_ptr[i];
      xmax = w_ptr[i];
      ymax = h_ptr[i];
    }

    xmin = clampf(xmin, 0.0f, 639.0f);
    ymin = clampf(ymin, 0.0f, 639.0f);
    xmax = clampf(xmax, 0.0f, 639.0f);
    ymax = clampf(ymax, 0.0f, 639.0f);

    if (xmax <= xmin || ymax <= ymin) {
      continue;
    }

    pass_count++;

    results.emplace_back(
        YoloV5Result(0,
                     xmin,
                     ymin,
                     xmax,
                     ymax,
                     score,
                     yolo8_config_.class_names[0]));
  }

  RCLCPP_WARN(rclcpp::get_logger("Yolo8_detection_parser"),
              "boxes passed threshold before nms: %d", pass_count);
}

int32_t Parse(
    const std::shared_ptr<hobot::dnn_node::DnnNodeOutput> &node_output,
    std::vector<std::shared_ptr<YoloV5Result>> &results) {
  if (!node_output || node_output->output_tensors.empty()) {
    RCLCPP_ERROR(rclcpp::get_logger("Yolo8_detection_parser"),
                 "node_output is null or empty");
    return -1;
  }

  std::vector<YoloV5Result> parse_results;
  ParseTensor(node_output->output_tensors[0], parse_results);

  yolo_nms(parse_results, nms_threshold_, nms_top_k_, results, false);

  RCLCPP_WARN(rclcpp::get_logger("Yolo8_detection_parser"),
              "boxes after nms: %zu", results.size());

  return 0;
}

}  // namespace dnn_node_sample
}  // namespace dnn_node
}  // namespace hobot