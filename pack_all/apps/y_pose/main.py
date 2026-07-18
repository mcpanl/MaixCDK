from maix import display, image, time
import threading
from loading import Loading

font_size = 20
image.load_font("sourcehansans", "/maixapp/share/font/SourceHanSansCN-Regular.otf", size = font_size)
image.set_default_font("sourcehansans")

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

IS_HIDE_KEYPOINTS = False

def toggle_hide():
    global IS_HIDE_KEYPOINTS
    
    if IS_HIDE_KEYPOINTS:
        IS_HIDE_KEYPOINTS = False
    else:
        IS_HIDE_KEYPOINTS = True

longpress_lock = False

def on_user_key(key_id, state):
    '''
        this func called in a single thread
    '''
    
    global longpress_lock

    if key_id == 352:
        if state == 0 and longpress_lock == False:
            toggle_hide()
        elif state == 1:
            longpress_lock = False
        elif state ==2:
            longpress_lock = True

key_obj = key.Key(on_user_key)

from maix import camera, nn, app, touchscreen, pinmap, gpio
import math
import numpy as np
from collections import deque
import select
import socket
import os

cam_flip_init = False
pressed_already = False
last_x = 0
last_y = 0
last_pressed = False

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
    global rec_display, back
    
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
                    # key_down_count = 0
                    back = True
                    print(f'key_swith_mind:{back}')
                    app.set_exit_flag(True)
                    break
                key_down_count += 1
            elif key_swith_right.value() == 0:
                # print(f'key_swith_right:{key_down_count}')
                if key_down_count == 2:
                    print('d+1')
                key_down_count += 1
            elif key_swith_left.value() == 0:
                # print(f'key_swith_left:{key_down_count}')
                if key_down_count == 2:
                    print('d-1')
                key_down_count += 1
            else:
                key_down_count = 0
        time.sleep_ms(10)

key_thread = threading.Thread(target=key_scan)
key_thread.daemon = True
key_thread.start()

# detector = nn.YOLOv8(model="/root/models/yolov8n_pose.mud", dual_buff = True)
detector = nn.YOLO11(model="/root/models/yolo11n_pose.mud", dual_buff = True)

cam_high = camera.Camera(disp.width(), disp.height(), image.Format.FMT_RGBA8888)
cam = cam_high.add_channel(detector.input_width(), detector.input_height(), detector.input_format())

def set_camera_flip(state):
    global cam_flip_init
    cam_high.vflip(state)
    cam.vflip(state)
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

IS_REVERSE_WH = False

lFrameResult = []
if IS_REVERSE_WH:
    RESOLUTION = (disp.height(), disp.width())
else:
    RESOLUTION = (disp.width(), disp.height())


ts = touchscreen.TouchScreen()

scale_x = disp.width() / cam.width()
scale_y = disp.height() / cam.height()

print(scale_x, scale_y)

margin = 12

text_offset_x = -3

exit_image = image.load('./assets/images/back_btn.png', image.Format.FMT_RGBA8888)


exit_pos_x = margin
exit_pos_y = margin
exit_pos_w = 72
exit_pos_h = 72
exit_btn_pos = [exit_pos_x, exit_pos_y, exit_pos_w, exit_pos_h]
exit_btn_pos2 = image.resize_map_pos(detector.input_width(), detector.input_height(), disp.width(), disp.height(), image.Fit.FIT_CONTAIN, exit_btn_pos[0], exit_btn_pos[1], exit_btn_pos[2], exit_btn_pos[3])

posture_recognition_image = image.load('./assets/images/PostureRecognition.png', image.Format.FMT_RGBA8888)

