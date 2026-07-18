from maix import display, image, time
import threading
from loading import Loading

font_size = 20
image.load_font("sourcehansans", "/maixapp/share/font/SourceHanSansCN-Regular.otf", size = font_size)  # 加载自定义字体
image.set_default_font("sourcehansans")  # 设置默认字体

disp = display.Display()
show_loading = True
loaindg = Loading(disp)
loading_thread = None
display_show_lock = threading.Lock()

# 加载动画，import 逻辑放到加载动画后面用来以防黑屏等待提升用户体验
def loading_thread_main():
    global show_loading, loading_thread

    should_break = False

    while 1:
        with display_show_lock:
            if show_loading:
                img = loaindg.draw()
                disp.show(img)
            else:
                should_break = True
        
        if should_break:
            break

        time.sleep_ms(5)

loading_thread = threading.Thread(target=loading_thread_main, daemon=True)
loading_thread.start()

from maix import key

longpress_lock = False

def on_user_key(key_id, state):
    '''
        this func called in a single thread
    '''
    global CONFIRM_BTN_CLICKED, FUNC_STATUS, IS_HIDE_KEYPOINTS, show_delete_popup, longpress_lock

    try:
        if show_learn_list_content:
            return

        if key_id == 352:
            if state == 0 and longpress_lock == False:
                try:
                    if current_tab == 0:
                        FUNC_STATUS["keypoint"] = not FUNC_STATUS["keypoint"]
                        return
                    
                    print('FUNC_STATUS["learn"]', FUNC_STATUS["learn"])

                    if current_tab == 1:
                        if FUNC_STATUS["learn"]:
                            show_delete_popup = True
                        else:
                            if cross_exsit():
                                print("按下学习按钮, 矩形存在人脸")
                                lResult = learn_face()
                                if lResult:
                                    print("学习成功")
                                else:
                                    print("学习失败")
                            else:
                                print("按下学习按钮，矩形不存在人脸")


                except Exception as e:
                    print(e)
            elif state == 1:
                longpress_lock = False
            elif state == 2:
                longpress_lock = True
    except Exception as e:
            pass
    
# 覆盖原来的用户按钮事件
key_obj = key.Key(on_user_key)

import math
import os
import select
import socket
import traceback
from delete_popup import DeletePopup
from maix import app, camera, nn, pinmap, touchscreen, err, gpio
from utils import is_in_button, truncate_string_by_width

recognizer_lock = threading.Lock()
img_no_disp = None
detector = nn.YOLOv8(model="/root/models/yolov8n_face.mud", dual_buff=False)
# 临时禁用 Landmark 与 FaceRecognizer，仅使用上方 detector
RECOGNIZER_AND_LANDMARKS_DISABLED = True
# landmarks_detector = nn.FaceLandmarks(model="/root/models/face_landmarks.mud")
# recognizer = nn.FaceRecognizer(detect_model="/root/models/yolov8n_face.mud", feature_model = "/root/models/insghtface_webface_r50.mud", dual_buff=False)
# 临时关闭情绪分类模型
# classifier = nn.Classifier(model="/root/models/face_emotion.mud", dual_buff=False)
faces_path = "/mk/faces.bin"
pressed_already = False
last_x = 0
last_y = 0
last_pressed = False

show_key_point_btn_img = image.load('./assets/images/show_key_point_btn.png', image.Format.FMT_RGBA8888)
hide_key_point_btn_img = image.load('./assets/images/hide_key_point_btn.png', image.Format.FMT_RGBA8888)
learn_btn_img = image.load('./assets/images/learn_btn.png', image.Format.FMT_RGBA8888)
del_learn_btn_img = image.load('./assets/images/del_learn_btn.png', image.Format.FMT_RGBA8888)
face_learning_btn_section_img = image.load('./assets/images/face_learning_btn_section.png', image.Format.FMT_RGBA8888)
face_learning_active_img = image.load('./assets/images/face_learning_active.png', image.Format.FMT_RGBA8888)
face_learning_unactive_img = image.load('./assets/images/face_learning_unactive.png', image.Format.FMT_RGBA8888)
face_detection_btn_section_img = image.load('./assets/images/face_detection_btn_section.png', image.Format.FMT_RGBA8888)
face_detection_active_img = image.load('./assets/images/face_detection_btn_active.png', image.Format.FMT_RGBA8888)
face_detection_unactive_img = image.load('./assets/images/face_detection_btn_unactive.png', image.Format.FMT_RGBA8888)
face_emotion_active_img = image.load('./assets/images/emotion_recognition_active.png', image.Format.FMT_RGBA8888)
face_emotion_unactive_img = image.load('./assets/images/emotion_recognition_unactive.png', image.Format.FMT_RGBA8888)
emotion_recognition_btn_section_img = image.load('./assets/images/emotion_recognition_btn_section.png', image.Format.FMT_RGBA8888)
emotion_recognition_btn_section2_img = image.load('./assets/images/emotion_recognition_btn_section2.png', image.Format.FMT_RGBA8888)
happy_img = image.load('./assets/images/happy.png', image.Format.FMT_RGBA8888)
disgust_img = image.load('./assets/images/disgust.png', image.Format.FMT_RGBA8888)
fear_img = image.load('./assets/images/fear.png', image.Format.FMT_RGBA8888)
angry_img = image.load('./assets/images/angry.png', image.Format.FMT_RGBA8888)
sad_img = image.load('./assets/images/sad.png', image.Format.FMT_RGBA8888)
surprise_img = image.load('./assets/images/surprise.png', image.Format.FMT_RGBA8888)
neutral_img = image.load('./assets/images/neutral.png', image.Format.FMT_RGBA8888)
list_img = image.load('./assets/images/list.png', image.Format.FMT_RGBA8888)
list_img_xywh = [564, 402, list_img.width(), list_img.height()]
learned_faces_img = image.load('./assets/images/learned_faces.png', image.Format.FMT_RGBA8888)
learned_faces_img_xywh = [86, 18, learned_faces_img.width(), learned_faces_img.height()]
icon_delete_img = image.load('./assets/images/icon_delete.png', image.Format.FMT_RGBA8888)

mood_map = {
    "angry": ["生气", "眉毛内收", "Annoyed", angry_img.resize(25, 25)],
    "disgust": ["恶心","鼻子皱起", "Disgust", disgust_img.resize(25, 25)],
    "fear": ["害怕", "眼睛睁大", "Nervous", fear_img.resize(25, 25)],
    "happy": ["高兴", "微笑", "Smile", happy_img.resize(25, 25)],
    "sad": ["悲伤", "皱眉", "Frown", sad_img.resize(25, 25)],
    "surprise": ["惊讶", "张嘴", "Shout", surprise_img.resize(25, 25)],
    "neutral": ["自然", "中性", "Neutral", neutral_img.resize(25, 25)]
}


