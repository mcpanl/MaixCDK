[basic]
type = rknn
model = yolov5n_224.rknn

[extra]
model_type = yolov5
input_type = rgb
mean = 0, 0, 0
scale = 0.00392156862745098, 0.00392156862745098, 0.00392156862745098
input_channel = chw
labels = coco80.txt
anchors = 3.5, 4.55, 5.6, 10.5, 11.55, 8.05, 10.5, 21.35, 21.7, 15.75, 20.65, 41.65, 40.6, 31.5, 54.6, 69.3, 130.55, 114.1
