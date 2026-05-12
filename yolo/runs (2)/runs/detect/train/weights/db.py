import os
import time
import cv2
import numpy as np
import onnxruntime as ort

IMG_SIZE = 640
IMAGE_PATH = r"D:\yolo\runs (2)\runs\detect\train\weights\calib_image_dir\00C7DA2592B4EF2A73FFFA7B08FF4E09.jpg"
MODEL_PATH = "best_int8.onnx"

# 尽量对齐你当前板端 parser
CONF_THRES = 0.20
IOU_THRES = 0.50

# 板端当前是单类
CLASS_NAMES = ["grain_pile"]


def letterbox(im, new_shape=(640, 640), color=(114, 114, 114)):
    h, w = im.shape[:2]
    r = min(new_shape[0] / h, new_shape[1] / w)
    new_unpad = (int(round(w * r)), int(round(h * r)))

    dw = (new_shape[1] - new_unpad[0]) / 2
    dh = (new_shape[0] - new_unpad[1]) / 2

    im = cv2.resize(im, new_unpad, interpolation=cv2.INTER_LINEAR)
    top, bottom = int(round(dh - 0.1)), int(round(dh + 0.1))
    left, right = int(round(dw - 0.1)), int(round(dw + 0.1))

    im = cv2.copyMakeBorder(
        im, top, bottom, left, right,
        cv2.BORDER_CONSTANT, value=color
    )
    return im, r, (dw, dh)


def preprocess(img_bgr):
    img_rgb = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2RGB)
    img_lb, ratio, dwdh = letterbox(img_rgb, (IMG_SIZE, IMG_SIZE))
    x = img_lb.astype(np.float32) / 255.0
    x = np.transpose(x, (2, 0, 1))
    x = np.expand_dims(x, axis=0)
    return x, ratio, dwdh


def print_output_debug(outputs):
    print("输出个数:", len(outputs))
    for i, out in enumerate(outputs):
        out_np = np.array(out)
        print(f"\n===== 输出[{i}] =====")
        print("shape:", out_np.shape)

        flat = out_np.reshape(-1)
        n = min(20, flat.size)
        print(f"前 {n} 个值:")
        print(flat[:n])


def print_detection_debug(dets, title="检测结果"):
    print(f"\n===== {title} =====")
    print("检测框数量:", len(dets))
    for i, d in enumerate(dets):
        x1, y1, x2, y2 = d["box"]
        score = d["score"]
        cls = d["class_id"]

        label = CLASS_NAMES[cls] if 0 <= cls < len(CLASS_NAMES) else f"class{cls}"

        print(
            f"[{i}] "
            f"class_id={cls}, "
            f"label={label}, "
            f"score={score:.6f}, "
            f"box=({x1:.2f}, {y1:.2f}, {x2:.2f}, {y2:.2f})"
        )