cross_size = 72
cross_coord = (640//2 - cross_size//2, 480//2, 640//2 + cross_size//2, 480//2, 640//2, 480//2 - cross_size//2, 640//2, 480//2 + cross_size//2)
cross_rect = (284, 204, 356, 276)
sub_68_idxes = [162, 234, 93, 58, 172, 136, 149, 148, 152, 377, 378, 365, 397, 288, 323, 454, 389, 71, 63, 105, 66, 107, 336,
                296, 334, 293, 301, 168, 197, 5, 4, 75, 97, 2, 326, 305, 33, 160, 158, 133, 153, 144, 362, 385, 387, 263, 373,
                380, 61, 39, 37, 0, 267, 269, 291, 405, 314, 17, 84, 181, 78, 82, 13, 312, 308, 317, 14, 87]
detect_conf_th = 0.5
detect_iou_th = 0.45
landmarks_conf_th = 0.5
landmarks_abs = True
landmarks_rel = False
max_face_num = 2

# recognizer = nn.FaceRecognizer(detect_model="/root/models/retinaface.mud", feature_model = "/root/models/face_feature.mud", dual_buff=True)
IS_HIDE_KEYPOINTS = False
IS_REVERSE_WH = False
CONFIRM_BTN_CLICKED = False
LEARN_TEST_SIGNAL = False
switch_status = False
ts = touchscreen.TouchScreen()
FACE_LEARN_MODE = True
show_learn_list_content = False
FUNC_STATUS = {"keypoint": True, "learn": False}

cam = camera.Camera(disp.width(), disp.height(), image.Format.FMT_RGB888)
# cam_min = cam.add_channel(recognizer.input_width(), recognizer.input_height(), recognizer.input_format())
cam_min = cam.add_channel(detector.input_width(), detector.input_height(), detector.input_format())

delete_popup = DeletePopup(disp) # 删除确认弹窗
show_delete_popup = False
current_delete_label_name = ""
cam_flip_init = False
out_side_labels = []
current_page = 1 # 当前页码
total_page = 0 # 总页数
pagination_num = 4 # 每页 x 条数据
pagination_list = [] # 分页列表数据

prev_page_btn_img = image.load('./assets/images/prev_page_btn.png', image.Format.FMT_RGBA8888)
prev_page_btn_right_margin = 45
prev_page_btn_xywh = [disp.width() // 2 - prev_page_btn_img.width() - prev_page_btn_right_margin, disp.height() - prev_page_btn_img.height(), prev_page_btn_img.width(), prev_page_btn_img.height()]

next_page_btn_img = image.load('./assets/images/next_page_btn.png', image.Format.FMT_RGBA8888)
next_page_btn_left_margin = 45
next_page_btn_xywh = [disp.width() // 2 + next_page_btn_left_margin, disp.height() - next_page_btn_img.height(), next_page_btn_img.width(), next_page_btn_img.height()]

# 加载本地的人脸数据（依赖 recognizer，临时禁用时跳过）
# if os.path.exists(faces_path):
#     recognizer.load_faces(faces_path)
#     face_label_list = recognizer.labels[1:] # 第一个是 unknow，所以需要去掉第一个
#     if len(face_label_list) > 0:
#         for index, item in enumerate(face_label_list):
#             out_side_labels.append({
#                 "id": index + 1,
#                 "label_name": item
#             })

scale_x = cam.width() / cam_min.width()
scale_y = cam.height() / cam_min.height()

def scale_points(coords,scale_x,scale_y):
    scaled_coords=[]
    for i in range(0, len(coords),2):
        x=round(coords[i]*scale_x) 
        y=round(coords[i+1]*scale_y)
        scaled_coords.extend([x, y])
    return scaled_coords

def scale_obj(obj):
    try:
        obj.x = round(obj.x * scale_x)
        obj.y = round(obj.y * scale_y)
        obj.w = round(obj.w * scale_x)
        obj.h = round(obj.h * scale_y)
        obj.points = scale_points(obj.points, scale_x, scale_y)

        return obj
    except Exception as e:
        print(e)
        return obj

def set_camera_flip(state):
    global cam_flip_init
    cam_min.vflip(state)
    # cam_min.hmirror(state)
    cam.vflip(state)
    # cam.hmirror(state)
    cam_flip_init = True


def wait_camera_flip_init():
    while(not cam_flip_init):
        try:
            send_socket_message('global_get_camera_flip_status')
        except Exception as e:
            print(e)
        time.sleep_ms(250)

accept_thread2 = threading.Thread(target=wait_camera_flip_init)
accept_thread2.daemon = True
accept_thread2.start()


# Unix 域套接字路径
socket_path = "/tmp/my_socket"

client_socket = None
client_socket_status = False

socket_rx_string_buffer = ''


gFrameFaceResult = []

if IS_REVERSE_WH:
    RESOLUTION = (disp.height(), disp.width())
else:
    RESOLUTION = (disp.width(), disp.height())

center = (RESOLUTION[0] // 2, RESOLUTION[1] // 2)
margin = 12
exit_label = "<"
back_btn_img = image.load("./assets/images/back_btn.png", image.Format.FMT_RGBA8888)
exit_size = image.string_size(exit_label, scale=2)
exit_pos_x = margin
exit_pos_y = margin
exit_pos_w = margin * 2 + exit_size.width()
exit_pos_h = margin * 2 + exit_size.height()
exit_btn_pos = [6, 6, exit_pos_w + 30, exit_pos_h + 30]  # fit img rect

func_size = image.string_size(exit_label, scale=2)
func_pos_w = margin * 2 + func_size.width()
func_pos_h = margin * 2 + func_size.height()
func_btn_pos = [562, 6, func_pos_w + 30, func_pos_h + 30] # fit img rect

face_btn_pos = [98, 18, face_detection_active_img.width(), face_detection_active_img.height()]
face_learn_xywh = [315, 18, face_learning_active_img.width(), face_learning_active_img.height()]
face_learning_active_img_xywh = [200, 18, face_learning_active_img.width(), face_learning_active_img.height()]
emotion_pos = [243, 18, face_emotion_active_img.width(), face_emotion_active_img.height()]
emotion_recognition_btn_section_img_xywh = [522, 18, emotion_recognition_btn_section_img.width(), emotion_recognition_btn_section_img.height()]
emotion_recognition_btn_section2_img_xywh = [407, 18, emotion_recognition_btn_section2_img.width(), emotion_recognition_btn_section2_img.height()]
face_learning_btn_section_img_xywh = [98, 18, face_learning_btn_section_img.width(), face_learning_btn_section_img.height()]

current_tab = 1

def clamp(x, lo, hi):
    return max(lo, min(x, hi))

def key_init():
    global key_swith_mid, key_swith_right, key_swith_left

    pinmap.set_pin_function("A22", "GPIOA22")
    pinmap.set_pin_function("A23", "GPIOA23")
    pinmap.set_pin_function("A25", "GPIOA25")

    try:
        key_swith_mid = gpio.GPIO("GPIOA22", gpio.Mode.IN)
    except Exception as e:
        print(e)

    try:
        key_swith_right = gpio.GPIO("GPIOA23", gpio.Mode.IN)
    except Exception as e:
        print(e)

    try:
        key_swith_left = gpio.GPIO("GPIOA25", gpio.Mode.IN)
    except Exception as e:
        print(e)
    
    print("key_init done")

def key_scan():
    global show_learn_list_content, show_delete_popup
    
    last_key_time = time.ticks_ms()
    last_key_state = 0
    key_down_count = 0
    key_init()
    while not app.need_exit():
        

        # 实现10ms扫描一次按键
        if time.ticks_ms() - last_key_time > 60:
            last_key_time = time.ticks_ms()
            if key_swith_mid.value() == 0:
                if key_down_count == 2:
                    show_delete_popup = False

                    if show_learn_list_content:
                        show_learn_list_content = False
                    else:
                        app.set_exit_flag(True)
                        break
                key_down_count += 1
            elif key_swith_right.value() == 0:
                if show_learn_list_content:
                    continue

                if key_down_count == 1:
                    on_right()

                key_down_count += 1
            elif key_swith_left.value() == 0:
                if show_learn_list_content:
                    continue

                if key_down_count == 1:
                    on_left()

                key_down_count += 1
            else:
                key_down_count = 0
        time.sleep_ms(10)

key_thread = threading.Thread(target=key_scan)
key_thread.daemon = True
key_thread.start()


'''
👇单向切换
'''
def on_right():
    global current_tab, switch_status
    if current_tab < 2:
        current_tab += 1
    switch_status = current_tab == 2

def on_left():
    global current_tab, switch_status
    if current_tab > 0:
        current_tab -= 1
    switch_status = current_tab == 2

'''
👇循环切换
'''
# def on_right():
#     global current_tab, switch_status
#     if current_tab == 0:
#         current_tab = 1
#         switch_status = False
#     elif current_tab == 1:
#         current_tab = 2
#         switch_status = True
#     else:
#         switch_status = False
#         current_tab = 0

# def on_left():
#     global current_tab, switch_status
#     if current_tab == 0:
#         switch_status = True
#         current_tab = 2
#     elif current_tab == 1:
#         switch_status = False
#         current_tab = 0
#     else:
#         switch_status = False
#         current_tab = 1

def get_sub_landmarks(points, points_z, idxes):
    new_points = []
    new_points_z = []
    for i in idxes:
        new_points.append(points[i*2])
        new_points.append(points[i*2 + 1])
        new_points_z.append(points_z[i])
    return new_points, new_points_z

class LearnFace:
    def add_learned_faces(self, obj, name=None):
        global out_side_labels, FUNC_STATUS

        try:
            if RECOGNIZER_AND_LANDMARKS_DISABLED:
                print("[人脸学习Inner] FaceRecognizer 已临时关闭")
                return False

            FUNC_STATUS["learn"] = True

            if len(out_side_labels) == 0:
                _id = 1
            else:
                _id = out_side_labels[-1]["id"] + 1
            
            label_name = ""
            
            if name != None:
                label_name = name
            else:
                name_id = 1
                
                if len(out_side_labels) > 0:
                    reversed_id = -1
                    while (abs(reversed_id) <= len(out_side_labels)):
                        name_list = out_side_labels[reversed_id]["label_name"].split(" ")
                        if len(name_list) == 2 and name_list[0] == "Face" and name_list[1].isdigit():
                            name_id = int(name_list[1]) + 1 # 获取名称里面的 ID
                            break
                        else:
                            reversed_id -= 1

                label_name = f"Face {name_id}"
            
            with recognizer_lock:
                error_maix = recognizer.add_face(obj, label_name)

                if error_maix == err.Err.ERR_NONE:
                    out_side_labels.append({
                        "id": _id, # id 需要和 class_id 同步
                        "label_name": label_name
                    })
                    recognizer.save_faces(faces_path)

                    return True
                else:
                    print(error_maix)
                    return False
        except Exception as e:
            print(f"[人脸学习Inner] 学习失败：{e}")
            return False


    def set_face_name(self, origin_name, new_name):
        global out_side_labels
        
        try:
            if RECOGNIZER_AND_LANDMARKS_DISABLED:
                return False

            faces = recognizer.recognize(img_no_disp, 0.5, 0.45, 0.85, True, True)
            face_in_screen = False
            is_exist = False

            if new_name == None:
                print(f"[人脸重命名] 失败 新名称为None")
                return False
            
            for item in out_side_labels:
                if item["label_name"] == new_name:
                    is_exist = True
                    break
            
            # 如果名称已存在
            if is_exist:
                return False

            for item in faces:
                if out_side_labels[item.class_id - 1]["label_name"] == origin_name:
                    face_in_screen = True
                    break
            
            # 如果当前要重命名的人脸没显示在屏幕内，则无法重命名
            if not face_in_screen:
                return False
            
            self.remove_learned_faces(origin_name)
            time.sleep_ms(500) # 删除人脸后需要间隔一会才能新增人脸，不然没效果
            learn_face(new_name)
            recognizer.save_faces(faces_path)
            print(f"[人脸重命名] 重命名{origin_name} -> {new_name} 成功")

            return True
        except Exception as e:
            print(f"[人脸重命名] 重命名{origin_name} -> {new_name} 失败： {e}")
            return False

    def remove_learned_faces(self, name_i):
        global out_side_labels, FUNC_STATUS

        with recognizer_lock:
            try:
                if RECOGNIZER_AND_LANDMARKS_DISABLED:
                    return False

                want_del_index = -1

                for index, item in enumerate(out_side_labels):
                    if item["label_name"] == name_i:
                        want_del_index = index
                        print(f"[DELETE FACE CONFIRM] 即将删除{name_i}对应的id {index}")
                        break
                
                if want_del_index != -1:
                    if name_i == "":
                        print("使用idx删除了人脸标签")
                        recognizer.remove_face(idx=recognizer.labels.index(name_i) - 1, label=name_i)  # 当删除人脸名为''时，需要用idx删除
                    else:
                        recognizer.remove_face(-1, name_i) # 根据标签名称删除人脸

                    for index, item in enumerate(out_side_labels):
                        if index > want_del_index:
                            item["id"] -= 1 # 因为执行 remove_face 后，class_id 会减 1，所以需要和 class_id 同步对应

                    del out_side_labels[want_del_index] # 删除该人脸的标签

                    FUNC_STATUS["learn"] = False

                    recognizer.save_faces(faces_path)

                    return True
                return False
            except Exception as e:
                traceback.print_exc()
                print(f"[DELETE FACE CONFIRM] {e}")
                return False
            

    def remove_all_learned_faces(self):
        global out_side_labels

        if RECOGNIZER_AND_LANDMARKS_DISABLED:
            out_side_labels = []
            return

        for i in range(len(recognizer.labels) - 1):
            recognizer.remove_face(0)

        out_side_labels = []

        recognizer.save_faces(faces_path)

class Commander:
    def __init__(self, message) -> None:
        self.message = message.split("\n")
        self.cmds = {}
        for i in range(len(self.message)):
            var_name = f"var{i}"
            self.cmds[var_name] = self.message[i]

    def get_learn_center_face_command(self, success: int):
        return f"learn_face\nfull\n{success}"

    def get_rename_face_command(self, success:int):
        return f"set_face_name\nfull\n{success}"

    def get_remove_faces_command(self):
        return f"delete_face_all\nfull\n1"

    def get_remove_special_face_command(self, success:int):
        return f"delete_face_by_name\nfull\n{success}"

    def get_find_face_info_command(self, is_empty, value=None):
        empty_str = "empty" if is_empty else "full"
        if is_empty:
            return f"get_face_info\n{empty_str}"
        else:
            return f"get_face_info\n{empty_str}\n{value}"

    def get_find_face_mood_command(self, is_empty, value=None):
        empty_str = "empty" if is_empty else "full"
        if is_empty:
            return f"get_face_mood\n{empty_str}"
        else:
            return f"get_face_mood\n{empty_str}\n{value}"

    def get_find_face_info_by_name_command(self, is_empty, value=None):
        empty_str = "empty" if is_empty else "full"
        if is_empty:
            return f"get_face_info_by_name\n{empty_str}"
        else:
            return f"get_face_info_by_name\n{empty_str}\n{value}"

    def get_find_face_mood_by_name_command(self, is_empty, value=None):
        empty_str = "empty" if is_empty else "full"
        if is_empty:
            return f"get_face_mood_by_name\n{empty_str}"
        else:
            return f"get_face_mood_by_name\n{empty_str}\n{value}"

study_face = LearnFace()

class Face:
    def __init__(self, x, y, w, h, obj, conf, label_id=None, label_name=None, is_learned=None, emotion=None, emotion_conf=None) -> None:
        self.x = x
        self.y = y
        self.w = w
        self.h = h
        self.conf = conf
        self.label_id = label_id
        self.label_name = label_name
        self.is_learned = is_learned
        self.obj = obj
        self.emotion = emotion
        self.emotion_conf = emotion_conf

        

class FrameResult:
    def __init__(self, time_stamp, results: list[Face]) -> None:
        self.time_stamp = time_stamp
        self.results = results

def get_frame_content_lastest():
    if len(gFrameFaceResult) > 0:
        return gFrameFaceResult[-1]
    else:
        return None

def send_socket_message(message:str):
    try:
        message_packet = f"@#{message}#@"
        print('[发送成功]\n', message_packet)
        if client_socket:
            client_socket.sendall(message_packet.encode())
    except Exception as e:
        print(e)

def handler_socket_message(message: str):
    global FUNC_STATUS

    cmds = message.split("\n")

    print(cmds)

    if cmds[0] == 'PASS' :
        return
    elif cmds[0] == "global_set_camera_flip_status":
        value = cmds[1]
        if value == "1":
            set_camera_flip(1)
        else:
            set_camera_flip(0)

        return

    commander = Commander(message)
    if commander.cmds["var0"] == "learn_face":
        face_initial_name = commander.cmds["var1"]
        if cross_exsit():
            if learn_face(face_initial_name):
                send_socket_message(commander.get_learn_center_face_command(1))
                FUNC_STATUS["learn"] = True
            else:
                send_socket_message(commander.get_learn_center_face_command(2))
        else:
            send_socket_message(commander.get_learn_center_face_command(2))
              

    elif commander.cmds["var0"] == "set_face_name":
        face_origin_name: str = commander.cmds["var1"]
        face_set_name: str = commander.cmds["var2"]
        if study_face.set_face_name(face_origin_name, face_set_name):
            send_socket_message(commander.get_rename_face_command(1))
        else:
            send_socket_message(commander.get_rename_face_command(2))

    elif commander.cmds["var0"] == "delete_face_all":
        study_face.remove_all_learned_faces()
        send_socket_message(commander.get_remove_faces_command())
        FUNC_STATUS["learn"] = False

    elif commander.cmds["var0"] == "delete_face_by_name":
        face_name = commander.cmds["var1"]
        if study_face.remove_learned_faces(face_name):
            send_socket_message(commander.get_remove_special_face_command(1))
            FUNC_STATUS["learn"] = False
        else:
            send_socket_message(commander.get_remove_special_face_command(2))

    elif commander.cmds["var0"] == "get_face_info":
        position = commander.cmds["var1"]
        return_value = commander.cmds["var2"]
        # 根据 position 设置人脸选择模式
        if position == "1":
            mode = "center"         # 最中间
        elif position == "2":
            mode = "leftmost"         # 最靠左
        elif position == "3":
            mode = "rightmost"         # 最靠右
        elif position == "4":
            mode = "topmost"         # 最靠上
        elif position == "5":
            mode = "bottommost"         # 最靠下
        elif position == "6":
            mode = "areamax"           # 最大
        elif position == "7":
            mode = "areamin"           # 最小
        elif position == "8":
            mode = "confmax"     # 置信度最高
        elif position == "9":
            mode = "confmin"     # 置信度最低
        elif position == "10":
            mode = "happy"           # 高兴
        elif position == "11":
            mode = "neutral"           # 平静
        elif position == "12":
            mode = "surprise"           # 惊讶
        elif position == "13":
            mode = "sad"           # 悲伤
        elif position == "14":
            mode = "angry"           # 生气
        elif position == "15":
            mode = "disgust"           # 厌恶
        elif position == "16":
            mode = "fear"           # 恐惧

        else:
            raise ValueError(f"check message")

        result = get_face_info(mode=mode)
        emotions = ["10", "11", "12", "13", "14", "15", "16"]

        if result:
            if return_value == "1":
                send_socket_message(commander.get_find_face_info_command(False, result.x + result.w // 2))

            elif return_value == "2":
                send_socket_message(commander.get_find_face_info_command(False, result.y + result.h // 2))

            elif return_value == "3":
                send_socket_message(commander.get_find_face_info_command(False, result.w))

            elif return_value == "4":
                send_socket_message(commander.get_find_face_info_command(False, result.h))

            elif return_value == "5":
                if position in emotions:
                    send_socket_message(commander.get_find_face_info_command(False, result.emotion_conf))
                else:
                    send_socket_message(commander.get_find_face_info_command(False, result.conf))

        else:
            send_socket_message(commander.get_find_face_info_command(True))

    elif commander.cmds["var0"] == "get_face_mood":
        position = commander.cmds["var1"]
        return_value = commander.cmds["var2"]
        # 根据 position 设置人脸选择模式
        if position == "1":
            mode = "center"         # 最中间
        elif position == "2":
            mode = "leftmost"         # 最靠左
        elif position == "3":
            mode = "rightmost"         # 最靠右
        elif position == "4":
            mode = "topmost"         # 最靠上
        elif position == "5":
            mode = "bottommost"         # 最靠下
        elif position == "6":
            mode = "areamax"           # 最大
        elif position == "7":
            mode = "areamin"           # 最小
        elif position == "8":
            mode = "emotion_confmax"     # 置信度最高
        elif position == "9":
            mode = "emotion_confmin"     # 置信度最低
        elif position == "10":
            mode = "happy"           # 高兴
        elif position == "11":
            mode = "neutral"           # 平静
        elif position == "12":
            mode = "surprise"           # 惊讶
        elif position == "13":
            mode = "sad"           # 悲伤
        elif position == "14":
            mode = "angry"           # 生气
        elif position == "15":
            mode = "disgust"           # 厌恶
        elif position == "16":
            mode = "fear"           # 恐惧

        else:
            raise ValueError(f"check message")

        result = get_face_info(mode=mode)

        if result:
            if return_value == "1":
                send_socket_message(commander.get_find_face_mood_command(False, result.emotion))

            elif return_value == "2":
                send_socket_message(commander.get_find_face_mood_command(False, result.label_name))

        else:
            send_socket_message(commander.get_find_face_mood_command(True))


    elif commander.cmds["var0"] == "get_face_info_by_name":
        name = commander.cmds["var1"]
        return_value = commander.cmds["var2"]
        result = get_face_info(mode="name", name=name)

        if result:
            if return_value == "1":
                send_socket_message(commander.get_find_face_info_by_name_command(False, result.x + result.w // 2))

            elif return_value == "2":
                send_socket_message(commander.get_find_face_info_by_name_command(False, result.y + result.h // 2))

            elif return_value == "3":
                send_socket_message(commander.get_find_face_info_by_name_command(False, result.w))

            elif return_value == "4":
                send_socket_message(commander.get_find_face_info_by_name_command(False, result.h))

            elif return_value == "5":
                send_socket_message(commander.get_find_face_info_by_name_command(False, result.conf))

        else:
            send_socket_message(commander.get_find_face_info_by_name_command(True))

    elif commander.cmds["var0"] == "get_face_mood_by_name":
        name = commander.cmds["var1"]
        return_value = commander.cmds["var2"]
        result = get_face_info(mode="name", name=name)

        if result:
            if return_value == "1":
                send_socket_message(commander.get_find_face_mood_by_name_command(False, result.emotion))

            elif return_value == "2":
                send_socket_message(commander.get_find_face_mood_by_name_command(False, result.label_name))

        else:
            send_socket_message(commander.get_find_face_mood_by_name_command(True))



def socket_worker():
    global client_socket_status, client_socket, socket_rx_string_buffer
    while not app.need_exit():
        if not client_socket_status:
            if os.path.exists(socket_path):
                client_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                client_socket.connect(socket_path)
                client_socket_status = True
                print("Socket连接成功")
            time.sleep_ms(500)
            continue
        
        if not client_socket:
            time.sleep_ms(500)
            continue

        ready_to_read, _, _ = select.select([client_socket], [], [], 0.25)  # 0.25秒超时

        if ready_to_read:
            # 接收服务端的消息
            data = client_socket.recv(1024)
            if data:
                # print(f"Received from server: {data.decode()}")
                tmp_str = data.decode()
                socket_rx_string_buffer += tmp_str
                while True:
                    start_idx = socket_rx_string_buffer.find('@#')
                    end_idx = socket_rx_string_buffer.find('#@')
                    if start_idx != -1 and end_idx != -1 and start_idx < end_idx:
                        packet = socket_rx_string_buffer[start_idx+2 : end_idx]
                        
                        # print('[收到完整数据包]\n', packet)
                        cmds = packet.split("\n")
                        if cmds[0] == 'global_debug':
                            val1 = int(cmds[1])
                            val2 = int(cmds[2])
                            try:
                                disp.set_debug_info(val1, val2)
                            except Exception as e:
                                pass
                        else:
                            handler_socket_message(packet)
                        
                        socket_rx_string_buffer = socket_rx_string_buffer[end_idx+2:]
                    else:
                        # 没有完整数据包时退出循环
                        break
            else:
                # 如果接收到空数据，说明连接已关闭
                print("Connection closed by server")

        time.sleep_ms(25)

accept_thread = threading.Thread(target=socket_worker)
accept_thread.daemon = True
accept_thread.start()

def learn_face(initial_name=None):
    closest_obj = get_face_info(mode="center")
    
    if not closest_obj:
        print("[人脸学习] 最近帧没有相关人脸")
        return False
    else:
        if closest_obj.is_learned:
            print("[人脸学习] 屏幕中央人脸已被学习")
            return False
                    

    if study_face.add_learned_faces(closest_obj.obj, initial_name):
        print("[人脸学习] 人脸学习成功")
        return True
    else:
        print("[人脸学习] 人脸学习失败")
        return False



def cross_learned_exsit():
    exsit = False
    frame_content = get_frame_content_lastest()
    if not frame_content:
        return exsit
    for result in frame_content.results:
        if not result.is_learned:
            pass
        else:
            x1 = max(result.x, 0)
            y1 = max(result.y, 0)
            face_rect = [x1, y1, result.w, result.h]
            if is_in_button(center[0], center[1], face_rect):
                exsit = True

                return exsit

    return exsit

def cross_exsit():
    exsit = False
    frame_content = get_frame_content_lastest()
    if not frame_content:
        return exsit
    for result in frame_content.results:
        x1 = max(result.x, 0)
        y1 = max(result.y, 0)
        face_rect = [x1, y1, result.w, result.h]
        if is_in_button(center[0], center[1], face_rect):
            exsit = True
            return exsit

    return exsit



def get_face_info(mode:str, *args, **kwargs):
    if mode=="center":
        orientations = []
        center_x = RESOLUTION[0] / 2
        center_y = RESOLUTION[1] / 2 
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            x_mid = result_indvi.x + result_indvi.w // 2
            y_mid = result_indvi.y + result_indvi.h // 2
            distance = math.sqrt((x_mid - center_x) ** 2 + (y_mid - center_y) ** 2)
            area_dict = {"result": result_indvi, "distance": distance}
            orientations.append(area_dict)
        if orientations:
            closest_item = min(orientations, key=lambda x: x['distance'])
            return closest_item["result"]
        else:
            return None

    elif mode=="leftmost":
        orientations = []
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            x = result_indvi.x
            area_dict = {"result": result_indvi, "x": x}
            orientations.append(area_dict)

        if orientations:
            closest_item = min(orientations, key=lambda x: x['x'])
            return closest_item["result"]
        else:
            return None

    elif mode=="rightmost":
        orientations = []
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            x = result_indvi.x
            area_dict = {"result": result_indvi, "x": x}
            orientations.append(area_dict)

        if orientations:
            closest_item = max(orientations, key=lambda x: x['x'])
            return closest_item["result"]
        else:
            return None

    elif mode=="topmost":
        orientations = []
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            y = result_indvi.y
            area_dict = {"result": result_indvi, "y": y}
            orientations.append(area_dict)

        if orientations:
            closest_item = min(orientations, key=lambda x: x['y'])
            return closest_item["result"]
        else:
            return None

    elif mode=="bottommost":
        orientations = []
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            y = result_indvi.y
            area_dict = {"result": result_indvi, "y": y}
            orientations.append(area_dict)

        if orientations:
            closest_item = max(orientations, key=lambda x: x['y'])
            return closest_item["result"]
        else:
            return None

    elif mode=="areamax":
        orientations = []
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            area = result_indvi.w * result_indvi.h
            area_dict = {"result": result_indvi, "area": area}
            orientations.append(area_dict)

        if orientations:
            closest_item = max(orientations, key=lambda x: x['area'])
            return closest_item["result"]
        else:
            return None

    elif mode=="areamin":
        orientations = []
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            area = result_indvi.w * result_indvi.h
            area_dict = {"result": result_indvi, "area": area}
            orientations.append(area_dict)

        if orientations:
            closest_item = min(orientations, key=lambda x: x['area'])
            return closest_item["result"]
        else:
            return None

    elif mode=="confmax":
        orientations = []
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            conf = result_indvi.conf
            area_dict = {"result": result_indvi, "conf": conf}
            orientations.append(area_dict)

        if orientations:
            closest_item = max(orientations, key=lambda x: x['conf'])
            return closest_item["result"]
        else:
            return None

    elif mode=="confmin":
        orientations = []
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            conf = result_indvi.conf
            area_dict = {"result": result_indvi, "conf": conf}
            orientations.append(area_dict)

        if orientations:
            closest_item = min(orientations, key=lambda x: x['conf'])
            return closest_item["result"]
        else:
            return None

    elif mode=="emotion_confmax":
        orientations = []
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            conf = result_indvi.emotion_conf
            area_dict = {"result": result_indvi, "conf": conf}
            orientations.append(area_dict)

        if orientations:
            closest_item = max(orientations, key=lambda x: x['conf'])
            return closest_item["result"]
        else:
            return None

    elif mode=="emotion_confmin":
        orientations = []
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            conf = result_indvi.emotion_conf
            area_dict = {"result": result_indvi, "conf": conf}
            orientations.append(area_dict)

        if orientations:
            closest_item = min(orientations, key=lambda x: x['conf'])
            return closest_item["result"]
        else:
            return None

    elif mode=="happy":
        label_new = mood_map.get("happy", [None,None,None,None])[2]
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            emotion = result_indvi.emotion
            if emotion == label_new:
                return result_indvi
        
        return None

    elif mode=="neutral":
        label_new = mood_map.get("neutral", [None,None,None,None])[2]
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            emotion = result_indvi.emotion
            if emotion == label_new:
                return result_indvi
        
        return None

    elif mode=="surprise":
        label_new = mood_map.get("surprise", [None,None,None,None])[2]
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            emotion = result_indvi.emotion
            if emotion == label_new:
                return result_indvi
        
        return None

    elif mode=="sad":
        label_new = mood_map.get("sad", [None,None,None,None])[2]
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            emotion = result_indvi.emotion
            if emotion == label_new:
                return result_indvi
        
        return None

    elif mode=="angry":
        label_new = mood_map.get("angry", [None,None,None,None])[2]
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            emotion = result_indvi.emotion
            if emotion == label_new:
                return result_indvi
        
        return None

    elif mode=="disgust":
        label_new = mood_map.get("disgust", [None,None,None,None])[2]
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            emotion = result_indvi.emotion
            if emotion == label_new:
                return result_indvi
        
        return None

    elif mode=="fear":
        label_new = mood_map.get("fear", [None,None,None,None])[2]
        result = get_frame_content_lastest()
        if not result:
            return None
        frame_result = result.results
        for result_indvi in frame_result:
            emotion = result_indvi.emotion
            if emotion == label_new:
                return result_indvi
        
        return None

    elif mode=="name":
        result = get_frame_content_lastest()
        
        if not result:
            return None
        
        frame_result = result.results
        
        # 找到所有匹配名称的人脸
        matching_faces = []
        for face_result in frame_result:
            if face_result.label_name == kwargs.get("name"):
                matching_faces.append(face_result)
        
        # 如果没有找到匹配的人脸，返回None
        if not matching_faces:
            return None
        
        # 如果只有一个匹配的人脸，直接返回
        if len(matching_faces) == 1:
            return matching_faces[0]
        
        # 如果有多个匹配的人脸，选择最靠近屏幕中间的人脸
        center_x = RESOLUTION[0] / 2
        center_y = RESOLUTION[1] / 2
        
        closest_face = None
        min_distance = float('inf')
        
        for face in matching_faces:
            face_center_x = face.x + face.w // 2
            face_center_y = face.y + face.h // 2
            distance = math.sqrt((face_center_x - center_x) ** 2 + (face_center_y - center_y) ** 2)
            
            if distance < min_distance:
                min_distance = distance
                closest_face = face
        
        return closest_face

def face_detection(img_no_disp, img_disp, save_length:int = 10):
    lResult = []
    results = []
    global FACE_LEARN_MODE
    global IS_HIDE_FACE_KEYPOINTS
    global out_side_labels
    
    count = 0
    dets = detector.detect(img_no_disp, 0.3, 0.45, keypoint_th = 0.5)
    if RECOGNIZER_AND_LANDMARKS_DISABLED:
        faces = dets
    else:
        faces = recognizer.recognize(img_no_disp, 0.5, 0.45, 0.85, True, True)

    for idx, obj in enumerate(faces):
        img_std = None
        if not RECOGNIZER_AND_LANDMARKS_DISABLED:
            img_std = landmarks_detector.crop_image(img_no_disp, obj.x, obj.y, obj.w, obj.h, obj.points)
        # img_std_gray = img_std.to_format(image.Format.FMT_GRAYSCALE)
        # res = classifier.classify(img_std_gray, softmax=True)
        # emotion_label_best = classifier.labels[res[0][0]]
        # score = res[0][1]
        emotion_label_best = "neutral"
        score = 0.0
        if RECOGNIZER_AND_LANDMARKS_DISABLED:
            face_score = obj.score
        else:
            face_score = dets[idx].score if len(faces) <= len(dets) else score
        radius = math.ceil(obj.w / 10)
        label_name = "Face"
        is_learned = False

        if not RECOGNIZER_AND_LANDMARKS_DISABLED:
            for item in out_side_labels:
                if item["id"] == obj.class_id:
                    label_name = item["label_name"]
                    break

        obj = scale_obj(obj)
        label_name_scale = 1
        label_string = f'{label_name} {face_score:.2f}'
        label_name_w, label_name_h = image.string_size(label_string, scale = label_name_scale)

        if label_name == "Face":
            is_learned = False
        else:
            is_learned = True

        label_name_bg_h = 30
        label_name_y = obj.y - label_name_bg_h // 2 - label_name_h // 2

        if is_learned:
            img_disp.draw_rect(obj.x, obj.y, obj.w, obj.h, image.Color.from_rgb(9, 232, 50), thickness = 2)
            img_disp.draw_rect(obj.x, obj.y - label_name_bg_h, label_name_w + 20, 30, image.Color.from_rgb(9, 232, 50), thickness = -1) # 标题背景
            img_disp.draw_string(obj.x + 10, obj.y - 24, label_string, image.COLOR_WHITE, scale = label_name_scale)

        else:
            img_disp.draw_rect(obj.x, obj.y, obj.w, obj.h, image.COLOR_WHITE, thickness = 2)
            img_disp.draw_rect(obj.x, obj.y - label_name_bg_h, label_name_w + 20, 30, image.COLOR_WHITE, thickness = -1) # 标题背景
            img_disp.draw_string(obj.x + 10, label_name_y, label_string, image.Color.from_rgb(38, 38, 38), scale = label_name_scale)

        f_detection = Face(
            x=obj.x,
            y=obj.y,
            w=obj.w,
            h=obj.h,
            conf=int(face_score * 100),
            obj=obj,
            label_id = 0 if RECOGNIZER_AND_LANDMARKS_DISABLED else obj.class_id,
            label_name = label_name,
            is_learned=is_learned,
            emotion=mood_map.get(emotion_label_best, [None,None,None,None])[2],
            emotion_conf=int(score * 100),
        )
        lResult.append(f_detection)

        if FUNC_STATUS.get("keypoint") and not RECOGNIZER_AND_LANDMARKS_DISABLED:
            if img_std:
                res = landmarks_detector.detect(img_std, landmarks_conf_th, landmarks_abs, landmarks_rel)
                res.points = scale_points(res.points, scale_x, scale_y)
                if res and res.valid:
                    results.append(res)
            count += 1
            if count >= max_face_num:
                break
        
    if FUNC_STATUS.get("keypoint") and not RECOGNIZER_AND_LANDMARKS_DISABLED:
        for res in results:
            sub_xy, sub_z = get_sub_landmarks(res.points, res.points_z, sub_68_idxes)
            landmarks_detector.draw_face(img_disp, sub_xy, len(sub_z), sub_z)

    frame_content = FrameResult(
        time_stamp=int(time.time()),
        results=lResult,
    )
    gFrameFaceResult.append(frame_content)
    if len(gFrameFaceResult) > save_length:
        del gFrameFaceResult[0]

    return img_disp

# 首字符是否是中文
def is_first_char_chinese(text):
    if not text:
        return False
    first_char = text[0]
    # 基本中文 + 扩展A/B区等
    ranges = [
        ('\u4e00', '\u9fff'),
        ('\u3400', '\u4dbf'),
        ('\u20000', '\u2a6df'),
        ('\u2a700', '\u2b73f'),
        ('\u2b740', '\u2b81f'),
    ]
    for start, end in ranges:
        if start <= first_char <= end:
            return True
    return False

def face_recognize(img_no_disp, img_disp, save_length:int = 10):
    global FACE_LEARN_MODE
    global FUNC_STATUS
    global CONFIRM_BTN_CLICKED
    global LEARN_TEST_SIGNAL
    
    if current_tab == 1 and not show_learn_list_content:
        img_disp.draw_image(list_img_xywh[0], list_img_xywh[1], list_img)
    
    if show_learn_list_content:
        return

    img_disp.draw_line(cross_coord[0], cross_coord[1], cross_coord[2], cross_coord[3], color = image.COLOR_WHITE, thickness = 2)
    img_disp.draw_line(cross_coord[4], cross_coord[5], cross_coord[6], cross_coord[7], color = image.COLOR_WHITE, thickness = 2)

    # 使用线程锁
    with recognizer_lock:
        lResult = []

        dets = detector.detect(img_no_disp, 0.3, 0.45, keypoint_th = 0.5)
        if RECOGNIZER_AND_LANDMARKS_DISABLED:
            faces = dets
        else:
            faces = recognizer.recognize(img_no_disp, 0.5, 0.45, 0.85, True, True)

        for index, obj in enumerate(faces):
            if not RECOGNIZER_AND_LANDMARKS_DISABLED:
                img_std = landmarks_detector.crop_image(img_no_disp, obj.x, obj.y, obj.w, obj.h, obj.points)
            # img_std_gray = img_std.to_format(image.Format.FMT_GRAYSCALE)
            # res = classifier.classify(img_std_gray, softmax=True)
            # emotion_label_best = classifier.labels[res[0][0]]
            # score = res[0][1]
            emotion_label_best = "neutral"
            score = 0.0
            if RECOGNIZER_AND_LANDMARKS_DISABLED:
                face_score = obj.score
            else:
                face_score = dets[index].score if len(faces) <= len(dets) else score
            
            radius = math.ceil(obj.w / 10)
            label_name = "Unknown"
            is_learned = False

            if not RECOGNIZER_AND_LANDMARKS_DISABLED:
                for item in out_side_labels:
                    if item["id"] == obj.class_id:
                        label_name = item["label_name"]
                        break
            # print('recognizer.labels', recognizer.labels)
            # print('out_side_labels', index, label_name, out_side_labels, obj)

            obj = scale_obj(obj)

            if label_name == "Unknown":
                is_learned = False
            else:
                is_learned = True

            label_name_scale = 1
            label_name_w, label_name_h = image.string_size(label_name, scale = label_name_scale)
            label_name_bg_h = 30
            label_name_y = obj.y - label_name_bg_h // 2 - label_name_h // 2

            if FACE_LEARN_MODE:
                if cross_learned_exsit():
                    FUNC_STATUS["learn"] = True
                else:
                    FUNC_STATUS["learn"] = False
                

            if is_learned:
                label = f"{label_name} {obj.score:.2f}"
                label_name_w, label_name_h = image.string_size(label, scale = label_name_scale)
                img_disp.draw_rect(obj.x, obj.y - label_name_bg_h, label_name_w + 20, 30, image.Color.from_rgb(9, 232, 50), thickness = -1) # 标题背景
                img_disp.draw_rect(obj.x, obj.y, obj.w, obj.h, image.Color.from_rgb(9, 232, 50), thickness = 2)
                # img_disp.draw_keypoints(obj.points, image.Color.from_rgb(9, 232, 50), size = radius if radius < 5 else 4)
                img_disp.draw_string(obj.x + 10, obj.y - 24, label, color=image.COLOR_WHITE, scale = label_name_scale)
                
            else:
                img_disp.draw_rect(obj.x, obj.y - label_name_bg_h, label_name_w + 20, 30, image.COLOR_WHITE, thickness = -1) # 标题背景
                img_disp.draw_rect(obj.x, obj.y, obj.w, obj.h, image.COLOR_WHITE, thickness = 2)
                # img_disp.draw_keypoints(obj.points, image.COLOR_WHITE, size = radius if radius < 5 else 4)
                img_disp.draw_string(obj.x + 10, obj.y - 24, label_name, image.Color.from_rgb(38, 38, 38), scale = label_name_scale)

            f_recognization = Face(
                x=obj.x,
                y=obj.y,
                w=obj.w,
                h=obj.h,
                conf=int(face_score * 100),
                label_id = 0 if RECOGNIZER_AND_LANDMARKS_DISABLED else obj.class_id,
                label_name = label_name,
                is_learned=is_learned,
                obj = obj,
                emotion=mood_map.get(emotion_label_best, [None,None,None,None])[2],
                emotion_conf=int(score * 100),
            )
            lResult.append(f_recognization)

        frame_content = FrameResult(
            time_stamp=int(time.time()),
            results=lResult
        )

        gFrameFaceResult.append(frame_content)

        if len(gFrameFaceResult) > save_length:
            del gFrameFaceResult[0]

        return img_disp

def emotion_recognize(img_no_disp, img_disp, save_length:int = 10):
    global out_side_labels
    lResult = []
    dets = detector.detect(img_no_disp, 0.3, 0.45, keypoint_th = 0.5)
    if RECOGNIZER_AND_LANDMARKS_DISABLED:
        faces = dets
    else:
        faces = recognizer.recognize(img_no_disp, 0.5, 0.45, 0.85, True, True)
    
    for idx, obj in enumerate(faces):
        if not RECOGNIZER_AND_LANDMARKS_DISABLED:
            img_std = landmarks_detector.crop_image(img_no_disp, obj.x, obj.y, obj.w, obj.h, obj.points)
        # img_std = landmarks_detector.crop_image(img_no_disp, obj.x, obj.y, obj.w, obj.h, obj.points,
        #                                     classifier.input_width(), classifier.input_height(), 0.9)
        # img_std_gray = img_std.to_format(image.Format.FMT_GRAYSCALE)
        obj = scale_obj(obj)
        img_disp.draw_rect(obj.x, obj.y, obj.w, obj.h, color = image.COLOR_WHITE, thickness = 2)
        # res = classifier.classify(img_std_gray, softmax=True)
        # label_best = classifier.labels[res[0][0]]
        label_best = "neutral"
        label_best_en = mood_map.get(label_best, ["", "", "", ""])[2]
        label_best_chn = mood_map.get(label_best, ["", "", "", ""])[1]
        # score = res[0][1]
        score = 0.0
        if RECOGNIZER_AND_LANDMARKS_DISABLED:
            face_score = obj.score
        else:
            face_score = dets[idx].score if len(faces) <= len(dets) else score

        label_name = "Face"
        is_learned = False


        for item in out_side_labels:
            if item["id"] == getattr(obj, "class_id", 0):
                label_name = item["label_name"]
                break

        if label_name == "Face":
            is_learned = False
        else:
            is_learned = True


        label_name_scale = 1
        label_name_w, label_name_h = image.string_size(f"{label_best_en}: {score:.2f} ", scale = label_name_scale)

        label_name_bg_h = 30
        label_name_x_margin = 10
        label_name_y = obj.y - label_name_bg_h // 2 - label_name_h // 2
        img_disp.draw_rect(obj.x, obj.y - label_name_bg_h, label_name_w + 24 + label_name_x_margin * 2, label_name_bg_h, image.COLOR_WHITE, thickness = -1) # 标题背景
        
        if label_best:
            emotion_img = mood_map.get(label_best, [None, None, None, None])[-1]
            x = max(0, obj.x)
            y = max(0, obj.y - label_name_bg_h // 2 - label_name_h // 2)

            x_with_img = min(x + 25, RESOLUTION[0])
            if emotion_img:
                try:
                    
                    img_disp.draw_image(clamp(x + label_name_x_margin, 0, 640), clamp(obj.y - label_name_bg_h // 2 - emotion_img.width() // 2, 0, 480), emotion_img)
                except Exception as e:
                    traceback.print_exc()
                    print(label_best_en)
                
                img_disp.draw_string(clamp(x_with_img + label_name_x_margin, 0, 640), clamp(y, 0, 480), f"{label_best_en}: {score:.2f}", color=image.Color.from_rgb(38, 38, 38), scale = label_name_scale)
            else:
                img_disp.draw_string(clamp(x + label_name_x_margin, 0, 640), clamp(y, 0, 480), f"{label_best_en}: {score:.2f}", color=image.Color.from_rgb(38, 38, 38), scale = label_name_scale)
        

        
        emotion = Face(
            x=obj.x,
            y=obj.y,
            w=obj.w,
            h=obj.h,
            conf=int(face_score * 100),
            label_id = 0 if RECOGNIZER_AND_LANDMARKS_DISABLED else obj.class_id,
            label_name = label_name,
            is_learned=is_learned,
            obj = obj,
            emotion=mood_map.get(label_best, [None, None, None, None])[2],
            emotion_conf=int(score * 100),
        )
        lResult.append(emotion)
    frame_content = FrameResult(
        time_stamp=int(time.time()),
        results=lResult
    )
    gFrameFaceResult.append(frame_content)
    if len(gFrameFaceResult) > save_length:
        del gFrameFaceResult[0]

    return img_disp

last_tick = time.time()
icon_delete_img_x = 520
learn_list_header_name_str_size = image.string_size('Name', scale=1.6)

def main(disp):
    global display_show_lock, show_loading, current_tab, FACE_LEARN_MODE, last_tick, FUNC_STATUS, switch_status, img_no_disp, show_delete_popup, show_learn_list_content, current_delete_label_name
    global last_pressed, last_x, last_y, pressed_already, pagination_list, current_page, total_page, learn_list_header_name_str_size

    while not app.need_exit():
        if int(time.time()) - last_tick > 5:
            send_socket_message('PASS')
            last_tick = time.time()

        img_disp = None

        if show_learn_list_content:
            img_disp = image.Image(disp.width(), disp.height(), image.Format.FMT_RGBA8888)
        else:
            img_disp_tmp = cam.read()
            img_disp = img_disp_tmp.to_format(image.Format.FMT_RGBA8888)

        img_no_disp = cam_min.read()
        x, y, pressed = ts.read()
            
        # 防误触设计，模拟用户按压屏幕松开后才触发
        if x != last_x or y != last_y or pressed != last_pressed:
            last_x = x
            last_y = y
            last_pressed = pressed
        if pressed:
            pressed_already = True
        elif pressed_already:
            pressed_already = False
            # 如果当前显示的是删除弹窗
            if show_delete_popup:
                temp_face_current = get_face_info("center")

                if temp_face_current:
                    print(f"[DELETE FACE CONFIRM]saved remove face label: {temp_face_current.label_name}")
                else:
                    print("[DELETE FACE CONFIRM] current remove face label is None")

                # 如果点击删除弹窗的取消按钮或关闭按钮
                if delete_popup.is_touch_cancel(x, y) or delete_popup.is_touch_close(x, y):
                    show_delete_popup = False

                # 如果点击删除弹窗的确认按钮
                elif delete_popup.is_touch_confirm(x, y):
                    show_delete_popup = False

                    if show_learn_list_content:
                        study_face.remove_learned_faces(current_delete_label_name)

                        # 如果删除的这条是最后一条数据则自动翻到上一页
                        if pagination_list and len(pagination_list[current_page - 1]) == 1 and current_page > 1:
                            current_page -= 1
                    elif temp_face_current:
                        lResult = study_face.remove_learned_faces(temp_face_current.label_name)
                        if lResult:
                            print("[DELETE FACE CONFIRM] remove face label success!")
                        else:
                            print("[DELETE FACE CONFIRM] remove face label failed!")
            else:
                if current_tab == 0:
                    # 如果点击的是后退按钮
                    if is_in_button(x, y, exit_btn_pos):
                        app.set_exit_flag(True)
                        break
                    # 如果点击的是识别已学习的人脸按钮
                    elif is_in_button(x, y, face_learn_xywh):
                        current_tab = 1
                    # 如果点击的是情绪识别
                    elif is_in_button(x, y, emotion_recognition_btn_section_img_xywh):
                        current_tab = 2
                    # 如果点击的是隐藏或显示人脸关键点按钮
                    elif is_in_button(x, y, func_btn_pos):
                        FUNC_STATUS["keypoint"] = not FUNC_STATUS["keypoint"]
                
                elif current_tab == 1:
                    # 如果是显示的是人脸列表
                    if show_learn_list_content:
                        current_y = 90
                        current_y += 35 + learn_list_header_name_str_size.height()
                        _show_delete_popup = False

                        # 检查pagination_list是否为空或current_page是否超出范围
                        if pagination_list and current_page > 0 and current_page <= len(pagination_list):
                            for index, item in enumerate(pagination_list[current_page - 1]):
                                # 超过屏幕大小需要跳过不然会闪退报错
                                if current_y > disp.height():
                                    continue

                                text = item['label_name']
                                icon_delete_img_y = current_y - 6
                                text = truncate_string_by_width(text, 1.6, 300)
                                text_h = image.string_size(text, scale=1.6).height()

                                if is_in_button(x, y, [icon_delete_img_x, icon_delete_img_y, icon_delete_img.width(), icon_delete_img.height()]):
                                    current_delete_label_name = item['label_name']
                                    _show_delete_popup = True

                                current_y += 35 + text_h
                        
                        # 如果点击的是删除按钮
                        if _show_delete_popup:
                            show_delete_popup = True
                        
                        # 如果点击的是后退按钮
                        elif is_in_button(x, y, exit_btn_pos):
                            show_learn_list_content = False

                        # 如果点击的是上一页
                        elif is_in_button(x, y, prev_page_btn_xywh) and current_page > 1:
                            current_page -= 1

                        # 如果点击的是下一页
                        elif is_in_button(x, y, next_page_btn_xywh) and current_page < total_page:
                            current_page += 1
                    else:
                        # 如果点击的是识别人脸按钮
                        if is_in_button(x, y, face_btn_pos):
                            current_tab = 0
                        # 如果点击的是后退按钮
                        elif is_in_button(x, y, exit_btn_pos):
                            app.set_exit_flag(True)
                            break
                        # 如果点击的是情绪识别
                        elif is_in_button(x, y, emotion_recognition_btn_section2_img_xywh):
                            current_tab = 2
                        # 如果点击的右上角的删除学习人脸或学习人脸按钮
                        elif is_in_button(x, y, func_btn_pos):
                            # 如果点击的是删除学习按钮
                            if FUNC_STATUS["learn"]:
                                show_delete_popup = True
                            else:
                                learn_face()
                        # 如果点击的右下角的列表按钮
                        elif is_in_button(x, y, list_img_xywh):
                            show_learn_list_content = True
                
                elif current_tab == 2:
                    # 如果点击的是识别已学习的人脸按钮
                    if is_in_button(x, y, face_learning_btn_section_img_xywh):
                        current_tab = 1
                    # 如果点击的是后退按钮
                    elif is_in_button(x, y, exit_btn_pos):
                        app.set_exit_flag(True)
                        break
        
        if current_tab == 0:
            FACE_LEARN_MODE = False

            if not show_delete_popup:
                face_detection(img_no_disp, img_disp)

        elif current_tab == 1:
            FACE_LEARN_MODE = True
            
            if not show_delete_popup:
                face_recognize(img_no_disp, img_disp)

        elif current_tab == 2:
            FACE_LEARN_MODE = False

            if not show_delete_popup:
                emotion_recognize(img_no_disp, img_disp)
        else:
            FACE_LEARN_MODE = False

        img_disp.draw_image(exit_btn_pos[0], exit_btn_pos[1], back_btn_img)

        if show_learn_list_content:
            img_disp.draw_image(learned_faces_img_xywh[0], learned_faces_img_xywh[1], learned_faces_img)

            current_y = 90

            img_disp.draw_string(24, current_y + 4, 'ID', scale=1.6)
            img_disp.draw_string(96, current_y, 'Name', scale=1.6)

            if len(out_side_labels) > 0:
                current_y += 35 + learn_list_header_name_str_size.height()
                pagination_list = [out_side_labels[i:i+pagination_num] for i in range(0, len(out_side_labels), pagination_num)] # 按每页4条数据来平分数组
                total_page = len(pagination_list)
                
                # 如果current_page超出范围，重置为1
                if current_page > total_page:
                    current_page = 1

                # 确保current_page在有效范围内
                if current_page > 0 and current_page <= len(pagination_list):
                    for index, item in enumerate(pagination_list[current_page - 1]):
                        # 超过屏幕大小需要跳过不然会闪退报错
                        if current_y > disp.height():
                            continue

                        text = item['label_name']
                        icon_delete_img_y = current_y - 6
                        text = truncate_string_by_width(text, 1.6, 300)
                        
                        img_disp.draw_string(24, current_y + 4, str((current_page - 1) * 4 + (index + 1)), scale=1.6)
                        text_h = image.string_size(text, scale=1.6).height()
                        img_disp.draw_string(96, current_y, text, scale=1.6)
                        img_disp.draw_image(icon_delete_img_x, icon_delete_img_y, icon_delete_img)

                        current_y += 35 + text_h
                
                if current_page > 1:
                    img_disp.draw_image(prev_page_btn_xywh[0], prev_page_btn_xywh[1], prev_page_btn_img)
                
                pagination_num_text = f"{current_page}/{total_page}"
                pagination_num_text_w, pagination_num_text_h = image.string_size(pagination_num_text, scale=1.575)
                img_disp.draw_string(disp.width() // 2 - pagination_num_text_w // 2, disp.height() - pagination_num_text_h - 20, pagination_num_text, scale=1.575)

                if current_page < total_page:
                    img_disp.draw_image(next_page_btn_xywh[0], next_page_btn_xywh[1], next_page_btn_img)
        else:
            if current_tab == 0:
                img_disp.draw_image(face_btn_pos[0], face_btn_pos[1], face_detection_active_img)
                img_disp.draw_image(face_learn_xywh[0], face_learn_xywh[1], face_learning_unactive_img)
                img_disp.draw_image(emotion_recognition_btn_section_img_xywh[0], emotion_recognition_btn_section_img_xywh[1], emotion_recognition_btn_section_img)
            
            if current_tab == 1:
                img_disp.draw_image(face_btn_pos[0], face_btn_pos[1], face_detection_btn_section_img)
                img_disp.draw_image(face_learning_active_img_xywh[0], face_learning_active_img_xywh[1], face_learning_active_img)
                img_disp.draw_image(emotion_recognition_btn_section2_img_xywh[0], emotion_recognition_btn_section2_img_xywh[1], emotion_recognition_btn_section2_img)
            
            if current_tab == 2:
                img_disp.draw_image(face_learning_btn_section_img_xywh[0], face_learning_btn_section_img_xywh[1], face_learning_btn_section_img)
                img_disp.draw_image(emotion_pos[0], emotion_pos[1], face_emotion_active_img)
        
            if current_tab == 0:
                # 绘制显示关键点按钮
                if FUNC_STATUS.get("keypoint"):
                    img_disp.draw_image(func_btn_pos[0], func_btn_pos[1], show_key_point_btn_img)
                elif not FUNC_STATUS.get("keypoint"):
                    img_disp.draw_image(func_btn_pos[0], func_btn_pos[1], hide_key_point_btn_img)

            if current_tab == 1:
                # 绘制显示学习人脸按钮
                if FUNC_STATUS.get("learn"):
                    img_disp.draw_image(func_btn_pos[0], func_btn_pos[1], del_learn_btn_img)
                elif not FUNC_STATUS.get("learn"):
                    img_disp.draw_image(func_btn_pos[0], func_btn_pos[1], learn_btn_img)

        if show_delete_popup:
            delete_popup.draw(img_disp)

        with display_show_lock:
            show_loading = False
            disp.show(img_disp) # 显示到屏幕
        
        time.sleep_ms(5) # 休眠一些时间来释放一些CPU使用

try:
    main(disp)
except Exception:
    import traceback
    msg = traceback.format_exc()
    print(msg)
    img = image.Image(disp.width(), disp.height(), image.Format.FMT_RGBA8888)
    img.draw_string(0, 0, msg, image.COLOR_WHITE)
    disp.show(img)
    while not app.need_exit():
        time.sleep_ms(100)