ocr_label = "Posture Recognition"
# ocr_pos_w = 300
# ocr_pos_h = 64
# ocr_pos_x = int(disp.width() /2) - int(ocr_pos_w / 2)
# ocr_pos_y = margin + int(ocr_pos_h /2) - int(ocr_pos_h / 2)
ocr_btn_pos = [disp.width() // 2 - posture_recognition_image.width() // 2, 24, posture_recognition_image.width(), posture_recognition_image.height()]
ocr_btn_pos2 = image.resize_map_pos(detector.input_width(), detector.input_height(), disp.width(), disp.height(), image.Fit.FIT_CONTAIN, ocr_btn_pos[0], ocr_btn_pos[1], ocr_btn_pos[2], ocr_btn_pos[3])


# ocr_text_size = image.string_size(ocr_label, 1.5)
# ocr_text_pos_x = ocr_pos_x + int(ocr_pos_w / 2) - int(ocr_text_size.width() / 2) + text_offset_x
# ocr_text_pos_y = ocr_pos_y + int(ocr_pos_h / 2) - int(ocr_text_size.height() / 2)


# temp
learn_btn_pos = [500,500,1,1]

pressed_flag = [False, False, False]
def on_clicked_pose():
    pass

def on_clicked_learn():
    pass

def is_in_button(x, y, btn_pos):
    return x > btn_pos[0] and x < btn_pos[0] + btn_pos[2] and y > btn_pos[1] and y < btn_pos[1] + btn_pos[3]

class PoseEstimation:
    # 翻译字典，避免重复定义
    TRANSLATION_DICT = {
        "zh": {
            "上身": "Body", "竖直": " Upright", "倾斜": " Tilted", "平躺": " Lie Down",
            "左": "Left ", "右": "Right ", "大腿": "Thigh", "小臂": "Forearm", "小腿": "Shin",
            "弯曲": " Curved", "伸直": " Straight", "大臂": "Upper Arm", "下垂" : " Drooping",
            "抬起" : " Raised", "平举" : " Horizontal", "举高" : " High", "上竖" : " Upright",
            "躺下": "Lie down", "直立": "Stand upright", "坐下": "Sit down", "斜躺": "Lie sideways",
            "向左1": "Turn left", "向右1": "Turn right", "双手平举": "T pose", "举左手": "Raise left arm",
            "举右手": "Raise right arm", "举双手": "Raise both arms", "双手比心": "Hand heart", "大字型": "Starfish pose",
        },
        "en": {
            "上身": "Body", "竖直": " Upright", "倾斜": " Tilted", "平躺": " Lie Down",
            "左": "Left ", "右": "Right ", "大腿": "Thigh", "小臂": "Forearm", "小腿": "Shin",
            "弯曲": " Curved", "伸直": " Straight", "大臂": "Upper Arm", "下垂" : " Drooping",
            "抬起" : " Raised", "平举" : " Horizontal", "举高" : " High", "上竖" : " Upright",
            "躺下": "Lie down", "直立": "Stand upright", "坐下": "Sit down", "斜躺": "Lie sideways",
            "向左1": "Turn left", "向右1": "Turn right", "双手平举": "T pose", "举左手": "Raise left arm",
            "举右手": "Raise right arm", "举双手": "Raise both arms", "双手比心": "Hand heart", "大字型": "Starfish pose",
        }
    }

    def __init__(self, keypoints_window_size=5):
        self.keypoints_map_deque = deque(maxlen=keypoints_window_size)
        self.status = []
        self.known_poses = {}
        # 初始化翻译器
        from maix import i18n
        self.trans = i18n.Trans(self.TRANSLATION_DICT)
        self.tr = self.trans.tr
        self.trans.set_locale(i18n.get_locale())

    def _keypoints_to_map(self, keypoints):
        """
        将关键点数组转换为关键点映射字典
        """
        keypoints_array = np.array(keypoints).reshape((-1, 2))
        assert(keypoints_array.shape == (17, 2))
        return {
            'Nose': keypoints_array[0],
            'Left Eye': keypoints_array[1],
            'Right Eye': keypoints_array[2],
            'Left Ear': keypoints_array[3],
            'Right Ear': keypoints_array[4],
            'Left Shoulder': keypoints_array[5],
            'Right Shoulder': keypoints_array[6],
            'Left Elbow': keypoints_array[7],
            'Right Elbow': keypoints_array[8],
            'Left Wrist': keypoints_array[9],
            'Right Wrist': keypoints_array[10],
            'Left Hip': keypoints_array[11],
            'Right Hip': keypoints_array[12],
            'Left Knee': keypoints_array[13],
            'Right Knee': keypoints_array[14],
            'Left Ankle': keypoints_array[15],
            'Right Ankle': keypoints_array[16],
        }

    def feed_keypoints_17(self, keypoints_17):
        """
        # keypoints 是 17 个关键点的坐标 [(x1, y1), (x2, y2), ..., (x17, y17)]
        基于 yolo-pose 17关键点模型估测骨架坐标:
        0 - 鼻子 (Nose)
        1 - 左眼 (Left Eye)
        2 - 右眼 (Right Eye)
        3 - 左耳 (Left Ear)
        4 - 右耳 (Right Ear)
        5 - 左肩 (Left Shoulder)
        6 - 右肩 (Right Shoulder)
        7 - 左肘 (Left Elbow)
        8 - 右肘 (Right Elbow)
        9 - 左手腕 (Left Wrist)
        10 - 右手腕 (Right Wrist)
        11 - 左髋部 (Left Hip)
        12 - 右髋部 (Right Hip)
        13 - 左膝 (Left Knee)
        14 - 右膝 (Right Knee)
        15 - 左脚踝 (Left Ankle)
        16 - 右脚踝 (Right Ankle)

        3-1-0-2-4 构成 头部 (Head)
        5-6-12-11-5 构成 躯干 (Torso)
        5-7-9 或 6-8-10 构成 上肢 (Upper Limbs)
        11-13-15 或 12-14-16 构成 下肢 (Lower Limbs)
        """
        kp_map = self._keypoints_to_map(keypoints_17)
        self.feed_keypoints_map(kp_map)

    def feed_keypoints_map(self, keypoints_map):
        # 保持向后兼容，但现在主要使用 _analyze_pose 方法
        self.keypoints_map_deque.append(keypoints_map)
        self.status = self._analyze_pose(keypoints_map)

    def _analyze_pose(self, km):
        """
        分析姿态的核心逻辑，直接使用传入的关键点映射
        """
        def angle_vec(v1: np.ndarray, v2: np.ndarray):
            return np.degrees(np.arctan2(np.cross(v1, v2), np.dot(v1, v2)))

        # 使用类级别的翻译器
        tr = self.tr
        status = []

        UP = np.array((0, -1))

        hs_l = np.array(km['Left Shoulder']-km['Left Hip'])
        hs_r = np.array(km['Right Shoulder']-km['Right Hip'])
        hs_c = (hs_l + hs_r) / 2

        uhs_l = angle_vec(UP, hs_l)
        uhs_r = angle_vec(UP, hs_r)
        uhs_c = angle_vec(UP, hs_c)
        status += [f"<uhs: l={uhs_l:.1f}, r={uhs_r:.1f}, c={uhs_c:.1f}>"]

        if abs(uhs_c) < 30:
            status += [tr("上身")+tr("竖直")] # Body Upright
        elif abs(uhs_c) < 80:
            status += [tr("上身")+tr("倾斜")] # Body Tilted
        else:
            status += [tr("上身")+tr("平躺")] # Body Lying Down

        hk_l = np.array(km['Left Knee']-km['Left Hip'])
        hk_r = np.array(km['Right Knee']-km['Right Hip'])
        hk_c = (hk_l + hk_r) / 2

        shk_l = angle_vec(hs_c, hk_l) # 左躯干与左大腿之间的角度
        shk_r = angle_vec(hs_c, hk_r) # 右躯干与右大腿之间的角度
        shk_c = angle_vec(hs_c, hk_c) # 左右两侧大腿向量的平均方向与躯干之间的角度
        status += [f"<shk: l={shk_l:.1f}, r={shk_r:.1f}, c={shk_c:.1f}>"]

        def det_curve(ang, status):
            ang = abs(ang)
            if ang < 160:
                status[-1] += tr("弯曲") # Curved
            else:
                status[-1] += tr("伸直") # Straight

        status += [tr("左")+tr("大腿")] # Left Thigh
        det_curve(shk_l, status)
        status += [tr("右")+tr("大腿")] # Right Thigh
        det_curve(shk_r, status)

        se_l = np.array(km['Left Elbow']-km['Left Shoulder'])
        se_r = np.array(km['Right Elbow']-km['Right Shoulder'])
        se_c = (se_l + se_r) / 2

        hse_l = angle_vec(-hs_l, se_l)
        hse_r = angle_vec(-hs_r, se_r)
        hse_c = angle_vec(-hs_c, se_c)
        status += [f"<hse: l={hse_l:.1f}, r={hse_r:.1f}, c={hse_c:.1f}>"]

        def det_hse(ang, status):
            ang = abs(ang)
            if ang < 20:
                status[-1] += tr("下垂") # Drooping
            elif ang < 80:
                status[-1] += tr("抬起") # Raised
            elif ang < 110:
                status[-1] += tr("平举") # Horizontal
            elif ang < 160:
                status[-1] += tr("举高") # High
            else:
                status[-1] += tr("上竖") # Upright

        status += [tr("左")+tr("大臂")] # Left Upper Arm
        det_hse(hse_l, status)
        status += [tr("右")+tr("大臂")] # Right Upper Arm
        det_hse(hse_r, status)

        ew_l = np.array(km['Left Wrist']-km['Left Elbow'])
        ew_r = np.array(km['Right Wrist']-km['Right Elbow'])
        ew_c = (ew_l + ew_r) / 2

        sew_l = angle_vec(-se_l, ew_l)
        sew_r = angle_vec(-se_r, ew_r)
        sew_c = angle_vec(-se_c, ew_c)
        status += [f"<sew: l={sew_l:.1f}, r={sew_r:.1f}, c={sew_c:.1f}>"]

        status += [tr("左")+tr("小臂")] # Left Forearm
        det_curve(sew_l, status)
        status += [tr("右")+tr("小臂")] # Right Forearm
        det_curve(sew_r, status)


        ka_l = np.array(km['Left Ankle']-km['Left Knee'])
        ka_r = np.array(km['Right Ankle']-km['Right Knee'])

        hka_l = angle_vec(-hk_l, ka_l)
        hka_r = angle_vec(-hk_r, ka_r)
        status += [f"<hka: l={hka_l:.1f}, r={hka_r:.1f}>"]

        status += [tr("左")+tr("小腿")] # Left Shin
        det_curve(hka_l, status)
        status += [tr("右")+tr("小腿")] # Right Shin
        det_curve(hka_r, status)


        sw_l = np.array(km['Left Wrist']-km['Left Shoulder'])
        sw_r = np.array(km['Right Wrist']-km['Right Shoulder'])
        sw_c = (sw_l + sw_r) / 2

        hsw_l = angle_vec(-hs_c, sw_l)
        hsw_r = angle_vec(-hs_c, sw_r)
        status += [f"<hsw: l={hsw_l:.1f}, r={hsw_r:.1f}"]

        status += ["total:"]
        if tr("上身")+tr("平躺") in status:
            status += [tr("躺下")]
        elif tr("上身")+tr("竖直") in status:
            if tr("左")+tr("大腿")+tr("伸直") in status or tr("右")+tr("大腿")+tr("伸直") in status:
                status += [tr("直立")]
            else:
                status += [tr("坐下")]
        else:
            if tr("左")+tr("大腿")+tr("伸直") in status or tr("右")+tr("大腿")+tr("伸直") in status: # todo: 斜躺平躺都可以弯腿。实际是二维平面投影图，应该要考虑人脸朝向（正向，背向，向左，向右。区分左右）
                status += [tr("斜躺")]
            else:
                status += [tr("坐下")]

        if tr("左")+tr("大臂")+tr("平举") in status and tr("左")+tr("小臂")+tr("伸直") in status:
            status += [tr("向左1")]
        if tr("右")+tr("大臂")+tr("平举") in status and tr("右")+tr("小臂")+tr("伸直") in status:
            status += [tr("向右1")]
        if tr("向左1") in status and tr("向右1") in status:
            del status[-1]
            del status[-1]
            status += [tr("双手平举")]

        if tr("左")+tr("大臂")+tr("举高") in status and tr("左")+tr("小臂")+tr("伸直") in status:
            status += [tr("举左手")]
        if tr("右")+tr("大臂")+tr("举高") in status and tr("右")+tr("小臂")+tr("伸直") in status:
            status += [tr("举右手")]
        if tr("举左手") in status and tr("举右手") in status:
            del status[-1]
            del status[-1]
            status += [tr("举双手")]

        if tr("左")+tr("大臂")+tr("举高") in status:
            del status[-1]
            status += [tr("举左手")]
        
        if tr("右")+tr("大臂")+tr("举高") in status:
            del status[-1]
            status += [tr("举右手")]

        if tr("右")+tr("大臂")+tr("举高") in status and tr("左")+tr("大臂")+tr("举高") in status:
            del status[-1]
            status += [tr("举双手")]

        if (tr("左")+tr("大臂")+tr("上竖") in status or tr("左")+tr("大臂")+tr("举高") in status) and tr("左")+tr("小臂")+tr("弯曲") in status and (tr("右")+tr("大臂")+tr("上竖") in status or tr("右")+tr("大臂")+tr("举高") in status) and tr("右")+tr("小臂")+tr("弯曲") in status:
            if abs(hsw_l) > 160 and abs(hsw_r) > 160:
                status += [tr("双手比心")]

        if tr("双手平举") in status and tr("左")+tr("小腿")+tr("伸直") in status and tr("右")+tr("小腿")+tr("伸直") in status:
            if -shk_r < 175 and -shk_r > 100 and shk_l < 175 and shk_l > 100 and abs(shk_c) > 160:
                status += [tr("大字型")]
        
        return status[status.index("total:")+1:]


    def get_status(self):
        # 返回当前状态，如果没有状态则返回空列表
        if self.status:
            return self.status[-1]
        else:
            return []

    def learn_pose(self, keypoints, pose_name="Pose1"):
        # 直接使用当前关键点，不使用滑动窗口平均
        km = self._keypoints_to_map(keypoints)
        self.known_poses[pose_name] = km
        print(f"Learned pose '{pose_name}': {km}")

    def compare_pose(self, keypoints, pose_name="Pose1", threshold=15):
        # 直接使用当前关键点，不使用滑动窗口平均
        km_current = self._keypoints_to_map(keypoints)
        
        if pose_name not in self.known_poses:
            # print(f"Pose '{pose_name}' is not learned yet.")
            return None
        km_ref = self.known_poses[pose_name]

        # 使用上身（肩膀和髋部）的向量角度作为对比特征
        def compute_upper_body_angle(km):
            UP = np.array((0, -1))
            hs_l = np.array(km['Left Shoulder'] - km['Left Hip'])
            hs_r = np.array(km['Right Shoulder'] - km['Right Hip'])
            hs_c = (hs_l + hs_r) / 2
            angle = np.degrees(np.arctan2(np.cross(UP, hs_c), np.dot(UP, hs_c)))
            return angle

        angle_current = compute_upper_body_angle(km_current)
        angle_ref = compute_upper_body_angle(km_ref)
        print(f"Current upper body angle: {angle_current:.1f}, Reference: {angle_ref:.1f}")

        if abs(angle_current - angle_ref) < threshold:
            return pose_name
        else:
            return None


    def evaluate_pose(self, keypoints):
        # 直接使用当前关键点进行姿态评估，不使用滑动窗口
        kp_map = self._keypoints_to_map(keypoints)
        
        # 直接调用姿态分析逻辑，不使用滑动窗口
        pose_status = self._analyze_pose(kp_map)
        # 返回最后一个姿态类型，如果没有则返回空字符串
        if pose_status:
            return pose_status[-1]
        else:
            return ""

def to_keypoints_np(obj_points):
    keypoints = np.array(obj_points)
    keypoints = keypoints.reshape((-1, 2))
    # print("kps: ", keypoints)
    return keypoints

pose_estimator = PoseEstimation()

RESULT_BY_OTHER_MAP = {
    "1": "center",
    "2": "leftmost",
    "3": "rightmost",
    "4": "topmost",
    "5": "bottommost",
    "6": "highest_confidence",
    "7": "lowest_confidence",
}

POSE_SPACE_MAP = {
    "1": "Stand upright",
    "2": "Raise both arms",
    "3": "Hand heart",
    "4": "Starfish pose",
    "5": "Turn left",
    "6": "Turn right",
    "7": "Sit down",
    "8": "Lie down",
    "9": "Raise left arm",
    "10": "Raise right arm",
    "11": "Lie sideways",
    "12": "T pose",
}

ATTR_MAP = {
    "1": "x",
    "2": "y",
    "3": "w",
    "4": "h",
    "5": "conf",
}


def send_result(command, result, attr=None):
    """
    根据结果是否存在及属性是否有效发送对应消息
    """
    if result and (attr is None or hasattr(result, attr) and getattr(result, attr) is not None):
        
        value = f"{command}\nfull\n{getattr(result, attr) if attr else result}"
    else:
        print("no attr")
        value = f"{command}\nempty"
    send_socket_message(value)

def process_get_pose_type(cmds):
    """
    Handler：get_pose_type
    """
    position_type = cmds[1]
    position_key = RESULT_BY_OTHER_MAP.get(position_type)

    result = get_result_by_other(position_key, lFrameResult)
    if result and hasattr(result, "pose_type") and result.pose_type:
        send_socket_message(f"get_pose_type\nfull\n{result.pose_type}")
    else:
        send_socket_message("get_pose_type\nempty")

def process_get_pose_info(cmds):
    """
    Hanlder：get_pose_info
    """
    position_type = cmds[1]
    result_type = cmds[2]

    key = RESULT_BY_OTHER_MAP.get(position_type)
    if not key:
        send_socket_message("get_pose_info\nempty")
        return

    result = get_result_by_other(key, lFrameResult)
    attr = ATTR_MAP.get(result_type)

    print('result', result)
    print('attr', attr)
    send_result("get_pose_info", result, attr)

def process_get_pose_space_info(cmds):
    """
    Hanlder：get_pose_space_info
    """
    position_type = cmds[1]
    result_type = cmds[2]

    pose_name = POSE_SPACE_MAP.get(position_type)
    if not pose_name:
        send_socket_message("get_pose_space_info\nempty")
        return

    result = get_center_result_by_pose(pose_name)
    attr = ATTR_MAP.get(result_type)
    send_result("get_pose_space_info", result, attr)

def process_get_pose_point_info(cmds):
    """
    Handler: get_pose_point_info
    """
    position_type = cmds[1]
    keypoint_type = cmds[2]
    result_type = cmds[3]
    key = RESULT_BY_OTHER_MAP.get(position_type)

    result = get_result_by_other(key, lFrameResult)
    
    if not result:
        send_socket_message("get_pose_point_info\nempty")
        return

    try:
        idx = int(keypoint_type) - 1
        keypoints = result.content
        print(keypoints)

        if (idx * 2 + 1) >= len(keypoints):
            send_socket_message("get_pose_point_info\nempty")
            return

        result_key = KeyPoint(
            x=keypoints[idx * 2],
            y=keypoints[idx * 2 + 1]
        )
        attr = ATTR_MAP.get(result_type)
        send_result("get_pose_point_info", result_key, attr)
    except (ValueError, IndexError):
        send_socket_message("get_pose_point_info\nempty")


def process_get_pose_point_info_by_name(cmds):
    """
    Handler: get_pose_point_info_by_name
    """
    pose_type = cmds[1]
    keypoint_type = cmds[2]
    result_type = cmds[3]

    pose_name = POSE_SPACE_MAP.get(pose_type)
    if not pose_name:
        send_socket_message("get_pose_point_info_by_name\nempty")
        return

    result = get_center_result_by_pose(pose_name)
    if not result:
        send_socket_message("get_pose_point_info_by_name\nempty")
        return

    try:
        idx = int(keypoint_type) - 1
        keypoints = result.content

        if (idx * 2 + 1) >= len(keypoints):
            send_socket_message("get_pose_point_info_by_name\nempty")
            return

        result_key = KeyPoint(
            x=keypoints[idx * 2],
            y=keypoints[idx * 2 + 1]
        )
        attr = ATTR_MAP.get(result_type)
        send_result("get_pose_point_info_by_name", result_key, attr)
    except (ValueError, IndexError):
        send_socket_message("get_pose_point_info_by_name\nempty")

class KeyPoint:
    def __init__(self, x, y) -> None:
        self.x = x
        self.y = y

class Pose:
    def __init__(self, x, y, w, h, conf, content:list, pose_type) -> None:
        self.x = x
        self.y = y
        self.w = w
        self.h = h
        self.conf = conf
        # 17 keypoints list
        self.content = content
        self.pose_type = pose_type

class FrameResult:
    def __init__(self, time_stamp, poses: list[Pose]) -> None:
        self.time_stamp = time_stamp
        self.poses = poses


# Unix 域套接字路径
socket_path = "/tmp/my_socket"

client_socket = None
client_socket_status = False

socket_rx_string_buffer = ''


def socket_worker():
    global client_socket_status, client_socket, socket_rx_string_buffer
    while not app.need_exit():
        if not client_socket_status:
            if os.path.exists(socket_path):
                client_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                client_socket.connect(socket_path)
                client_socket_status = True
                print("Socket连接成功")

                send_socket_message('global_get_camera_flip_status')
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
                print(f"Received from server: {data.decode()}")
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
                time.sleep_ms(500)

        time.sleep_ms(25)

accept_thread = threading.Thread(target=socket_worker)
accept_thread.daemon = True
accept_thread.start()

def send_socket_message(message):
    try:
        message_packet = f"@#{message}#@"
        print('[发送成功]\n', message_packet)
        if client_socket:
            client_socket.sendall(message_packet.encode())
    except Exception as e:
        print(e)


def handler_socket_message(message: str):
    print("[收到消息]", message)
    cmds = message.split("\n")

    if cmds[0] == "global_set_camera_flip_status":
        value = cmds[1]
        if value == "1":
            set_camera_flip(1)
        else:
            set_camera_flip(0)
    elif cmds[0] == "get_pose_type":
        process_get_pose_type(cmds)

    elif cmds[0] == "get_pose_info":
        process_get_pose_info(cmds)

    elif cmds[0] == "get_pose_space_info":
        process_get_pose_space_info(cmds)

    elif cmds[0] == "get_pose_point_info":
        process_get_pose_point_info(cmds)
    elif cmds[0] == "get_pose_point_info_by_name":
        process_get_pose_point_info_by_name(cmds)


        


def get_result_by_frame(frame_no):
    return lFrameResult[-(frame_no + 1)]

def get_center_result_by_pose(pose_type: str):
    orientations = []
    result = get_result_by_frame(0)
    frame_result = result.poses
    center_x = RESOLUTION[0] / 2
    center_y = RESOLUTION[1] / 2 
    for result_indvi in frame_result:
        x = result_indvi.x
        y = result_indvi.y
        pose_type_in = result_indvi.pose_type
        if pose_type_in == pose_type:
            distance = math.sqrt((x - center_x) ** 2 + (y - center_y) ** 2)
            area_dict = {"pose": result_indvi, "distance": distance}
            orientations.append(area_dict)

    if orientations:
        center_item = min(orientations, key=lambda x: x['distance'])
        return center_item["pose"]
    else:
        return None

def get_result_by_other(mode, results):
    if mode == "center":
        orientations = []
        center_x = RESOLUTION[0] / 2
        center_y = RESOLUTION[1] / 2 
        result = get_result_by_frame(0)
        frame_result = result.poses
        for result_indvi in frame_result:
            x = result_indvi.x
            y = result_indvi.y
            distance = math.sqrt((x - center_x) ** 2 + (y - center_y) ** 2)
            area_dict = {"pose": result_indvi, "distance": distance}
            orientations.append(area_dict)

        if orientations:
            closest_item = min(orientations, key=lambda x: x['distance'])
            return closest_item["pose"]
        else:
            return None

    elif mode == "leftmost":
        orientations = []
        result = get_result_by_frame(0)
        frame_result = result.poses
        for result_indvi in frame_result:
            x = result_indvi.x
            area_dict = {"pose": result_indvi, "x": x}
            orientations.append(area_dict)

        if orientations:
            left_item = min(orientations, key=lambda x: x['x'])
            return left_item["pose"]
        else:
            return None

    elif mode == "rightmost":
        orientations = []
        result = get_result_by_frame(0)
        frame_result = result.poses
        for result_indvi in frame_result:
            x = result_indvi.x
            area_dict = {"pose": result_indvi, "x": x}
            orientations.append(area_dict)

        if orientations:
            right_item = max(orientations, key=lambda x: x['x'])
            return right_item["pose"]
        else:
            return None

    elif mode== "topmost":
        orientations = []
        result = get_result_by_frame(0)
        frame_result = result.poses
        for result_indvi in frame_result:
            y = result_indvi.y
            area_dict = {"pose": result_indvi, "y": y}
            orientations.append(area_dict)

        if orientations:
            top_item = min(orientations, key=lambda x: x['y'])
            return top_item["pose"]
        else:
            return None

    elif mode == "bottommost":
        orientations = []
        result = get_result_by_frame(0)
        frame_result = result.poses
        for result_indvi in frame_result:
            y = result_indvi.y
            area_dict = {"pose": result_indvi, "y": y}
            orientations.append(area_dict)

        if orientations:
            bottom_item = max(orientations, key=lambda x: x['y'])
            return bottom_item["pose"]
        else:
            return None

    elif mode == "highest_confidence":
        items = []
        result = get_result_by_frame(0)
        frame_result = result.poses
        for result_indvi in frame_result:
            conf = result_indvi.conf
            conf_dict = {"pose": result_indvi, "conf": conf}
            items.append(conf_dict)

        if items:
            conf_item = max(items, key=lambda x: x['conf'])
            return conf_item["pose"]
        else:
            return None

    elif mode == "lowest_confidence":
        items = []
        result = get_result_by_frame(0)
        frame_result = result.poses
        for result_indvi in frame_result:
            conf = result_indvi.conf
            conf_dict = {"pose": result_indvi, "conf": conf}
            items.append(conf_dict)

        if items:
            conf_item = min(items, key=lambda x: x['conf'])
            return conf_item["pose"]
        else:
            return None

def scale_points(coords, scale_x, scale_y):
    scaled_coords = []
    for i in range(0, len(coords), 2):
        x = round(coords[i] * scale_x)
        y = round(coords[i+1] * scale_y)
        scaled_coords.extend([x, y])
    return scaled_coords


def pose_estimate(img, disp_img, save_length: int):
    time_stamp = int(time.time())
    objs = detector.detect(img, conf_th = 0.5, iou_th = 0.45, keypoint_th = 0.5)
    individual_obj = []
    for obj in objs:
        obj.points = scale_points(obj.points, scale_x, scale_y)
        obj.x = round(obj.x * scale_x)
        obj.y = round(obj.y * scale_y)
        obj.w = round(obj.w * scale_x)
        obj.h = round(obj.h * scale_y)

    for obj in objs:
        x_mid = int(obj.x + obj.w / 2)
        y_mid = int(obj.y + obj.h / 2)
        pose_type = pose_estimator.evaluate_pose(to_keypoints_np(obj.points))
        pose = Pose(
            x=x_mid,
            y=y_mid,
            w=obj.w,
            h=obj.h,
            conf=int(obj.score * 100),
            content=obj.points,
            pose_type=pose_type
        )
        individual_obj.append(pose)
        recognized_pose = pose_estimator.compare_pose(obj.points, pose_name="Pose1", threshold=15)
        if recognized_pose:
            disp_img.draw_string(obj.x, obj.y, "Pose1", color = image.COLOR_WHITE)
            disp_img.draw_rect(obj.x, obj.y, obj.w, obj.h, color = image.COLOR_GREEN)

        else:
            disp_img.draw_rect(obj.x, obj.y, obj.w, obj.h, color = image.COLOR_WHITE, thickness=2)
            title = f'{pose_type}: {obj.score:.2f}'
            title_scale = 0.9
            title_w, title_h = image.string_size(title, 0.9)
            title_bg_h = 30
            title_x_margin = 10
            disp_img.draw_rect(obj.x, obj.y - title_bg_h, title_w + title_x_margin * 2, title_bg_h, color = image.COLOR_WHITE, thickness=-1)
            disp_img.draw_string(obj.x + title_x_margin, obj.y - title_bg_h + title_bg_h // 2 - title_h // 2, title, scale=title_scale, color = image.COLOR_BLACK)
        
        if not IS_HIDE_KEYPOINTS:
            detector.draw_pose(disp_img, obj.points, 8 if detector.input_width() > 480 else 4, image.COLOR_WHITE)

    frame_result = FrameResult(
        time_stamp=time_stamp,
        poses=individual_obj
    )
    lFrameResult.append(frame_result)

    if len(lFrameResult) > save_length:
        del lFrameResult[0]

show_key_point_btn_img = image.load("./assets/images/show_key_point_btn.png", image.Format.FMT_RGBA8888)
hide_key_point_btn_img = image.load("./assets/images/hide_key_point_btn.png", image.Format.FMT_RGBA8888)
trigger_key_point_btn_x = disp.width() - show_key_point_btn_img.width() - 6
trigger_key_point_btn_y = 6

def main(disp):
    global IS_HIDE_KEYPOINTS, show_loading, pressed_already, last_x, last_y, last_pressed

    last_tick = time.time()
    arrowLeft = image.load("./assets/images/ArrowLeft_32_32.png", image.Format.FMT_RGBA8888)

    while not app.need_exit():
        if int(time.time()) - last_tick > 5:
            send_socket_message('PASS')
            last_tick = time.time()
        
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

            if is_in_button(x, y, exit_btn_pos2):
                break
                
            elif is_in_button(x, y, ocr_btn_pos2):
                on_clicked_pose()

            elif is_in_button(x,y, learn_btn_pos):
                on_clicked_learn()  

            # 如果点击的是否隐藏姿态关键点
            elif is_in_button(x, y, [trigger_key_point_btn_x, trigger_key_point_btn_y, show_key_point_btn_img.width(), show_key_point_btn_img.height()]):
                if IS_HIDE_KEYPOINTS:
                    IS_HIDE_KEYPOINTS = False
                else:
                    IS_HIDE_KEYPOINTS = True

        img2 = cam_high.read()
        img = cam.read()
        pose_estimate(img, img2, 1)   

        img2.draw_image(exit_btn_pos[0], exit_btn_pos[1], exit_image)
        img2.draw_image(ocr_btn_pos[0], ocr_btn_pos[1], posture_recognition_image)

        if IS_HIDE_KEYPOINTS:
            img2.draw_image(trigger_key_point_btn_x, trigger_key_point_btn_y, hide_key_point_btn_img)
        else:
            img2.draw_image(trigger_key_point_btn_x, trigger_key_point_btn_y, show_key_point_btn_img)

        
        with display_show_lock:
            show_loading = False
            disp.show(img2) # 显示到屏幕

        time.sleep_ms(5) # 休眠一些时间来释放一些CPU使用

try:
    main(disp)
except Exception:
    import traceback
    msg = traceback.format_exc()
    img = image.Image(disp.width(), disp.height())
    img.draw_string(0, 0, msg, image.COLOR_WHITE)
    disp.show(img)
    while not app.need_exit():
        time.sleep_ms(100)
