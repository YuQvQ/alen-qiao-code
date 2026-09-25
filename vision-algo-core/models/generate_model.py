# generate_model.py — 用 onnx 生成一个可被 OpenCV DNN 加载的图像分类 CNN 模型
# 输出: models/classifier.onnx  +  models/classifier_labels.txt
import numpy as np
import onnx
from onnx import helper, TensorProto, numpy_helper
import os

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
os.makedirs(OUT_DIR, exist_ok=True)

rng = np.random.RandomState(2024)
num_classes = 10

# ---- 权重(随机,但模型结构真实可推理) ----
def w(shape): return rng.randn(*shape).astype(np.float32) * 0.1
def b(n):     return (rng.randn(n).astype(np.float32) * 0.1)

W1 = w((16, 3, 3, 3))          # conv1: 3->16, k3
B1 = b(16)
W2 = w((32, 16, 3, 3))         # conv2: 16->32, k3
B2 = b(32)
W3 = w((64, 32, 3, 3))         # conv3: 32->64, k3
B3 = b(64)
# 经过三次 2x2 maxpool: 224->112->56->28 ; GAP -> 64
Wfc = w((64, num_classes))     # Gemm B: (in_features, out_features) = (K, N)
Bfc = b(num_classes)

init = [
    numpy_helper.from_array(W1, "conv1_w"), numpy_helper.from_array(B1, "conv1_b"),
    numpy_helper.from_array(W2, "conv2_w"), numpy_helper.from_array(B2, "conv2_b"),
    numpy_helper.from_array(W3, "conv3_w"), numpy_helper.from_array(B3, "conv3_b"),
    numpy_helper.from_array(Wfc, "fc_w"),   numpy_helper.from_array(Bfc, "fc_b"),
]

nodes = [
    helper.make_node("Conv", ["input", "conv1_w", "conv1_b"], ["c1"], kernel_shape=[3,3], pads=[1,1,1,1], strides=[1,1]),
    helper.make_node("Relu", ["c1"], ["r1"]),
    helper.make_node("MaxPool", ["r1"], ["p1"], kernel_shape=[2,2], strides=[2,2]),
    helper.make_node("Conv", ["p1", "conv2_w", "conv2_b"], ["c2"], kernel_shape=[3,3], pads=[1,1,1,1], strides=[1,1]),
    helper.make_node("Relu", ["c2"], ["r2"]),
    helper.make_node("MaxPool", ["r2"], ["p2"], kernel_shape=[2,2], strides=[2,2]),
    helper.make_node("Conv", ["p2", "conv3_w", "conv3_b"], ["c3"], kernel_shape=[3,3], pads=[1,1,1,1], strides=[1,1]),
    helper.make_node("Relu", ["c3"], ["r3"]),
    helper.make_node("MaxPool", ["r3"], ["p3"], kernel_shape=[2,2], strides=[2,2]),
    helper.make_node("GlobalAveragePool", ["p3"], ["gap"]),
    helper.make_node("Flatten", ["gap"], ["flat"]),
    helper.make_node("Gemm", ["flat", "fc_w", "fc_b"], ["logits"], alpha=1.0, beta=1.0, transB=0),
    helper.make_node("Softmax", ["logits"], ["output"], axis=1),
]

inputs  = [helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, 224, 224])]
outputs = [helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, num_classes])]

graph = helper.make_graph(nodes, "classifier_cnn", inputs, outputs, initializer=init)
model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 11)])
model.ir_version = 7
onnx.checker.check_model(model)
onnx.save(model, os.path.join(OUT_DIR, "classifier.onnx"))
print("[OK] saved", os.path.join(OUT_DIR, "classifier.onnx"))

# 标签文件
labels = ["class_%d" % i for i in range(num_classes)]
with open(os.path.join(OUT_DIR, "classifier_labels.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(labels) + "\n")
print("[OK] saved labels, classes =", num_classes)
