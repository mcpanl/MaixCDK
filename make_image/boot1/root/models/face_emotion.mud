[basic]
type = cvimodel
model = face_emotion_bf16.cvimodel

[extra]
model_type = classifier
input_type = gray
input_channel = hwc
mean = 127.5
scale = 0.00784313725490196
labels = angry, disgust, fear, happy, sad, surprise, neutral



