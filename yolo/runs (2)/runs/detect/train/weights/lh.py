import os
import glob
import cv2
import onnx
import numpy as np
from onnxruntime.quantization import (
    CalibrationDataReader,
    QuantType,
    QuantFormat,
    CalibrationMethod,
    quantize_static,
)

class YOLODataReader(CalibrationDataReader):
    def __init__(self, image_dir, input_name, input_size=(640, 640), max_samples=100):
        self.input_name = input_name
        self.input_size = input_size
        self.max_samples = max_samples

        exts = ["*.jpg", "*.jpeg", "*.png", "*.bmp"]
        self.image_paths = []
        for ext in exts:
            self.image_paths.extend(glob.glob(os.path.join(image_dir, ext)))

        self.image_paths = self.image_paths[:max_samples]
        self.data_iter = None

        if len(self.image_paths) == 0:
            raise FileNotFoundError(f"校准目录里没有图片: {image_dir}")

    def _letterbox(self, img, new_shape=(640, 640), color=(114, 114, 114)):
        h, w = img.shape[:2]
        new_w, new_h = new_shape

        r = min(new_w / w, new_h / h)
        resized_w, resized_h = int(round(w * r)), int(round(h * r))
        img = cv2.resize(img, (resized_w, resized_h), interpolation=cv2.INTER_LINEAR)

        dw = new_w - resized_w
        dh = new_h - resized_h
        top, bottom = dh // 2, dh - dh // 2
        left, right = dw // 2, dw - dw // 2

        img = cv2.copyMakeBorder(
            img, top, bottom, left, right,
            cv2.BORDER_CONSTANT, value=color
        )
        return img

    def _preprocess(self, image_path):
        img = cv2.imread(image_path)
        if img is None:
            raise RuntimeError(f"读取图片失败: {image_path}")

        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        img = self._letterbox(img, self.input_size)

        img = img.astype(np.float32) / 255.0
        img = np.transpose(img, (2, 0, 1))   # HWC -> CHW
        img = np.expand_dims(img, axis=0)    # -> NCHW

        return {self.input_name: img}

    def get_next(self):
        if self.data_iter is None:
            data = [self._preprocess(p) for p in self.image_paths]
            self.data_iter = iter(data)

        return next(self.data_iter, None)


def get_input_name(onnx_path):
    model = onnx.load(onnx_path)
    return model.graph.input[0].name


def check_opset(onnx_path):
    model = onnx.load(onnx_path)
    for opset in model.opset_import:
        if opset.domain == "" or opset.domain is None:
            return opset.version
    return None


def main():
    # 1. 你的 float ONNX 模型
    fp32_model = r"D:\yolo\runs (2)\runs\detect\train\weights\best.onnx"

    # 2. 输出 INT8 模型
    int8_model = r"D:\yolo\runs (2)\runs\detect\train\weights\best_int8.onnx"

    # 3. 校准图片目录
    calib_dir = r"D:\yolo\dataset\images\val"

    # 4. 输入尺寸
    input_size = (640, 640)

    # 5. 最多使用多少张校准图片
    max_samples = 100

    if not os.path.exists(fp32_model):
        raise FileNotFoundError(f"找不到 ONNX 模型: {fp32_model}")

    if not os.path.isdir(calib_dir):
        raise FileNotFoundError(f"找不到校准图片目录: {calib_dir}")

    opset_ver = check_opset(fp32_model)
    print("FP32 模型 opset:", opset_ver)

    if opset_ver is not None and opset_ver > 11:
        print("警告: 原始 FP32 模型本身就高于 opset11，后续量化也很难做到真正兼容 opset11。")

    input_name = get_input_name(fp32_model)
    print("ONNX 输入名:", input_name)

    reader = YOLODataReader(
        image_dir=calib_dir,
        input_name=input_name,
        input_size=input_size,
        max_samples=max_samples
    )

    nodes_to_exclude = [
         "/model.22/Concat_3",
        "/model.22/Reshape",
        "/model.22/Reshape_1",
        "/model.22/Reshape_2",
        "/model.22/Concat",
        "/model.22/dfl/Reshape",
        "/model.22/dfl/Transpose",
        "/model.22/dfl/Softmax",
    ]

    quantize_static(
        model_input=fp32_model,
        model_output=int8_model,
        calibration_data_reader=reader,

        quant_format=QuantFormat.QOperator,
        activation_type=QuantType.QUInt8,
        weight_type=QuantType.QInt8,
        calibrate_method=CalibrationMethod.MinMax,
        per_channel=False,
        reduce_range=False,

        nodes_to_exclude=nodes_to_exclude,
    )

    print("INT8 量化完成:", int8_model)


if __name__ == "__main__":
    main()