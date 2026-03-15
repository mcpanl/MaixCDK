[basic]
type = cvimodel
model = hand_landmarks_bf16.cvimodel

[extra]
model_type = hand_landmarks
input_type = rgb
input_channel = hwc
mean = 0, 0, 0
scale = 0.00392156862745098, 0.00392156862745098, 0.00392156862745098
labels = left, right
detect_model = hand_detector.mud
anchors = anchors.csv