def postprocess_raw_like_board(pred, orig_shape, ratio, dwdh, conf_thres, iou_thres):  #DeepSeek-R1-0528，电脑客户端访问，2025年11月14日 15：00–22:30
    """
    按你当前板端逻辑对齐：
    - 单类输出 [x, y, w, h, score]
    - 不做 sigmoid
    - 先 threshold
    - xywh -> xyxy
    - 去掉 letterbox padding，再映射回原图
    - 再做 NMS
    - 不合框
    """
    pred = np.array(pred)

    if pred.ndim == 3:
        pred = pred[0]

    if pred.ndim != 2:
        return []

    # 对齐成 [num_boxes, channels]
    if pred.shape[0] < pred.shape[1]:
        pred = pred.T

    h0, w0 = orig_shape[:2]
    dw, dh = dwdh

    boxes = []
    scores = []
    class_ids = []

    # 调试：看原始分数范围
    raw_scores = pred[:, 4] if pred.shape[1] >= 5 else np.array([])
    if raw_scores.size > 0:
        print("\n===== 原始输出调试 =====")
        print("raw score range: min=%.6f max=%.6f" % (raw_scores.min(), raw_scores.max()))
        for i in range(min(10, len(pred))):
            row = pred[i]
            print(
                f"[{i}] x={row[0]:.6f} y={row[1]:.6f} "
                f"w={row[2]:.6f} h={row[3]:.6f} raw_score={row[4]:.6f}"
            )

    for row in pred:
        if len(row) < 5:
            continue

        x, y, w, h = row[:4]
        score = float(row[4])
        cls_id = 0

        if score < conf_thres:
            continue

        x1 = x - w / 2.0
        y1 = y - h / 2.0
        x2 = x + w / 2.0
        y2 = y + h / 2.0

        # 与板端 sample.cpp 一致：先减 padding，再除 ratio
        x1 = (x1 - dw) / ratio
        x2 = (x2 - dw) / ratio
        y1 = (y1 - dh) / ratio
        y2 = (y2 - dh) / ratio

        x1 = np.clip(x1, 0, w0 - 1)
        y1 = np.clip(y1, 0, h0 - 1)
        x2 = np.clip(x2, 0, w0 - 1)
        y2 = np.clip(y2, 0, h0 - 1)

        if x2 <= x1 or y2 <= y1:
            continue

        boxes.append([float(x1), float(y1), float(x2), float(y2)])
        scores.append(score)
        class_ids.append(cls_id)

    print("boxes passed threshold before nms:", len(boxes))

    if len(boxes) == 0:
        return []

    boxes_xywh = []
    for b in boxes:
        x1, y1, x2, y2 = b
        boxes_xywh.append([x1, y1, x2 - x1, y2 - y1])

    indices = cv2.dnn.NMSBoxes(boxes_xywh, scores, conf_thres, iou_thres)

    results = []
    if len(indices) > 0:
        indices = np.array(indices).reshape(-1)
        for i in indices:
            results.append({
                "box": np.array(boxes[i], dtype=np.float32),
                "score": float(scores[i]),
                "class_id": int(class_ids[i])
            })

    results.sort(key=lambda x: x["score"], reverse=True)
    print("boxes after nms:", len(results))
    return results


def run_model(model_path, img):
    sess = ort.InferenceSession(model_path, providers=["CPUExecutionProvider"])
    input_name = sess.get_inputs()[0].name

    x, ratio, dwdh = preprocess(img)

    t0 = time.perf_counter()
    outputs = sess.run(None, {input_name: x})
    t1 = time.perf_counter()

    print(f"\n========== 模型: {model_path} ==========")
    print_output_debug(outputs)

    pred = outputs[0]
    print("\n主输出 shape:", np.array(pred).shape)

    detections = postprocess_raw_like_board(
        pred, img.shape, ratio, dwdh, CONF_THRES, IOU_THRES
    )

    print_detection_debug(detections, title="NMS后框（不合框）")
    return detections, (t1 - t0) * 1000


def draw(img, dets):
    out = img.copy()
    for d in dets:
        x1, y1, x2, y2 = d["box"].astype(int)
        score = d["score"]
        cls = d["class_id"]

        label = CLASS_NAMES[cls] if cls < len(CLASS_NAMES) else str(cls)

        cv2.rectangle(out, (x1, y1), (x2, y2), (0, 255, 0), 2)

        text = f"{label}:{score:.2f}"
        ty = max(y1 - 8, 20)
        cv2.putText(
            out, text, (x1, ty),
            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2
        )
    return out


def main():
    img = cv2.imread(IMAGE_PATH)
    if img is None:
        raise FileNotFoundError(f"读取图片失败: {IMAGE_PATH}")

    dets, t = run_model(MODEL_PATH, img)
    out = draw(img, dets)

    cv2.imwrite("result_no_merge.jpg", out)

    print("\n================ 最终结果 ================")
    print("检测框数量:", len(dets))
    print("推理时间: %.2f ms" % t)
    print("已保存: result_no_merge.jpg")


if __name__ == "__main__":
    main()