# generate_det_model.py — 生成 YOLO 格式的目标检测 ONNX 模型
# 输出: detector.onnx (输出形状 [1, num_anchors, 5+num_classes]) + detector_labels.txt
import numpy as np
import onnx
from onnx import helper, TensorProto, numpy_helper
import os

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
rng = np.random.RandomState(7)

num_classes = 10
num_anchors = 100
C = 5 + num_classes          # 15
feat = 64

def w(shape): return rng.randn(*shape).astype(np.float32) * 0.1
def b(n):     return rng.randn(n).astype(np.float32) * 0.1

W1 = w((16, 3, 3, 3)); B1 = b(16)
W2 = w((32, 16, 3, 3)); B2 = b(32)
W3 = w((feat, 32, 3, 3)); B3 = b(feat)
Wfc = w((feat, num_anchors * C))

# 偏置: 让前 3 个 anchor 的 objectness 与最高分类显著 > 0.5 (sigmoid 后)
Bfc = np.zeros(num_anchors * C, dtype=np.float32)
for a in range(num_anchors):
    base = a * C
    Bfc[base + 4] = 2.0 if a < 3 else -3.0   # objectness 列
    Bfc[base + 5] = 2.0 if a < 3 else -3.0   # class_0 分数
    Bfc[base + 0] = 0.0  # cx 基线 0.5
    Bfc[base + 1] = 0.0
    Bfc[base + 2] = -1.0  # w,h 偏小 -> ~0.27
    Bfc[base + 3] = -1.0

shape_const = np.array([1, num_anchors, C], dtype=np.int64)

init = [
    numpy_helper.from_array(W1, "c1w"), numpy_helper.from_array(B1, "c1b"),
    numpy_helper.from_array(W2, "c2w"), numpy_helper.from_array(B2, "c2b"),
    numpy_helper.from_array(W3, "c3w"), numpy_helper.from_array(B3, "c3b"),
    numpy_helper.from_array(Wfc, "fcw"), numpy_helper.from_array(Bfc, "fcb"),
    numpy_helper.from_array(shape_const, "shape"),
]
nodes = [
    helper.make_node("Conv", ["input", "c1w", "c1b"], ["x1"], kernel_shape=[3,3], pads=[1,1,1,1], strides=[2,2]),
    helper.make_node("Relu", ["x1"], ["r1"]),
    helper.make_node("Conv", ["r1", "c2w", "c2b"], ["x2"], kernel_shape=[3,3], pads=[1,1,1,1], strides=[2,2]),
    helper.make_node("Relu", ["x2"], ["r2"]),
    helper.make_node("Conv", ["r2", "c3w", "c3b"], ["x3"], kernel_shape=[3,3], pads=[1,1,1,1], strides=[2,2]),
    helper.make_node("Relu", ["x3"], ["r3"]),
    helper.make_node("GlobalAveragePool", ["r3"], ["gap"]),
    helper.make_node("Flatten", ["gap"], ["flat"]),
    helper.make_node("Gemm", ["flat", "fcw", "fcb"], ["logits"]),
    helper.make_node("Reshape", ["logits", "shape"], ["det_raw"]),
    helper.make_node("Sigmoid", ["det_raw"], ["output"]),
]
inputs  = [helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, 416, 416])]
outputs = [helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, num_anchors, C])]
graph = helper.make_graph(nodes, "detector_yolo", inputs, outputs, initializer=init)
model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
model.ir_version = 7
onnx.checker.check_model(model)
out_path = os.path.join(OUT_DIR, "detector.onnx")
onnx.save(model, out_path)
print("[OK] saved", out_path)

labels = ["cat", "dog", "car", "person", "bus", "bike", "bird", "boat", "chair", "bottle"]
with open(os.path.join(OUT_DIR, "detector_labels.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(labels) + "\n")
print("[OK] saved labels (%d classes)" % len(labels))
