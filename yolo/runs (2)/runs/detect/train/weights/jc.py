import onnx

model = onnx.load("best_int8.onnx")

# 打印输入形状
print("=== 输入形状 ===")
for input in model.graph.input:
    shape = [dim.dim_value for dim in input.type.tensor_type.shape.dim]
    print(f"输入: {input.name}, 形状: {shape}")

# 打印输出形状
print("\n=== 输出形状 ===")
for output in model.graph.output:
    shape = [dim.dim_value for dim in output.type.tensor_type.shape.dim]
    print(f"输出: {output.name}, 形状: {shape}")