import cv2
from ultralytics import YOLO

# ====== 改这里 ======
MODEL_PATH = r"D:\yolo\runs\detect\train\weights\best.onnx"   # 也可以改成 best.onnx
CAMERA_INDEX = 1                                            # 0=默认摄像头，1=外接摄像头
IMG_SIZE = 640
CONF_THRES = 0.6
DEVICE = "cpu"     # 有 NVIDIA 显卡可用 0；没有就改成 "cpu"
# ====================

def main():
    # 加载模型
    model = YOLO(MODEL_PATH)

    # 打开摄像头（Windows 下加 CAP_DSHOW 一般更稳）q
    cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_DSHOW)

    if not cap.isOpened():
        print(f"无法打开摄像头：{CAMERA_INDEX}")
        return

    # 可选：设置分辨率
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    print("摄像头已打开，按 q 退出。")

    while True:
        ret, frame = cap.read()
        if not ret:
            print("读取摄像头画面失败")
            break

        # 推理
        results = model.predict(
            source=frame,
            imgsz=IMG_SIZE,
            conf=CONF_THRES,
            device=DEVICE,
            verbose=False
        )

        # 画框
        annotated_frame = results[0].plot()

        # 显示
        cv2.imshow("YOLO Camera", annotated_frame)

        # 按 q 退出
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()