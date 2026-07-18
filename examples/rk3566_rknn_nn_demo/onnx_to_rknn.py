from rknn.api import RKNN
onnx_model = 'mobilenetv2-12.onnx'
rknn_model = 'mobilenetv2-12.rknn'
rknn = RKNN(verbose=True)
ret = rknn.config(target_platform='rk3566')
if ret != 0:
    raise RuntimeError(f'config failed: {ret}')
ret = rknn.load_onnx(model=onnx_model)
if ret != 0:
    raise RuntimeError(f'load_onnx failed: {ret}')
ret = rknn.build(do_quantization=False)
if ret != 0:
    raise RuntimeError(f'build failed: {ret}')
ret = rknn.export_rknn(rknn_model)
if ret != 0:
    raise RuntimeError(f'export_rknn failed: {ret}')
rknn.release()
print('export ok:', rknn_model)
