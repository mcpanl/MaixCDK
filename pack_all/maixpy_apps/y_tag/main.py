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

def on_user_key(key_id, state):
    '''
        this func called in a single thread
    '''

    print(f"key: {key_id}, state: {state}") # key.c or key.State.KEY_RELEASED


key_obj = key.Key(on_user_key)

import math
from maix import app, key, pinmap, gpio, camera, touchscreen
import os
import select
import socket
from utils import is_in_button

APP_CMD_REPORT_BARCODE  = 0x05              # defined by user
APP_CMD_REPORT_QRCODE   = 0x06              # defined by user
APP_CMD_REPORT_APRILTAG = 0x07              # defined by user

AT_PRESENT_FUNC = "tag_recognition"
pressed_already = False
last_x = 0
last_y = 0
last_pressed = False

# BARCODE_STR = "barcode"
# QRCODE_STR = "qrcode"
# APRILTAG_STR = "apriltag(TAG36H11)"

april_tag_btn_img = image.load("./assets/images/april_tag_btn.png", image.Format.FMT_RGBA8888)
april_tag_btn_active_img = image.load("./assets/images/april_tag_btn_active.png", image.Format.FMT_RGBA8888)
qr_code_btn_img = image.load("./assets/images/qr_code_btn.png", image.Format.FMT_RGBA8888)
qr_code_btn_active_img = image.load("./assets/images/qr_code_btn_active.png", image.Format.FMT_RGBA8888)
bacode_btn_img = image.load("./assets/images/bacode_btn.png", image.Format.FMT_RGBA8888)
bacode_btn_active_img = image.load("./assets/images/bacode_btn_active.png", image.Format.FMT_RGBA8888)

BARCODE_STR = "Barcode"
QRCODE_STR = "QR Code "
APRILTAG_STR = "AprilTag"
code = QRCODE_STR


exit_cnt = 0
server_star_time = time.ticks_ms()

cam = camera.Camera(disp.width(), disp.height(), image.Format.FMT_RGBA8888)
cam_min = cam.add_channel(320, 240)

# detector = image.QRCodeDetector()

screen_width = 18
screen_height = disp.height()
camera_width = cam.width()
camera_height = cam.height()
ts = touchscreen.TouchScreen()  # 初始化触摸屏设备


cam_flip_init = False

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
                    print('d+1', rec_display)
                    if rec_display == 2:
                        pass
                        # rec_display = 3
                    elif rec_display == 1:
                        rec_display = 2
                    elif rec_display == 3:
                        rec_display = 1
                key_down_count += 1
            elif key_swith_left.value() == 0:
                # print(f'key_swith_left:{key_down_count}')
                if key_down_count == 2:
                    print('d-1', rec_display)
                    if rec_display == 2:
                        rec_display = 1
                    elif rec_display == 1:
                        rec_display = 3
                    elif rec_display == 3:
                        pass
                        # rec_display = 2
                key_down_count += 1
            else:
                key_down_count = 0
        time.sleep_ms(10)

key_thread = threading.Thread(target=key_scan)
key_thread.daemon = True
key_thread.start()

def send_socket_message(message):
    try:
        message_packet = f"@#{message}#@"
        print('[发送成功]\n', message_packet)
        if client_socket:
            client_socket.sendall(message_packet.encode())
    except Exception as e:
        print(e)

def set_camera_flip(state):
    global cam_flip_init
    cam.vflip(state)
    # cam.hmirror(state)
    cam_min.vflip(state)
    # cam_min.hmirror(state)
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

def get_str_width(str, thickness = 1):
    return len(str) * font_size // 2 * thickness

def get_str_height(thickness = 1):
    return font_size * thickness

def gpio_init(id):
    os.system(f"echo {id} > /sys/class/gpio/export")
    os.system(f"echo out > /sys/class/gpio/gpio{id}/direction")

def gpio_deinit(id):
    os.system(f"echo {id} > /sys/class/gpio/unexport")

def gpio_set_value(id, value):
    os.system(f"echo {value} > /sys/class/gpio/gpio{id}/value")

gpioc2 = 418
gpio_init(gpioc2)
gpio_set_value(gpioc2, 0)

# 参数列表
Coefficient = 3.7  # 系数（目前可通用各种分辨率，无需调整）
tag_size = 4.25  # AprilTag 标签边长（单位：厘米）

def calculate_k(tag_size):
    global k, Coefficient
    k = tag_size*20 / Coefficient

k = tag_size*20 / Coefficient

camera_flip_status = 0  # 镜头翻转状态
    # 上一次镜头翻转状态
last_camera_flip_status = 0

canvas_width = screen_width
canvas_height =  int(screen_height * 0.15)
# upper_canvas = image.Image(screen_width, canvas_height)
# lower_canvas = image.Image(screen_width, canvas_height)
upper_canvas_x = 0
upper_canvas_y = 0
lower_canvas_x = 0
lower_canvas_y = screen_height - canvas_height

button_w = screen_width // 4
button_h = int(canvas_height * 0.8)
button_x1 = (screen_width // 3 - button_w) // 2
button_x2 = (screen_width // 3 - button_w) // 2 + screen_width // 3
button_x3 = (screen_width // 3 - button_w) // 2 + screen_width // 3 * 2
button_y = (canvas_height - button_h) // 2

button1 = image.Image(button_w, button_h)
button1.draw_rect(0, 0, button1.width(), button1.height(), image.COLOR_BLACK, thickness = 2)
button2 = button1.copy()
button3 = button1.copy()

button1_touch = button1.copy()
button2_touch = button1.copy()
button3_touch = button1.copy()

button1_text_x = (button_w - get_str_width(BARCODE_STR)) // 2
button2_text_x = (button_w - get_str_width(QRCODE_STR)) // 2
button3_text_x = (button_w - get_str_width(APRILTAG_STR)) // 2
button_text_y = (button_h - get_str_height()) // 2
button1.draw_string(button1_text_x, button_text_y, BARCODE_STR, image.COLOR_WHITE)
button2.draw_string(button2_text_x, button_text_y, QRCODE_STR, image.COLOR_WHITE)
button3.draw_string(button3_text_x, button_text_y, APRILTAG_STR, image.COLOR_WHITE)

button1_touch.draw_rect(0, 0, button1.width(), button1.height(), image.Color.from_rgb(0x1a, 0x1a, 0x1a), thickness = -1)
button1_touch.draw_string(button1_text_x, button_text_y, BARCODE_STR, image.COLOR_WHITE)
button2_touch.draw_rect(0, 0, button1.width(), button1.height(), image.Color.from_rgb(0x1a, 0x1a, 0x1a), thickness = -1)
button2_touch.draw_string(button2_text_x, button_text_y, QRCODE_STR, image.COLOR_WHITE)
button3_touch.draw_rect(0, 0, button1.width(), button1.height(),image.Color.from_rgb(0x1a, 0x1a, 0x1a), thickness = -1)
button3_touch.draw_string(button3_text_x, button_text_y, APRILTAG_STR, image.COLOR_WHITE)

icon_back_img = image.load("./assets/images/icon_back.png", image.Format.FMT_RGBA8888)
icon_back_img_xywh = [6, 6, icon_back_img.width(), icon_back_img.height()]
roi = [camera_width // 4, camera_height // 4, camera_width // 2, camera_height // 2]

barcode_roi_w = int(camera_width // 1.5)
barcode_roi_h = int(camera_height // 3.5)
barcode_roi_x = (camera_width - barcode_roi_w) // 2
barcode_roi_y = (camera_height - barcode_roi_h) // 2
barcode_roi = [barcode_roi_x, barcode_roi_y, barcode_roi_w, barcode_roi_h]

draw_box_line_y = 0
draw_box_line_dir = 0

def get_str_width(str, thickness = 1):
    return len(str) * 24 // 2 * thickness

def get_str_height(thickness = 1):
    return 24 * thickness

def calculate_distance(x_trans, y_trans, z_trans, k):
    return k * math.sqrt(x_trans * x_trans + y_trans * y_trans + z_trans * z_trans)


def is_button1(x, y):
    rec_btn_pos_tmp = rec_btn_pos['2']
    # real_x = button_x1 + lower_canvas_x
    # real_y = button_y + lower_canvas_y
    if x > rec_btn_pos_tmp[0] and x < rec_btn_pos_tmp[0] + rec_btn_pos_tmp[2]  and y > rec_btn_pos_tmp[1] and y < rec_btn_pos_tmp[1] + rec_btn_pos_tmp[3]:
        return True
    else:
        return False

def is_button2(x, y):
    rec_btn_pos_tmp = rec_btn_pos['1']
    # real_x = button_x1 + lower_canvas_x
    # real_y = button_y + lower_canvas_y
    if x > rec_btn_pos_tmp[0] and x < rec_btn_pos_tmp[0] + rec_btn_pos_tmp[2]  and y > rec_btn_pos_tmp[1] and y < rec_btn_pos_tmp[1] + rec_btn_pos_tmp[3]:
        return True
    else:
        return False

def is_button3(x, y):
    rec_btn_pos_tmp = rec_btn_pos['3']
    # real_x = button_x1 + lower_canvas_x
    # real_y = button_y + lower_canvas_y
    if x > rec_btn_pos_tmp[0] and x < rec_btn_pos_tmp[0] + rec_btn_pos_tmp[2]  and y > rec_btn_pos_tmp[1] and y < rec_btn_pos_tmp[1] + rec_btn_pos_tmp[3]:
        return True
    else:
        return False

def is_exit(x, y, touch):
    if touch == 0:
        return False

    if x > back_btn_pos[0]  and x < back_btn_pos[0] + back_btn_pos[2]  and y > back_btn_pos[1]  and y < back_btn_pos[1] +  back_btn_pos[3]:
        return True
    else:
        return False

def draw_box(img, x, y, w, h, color, tickness = 1, hidden_line = False):
    len = int(w * 0.05)
    img.draw_line(x, y, x + len, y, color, tickness)
    img.draw_line(x, y, x, y + len, color, tickness)
    img.draw_line(x + w, y, x + w - len, y, color, tickness)
    img.draw_line(x + w, y, x + w, y + len, color, tickness)
    img.draw_line(x + w, y + h, x + w - len, y + h, color, tickness)
    img.draw_line(x + w, y + h, x + w, y + h - len, color, tickness)
    img.draw_line(x, y + h, x + len, y + h, color, tickness)
    img.draw_line(x, y + h, x, y + h - len, color, tickness)

    global draw_box_line_y
    global draw_box_line_dir
    if draw_box_line_y == 0:
        draw_box_line_y = y
        draw_box_line_dir = 1
    else:
        if draw_box_line_y > y + h - h * 0.1:
            draw_box_line_dir = -1
        elif draw_box_line_y < y + h * 0.1:
            draw_box_line_dir = 1
        draw_box_line_y += 8 * draw_box_line_dir

    if hidden_line is False:
        img.draw_line(x + int(w * 0.1), draw_box_line_y, x + w - int(w * 0.1), draw_box_line_y, color, tickness)

def find_barcodes(img, img_resize1):
    label_id = 0
    barcodes = img.find_barcodes(barcode_roi)
    mk_app_run(barcodes, img, BARCODE_STR)
    for a in barcodes:
        label_id += 1
        img.draw_string((img.width() // 2) - get_str_width(str(a.payload())) // 2, img.height() - 44 , str(a.payload()), image.COLOR_WHITE)
        break

    if (len(barcodes) > 0):
        return True
    else:
        return False

detector = image.QRCodeDetector()

def find_qrcodes(img, resize_img):
    label_id = 0
    # resize_img = img.resize(320, 240)

    # qrcodes = resize_img.find_qrcodes()
    qrcodes = detector.detect(resize_img)
    
    mk_app_run(qrcodes, resize_img, QRCODE_STR)
    for index, a in enumerate(qrcodes):
        label_id += 1
        corners = a.corners()
        for i in range(4):
            corners[i][0] = corners[i][0] * img.width() // resize_img.width()
            corners[i][1] = corners[i][1] * img.height() // resize_img.height()
        x = a.x() * img.width() // resize_img.width()
        y = a.y() * img.height() // resize_img.height()

        text_scale = 0.8
        title_bg_h = 22
        title = f"QR Code {index + 1}" 
        title_w, title_h = image.string_size(title, text_scale)

        for i in range(4):
            img.draw_line(corners[i][0], corners[i][1], corners[(i + 1) % 4][0], corners[(i + 1) % 4][1], image.COLOR_WHITE, 2)
        
        # 识别单个二维码时无需显示 QR Code 1
        if len(qrcodes) > 1:
            img.draw_rect(x, y - title_bg_h, title_w + 20, title_bg_h, image.COLOR_WHITE, -1)
            img.draw_string(x + 10, y - title_bg_h // 2 - title_h // 2, title, image.COLOR_BLACK, text_scale)

        tmp_str = a.payload()

        # 如果字符串长度超过40个字符，则截取中间用...代替
        if len(tmp_str) > 40:
            start_len = 20  # 开头保留的字符数
            mid_len = 15     # 中间保留的字符数
            
            # 计算中间部分的起始位置（字符串中间 - mid_len//2）
            mid_start = len(tmp_str) // 2 - mid_len // 2
            
            # 构造新的字符串
            tmp_str = (
                tmp_str[:start_len] + 
                "..." + 
                tmp_str[mid_start : mid_start + mid_len] + 
                "..."
            )

        if len(qrcodes) == 1:
            value_str = tmp_str # 识别单个二维码时无需显示 QR Code 1
        else:
            value_str = title + ":" + tmp_str

        value_str_w, value_str_h = image.string_size(value_str, text_scale)

        img.draw_string(disp.width() // 2 - value_str_w // 2, img.height() - 18 - 34 * (label_id) , value_str, image.COLOR_WHITE, text_scale)

    if (len(qrcodes) > 0):
        return True
    else:
        return False

# 转角处理
def rotation_handle(data):
    while data < -180:
        data = data + 360
    while data > 180:
        data = data - 360 
    return data

def find_apriltags(img, mig_min):
    families = image.ApriltagFamilies.TAG36H11
    rotation = {'x':0, 'y':0, 'z':0}
    resize_img = img.resize(160, 120)
    new_roi = [1, 1, resize_img.width()-1, resize_img.height() -1]
    apriltags = resize_img.find_apriltags(new_roi, families)
    mk_app_run(apriltags, resize_img, APRILTAG_STR)
   

    if (len(apriltags) > 0):
        return True
    else:
        return False

def calculate_rectangle_center_from_top_left(x, y, w, h):
    center_x = x + w / 2
    center_y = y + h / 2
    
    return (center_x, center_y)


def euclidean_distance(point1, point2):
    return int(math.sqrt((point1[0] - point2[0]) ** 2 + (point1[1] - point2[1]) ** 2))

def area_compute(w, h):
    return w * h

def update_json_center(json_center, tmp_json_center, x, y, label_id, resize_img):
    tmp_json_center["distance"] = euclidean_distance((x, y), (resize_img.width(), resize_img.height()))
    tmp_json_center["id"] = f'{label_id}'
    if json_center["id"] == 'None' or json_center["distance"] > tmp_json_center["distance"]:
        json_center.update(tmp_json_center)

def update_relevant_info(relevant_info_id, relevant_info_data, x, y, w, h, label_id):
    area = area_compute(w, h)
    if relevant_info_id["area_max"] == 0:
        relevant_info_id.update({'middle': label_id, 'x_min': label_id, 'x_max': label_id, 'y_min': label_id, 'y_max': label_id, 'area_min': label_id, 'area_max': label_id})
        relevant_info_data.update({"area_max": area, "area_min": area, "x_max": x, "x_min": x, "y_max": y, "y_min": y})
    else:
        if relevant_info_data["area_max"] < area:
            relevant_info_data["area_max"] = area
            relevant_info_id["area_max"] = label_id
        if relevant_info_data["area_min"] > area:
            relevant_info_data["area_min"] = area
            relevant_info_id["area_min"] = label_id

        if relevant_info_data["x_max"] < x:
            relevant_info_data["x_max"] = x
            relevant_info_id["x_max"] = label_id
        if relevant_info_data["x_min"] > x:
            relevant_info_data["x_min"] = x
            relevant_info_id["x_min"] = label_id
        if relevant_info_data["y_max"] < y:
            relevant_info_data["y_max"] = y
            relevant_info_id["y_max"] = label_id
        if relevant_info_data["y_min"] > y:
            relevant_info_data["y_min"] = y
            relevant_info_id["y_min"] = label_id

def mk_app_run(label, resize_img, mode):
    global mk_cmd_app_json_send, code
    global g_result, g_spatial_attributes, g_rotation, g_relevant_info_id, g_label_id, g_mode
    relevant_info_id = {'middle': 0, 'x_min':0, 'x_max':0, 'y_min':0, 'y_max':0, 'area_min':0, 'area_max':0}
    relevant_info_data = {'middle': 0, 'x_min':0, 'x_max':0, 'y_min': 0, 'y_max':0, 'area_min':0, 'area_max': 0}
    rotation = {}
    json_center = {"id":'None',"distance": int(0)}
    tmp_json_center = {"id":'None',"distance": int(0)}
    coord = {}
    tmp_coord = {'x':0, 'y':0, 'id':0}
    tmp_max = {'x':0, 'y':0, 'id':0}
    tmp_min = {'x':0, 'y':0, 'id':0}
    _result = {}
    label_id = 0

    if mode == BARCODE_STR:
        for a in label:
            label_id += 1
            x = a.x() * img.width() // resize_img.width()
            y = a.y() * img.height() // resize_img.height()
            w = a.w() * img.width() // resize_img.width()
            h = a.h() * img.height() // resize_img.height()

            if relevant_info_id["area_max"] == 0:
                relevant_info_id = {'middle':label_id, 'x_min':label_id, 'x_max':label_id, 'y_min':label_id, 'y_max':label_id, 'area_min':label_id, 'area_max':label_id}
                relevant_info_data["area_max"] = area_compute(a.w(), a.h())
                relevant_info_data["area_min"] = relevant_info_data["area_max"]
                relevant_info_data["x_max"] = x
                relevant_info_data["x_min"] = x
                relevant_info_data["y_max"] = y
                relevant_info_data["y_min"] = y
                tmp_max["x"] = x + a.w()
                tmp_max["y"] = y + a.h()
                tmp_max["id"] = label_id

                tmp_min["x"] = x + a.w()
                tmp_min["y"] = y + a.h()
                tmp_min["id"] = label_id
            else:
                if relevant_info_data["area_max"] < area_compute(a.w(), a.h()):
                    relevant_info_data["area_max"] = area_compute(a.w(), a.h())
                    relevant_info_id["area_max"] = label_id
                if relevant_info_data["area_min"] > area_compute(a.w(), a.h()):
                    relevant_info_data["area_min"] = area_compute(a.w(), a.h())
                    relevant_info_id["area_min"] = label_id


                if relevant_info_data["x_max"] < x:
                    relevant_info_data["x_max"] = x
                    relevant_info_id["x_max"] = label_id
                if relevant_info_data["x_min"] > x:
                    relevant_info_data["x_min"] = x
                    relevant_info_id["x_min"] = label_id
                if relevant_info_data["y_max"] < y:
                    relevant_info_data["y_max"] = y
                    relevant_info_id["y_max"] = label_id
                if relevant_info_data["y_min"] > y:
                    relevant_info_data["y_min"] = y
                    relevant_info_id["y_min"] = label_id

            if json_center["id"] == 'None':
                json_center["distance"] = euclidean_distance((x + a.w(), y + a.h()), (resize_img.width(), resize_img.height()))
                json_center["id"] = f'{label_id}'
                tmp_coord["x"] = x + a.w()
                tmp_coord["y"] = y + a.h()
                tmp_coord["id"] = label_id
            else:
                tmp_json_center["distance"] = euclidean_distance((x + a.w(), y + a.h()), (resize_img.width(), resize_img.height()))
                tmp_json_center["id"] = f'{label_id}'
                if(json_center["distance"] > tmp_json_center["distance"]):
                    json_center["distance"] = tmp_json_center["distance"]
                    json_center["id"] = tmp_json_center["id"]
                    tmp_coord["x"] = x + a.w()
                    tmp_coord["y"] = y + a.h()
                    tmp_coord["id"] = label_id

            _result[f'{label_id}'] = str(a.payload())
            coord[str(label_id)] = {"x": x+w//2, "y": y+h//2, "w": w, "h": h, "distance": tmp_json_center["distance"], "result": str(a.payload())}
            break


    elif mode == QRCODE_STR:
        for a in label:
            label_id += 1
            x = a.x() * img.width() // resize_img.width()
            y = a.y() * img.height() // resize_img.height()
            w = a.w() * img.width() // resize_img.width()
            h = a.h() * img.height() // resize_img.height()
            if relevant_info_id["area_max"] == 0:
                relevant_info_id = {'middle':label_id, 'x_min':label_id, 'x_max':label_id, 'y_min':label_id, 'y_max':label_id, 'area_min':label_id, 'area_max':label_id}
                relevant_info_data["area_max"] = area_compute(a.w(), a.h())
                relevant_info_data["area_min"] = relevant_info_data["area_max"]
                relevant_info_data["x_max"] = x
                relevant_info_data["x_min"] = x
                relevant_info_data["y_max"] = y
                relevant_info_data["y_min"] = y
                tmp_max["x"] = x + a.w()
                tmp_max["y"] = y + a.h()
                tmp_max["id"] = label_id

                tmp_min["x"] = x + a.w()
                tmp_min["y"] = y + a.h()
                tmp_min["id"] = label_id
            else:
                if relevant_info_data["area_max"] < area_compute(a.w(), a.h()):
                    relevant_info_data["area_max"] = area_compute(a.w(), a.h())
                    relevant_info_id["area_max"] = label_id
                if relevant_info_data["area_min"] > area_compute(a.w(), a.h()):
                    relevant_info_data["area_min"] = area_compute(a.w(), a.h())
                    relevant_info_id["area_min"] = label_id


                if relevant_info_data["x_max"] < x:
                    relevant_info_data["x_max"] = x
                    relevant_info_id["x_max"] = label_id
                if relevant_info_data["x_min"] > x:
                    relevant_info_data["x_min"] = x
                    relevant_info_id["x_min"] = label_id
                if relevant_info_data["y_max"] < y:
                    relevant_info_data["y_max"] = y
                    relevant_info_id["y_max"] = label_id
                if relevant_info_data["y_min"] > y:
                    relevant_info_data["y_min"] = y
                    relevant_info_id["y_min"] = label_id

            if json_center["id"] == 'None':
                json_center["distance"] = euclidean_distance((x + a.w(), y + a.h()), (resize_img.width(), resize_img.height()))
                json_center["id"] = f'{label_id}'
                # tmp_coord["x"] = x + a.w()
                # tmp_coord["y"] = y + a.h()
                # tmp_coord["id"] = label_id
            else:
                tmp_json_center["distance"] = euclidean_distance((x + a.w(), y + a.h()), (resize_img.width(), resize_img.height()))
                tmp_json_center["id"] = f'{label_id}'
                if(json_center["distance"] > tmp_json_center["distance"]):
                    json_center["distance"] = tmp_json_center["distance"]
                    json_center["id"] = tmp_json_center["id"]
                    # tmp_coord["x"] = x + a.w()
                    # tmp_coord["y"] = y + a.h()
                    # tmp_coord["id"] = label_id
                    relevant_info_id["middle"] = label_id

            _result[f'{label_id}'] = str(a.payload())
            coord[str(label_id)] = {"x": x+w//2, "y": y+h//2, "w": w, "h": h, "distance": tmp_json_center["distance"], "result": str(a.payload())}


    elif mode == APRILTAG_STR:
        for a in label:
            label_id += 1
            x = a.x() * img.width() // resize_img.width()
            y = a.y() * img.height() // resize_img.height()
            w = a.w() * img.width() // resize_img.width()
            h = a.h() * img.height() // resize_img.height()
            if relevant_info_id["area_max"] == 0:
                relevant_info_id = {'middle':label_id, 'x_min':label_id, 'x_max':label_id, 'y_min':label_id, 'y_max':label_id, 'area_min':label_id, 'area_max':label_id}
                relevant_info_data["area_max"] = area_compute(w, h)
                relevant_info_data["area_min"] = relevant_info_data["area_max"]
                relevant_info_data["x_max"] = x
                relevant_info_data["x_min"] = x
                relevant_info_data["y_max"] = y
                relevant_info_data["y_min"] = y
                tmp_max["x"] = x + w
                tmp_max["y"] = y + h
                tmp_max["id"] = label_id

                tmp_min["x"] = x + w
                tmp_min["y"] = y + h
                tmp_min["id"] = label_id
            else:
                if relevant_info_data["area_max"] < area_compute(w, h):
                    relevant_info_data["area_max"] = area_compute(w, h)
                    relevant_info_id["area_max"] = label_id
                if relevant_info_data["area_min"] > area_compute(w, h):
                    relevant_info_data["area_min"] = area_compute(w, h)
                    relevant_info_id["area_min"] = label_id

                if relevant_info_data["x_max"] < x:
                    relevant_info_data["x_max"] = x
                    relevant_info_id["x_max"] = label_id
                if relevant_info_data["x_min"] > x:
                    relevant_info_data["x_min"] = x
                    relevant_info_id["x_min"] = label_id
                if relevant_info_data["y_max"] < y:
                    relevant_info_data["y_max"] = y
                    relevant_info_id["y_max"] = label_id
                if relevant_info_data["y_min"] > y:
                    relevant_info_data["y_min"] = y
                    relevant_info_id["y_min"] = label_id

            if json_center["id"] == 'None':
                json_center["distance"] = euclidean_distance((x + w//2, y + h//2), (img.width()//2, img.height()//2))
                json_center["id"] = f'{label_id}'
                tmp_coord["x"] = x + w//2
                tmp_coord["y"] = y + h//2
                tmp_coord["id"] = label_id
            else:
                tmp_json_center["distance"] = euclidean_distance((x + w//2, y + h//2), (img.width()//2, img.height()//2))
                tmp_json_center["id"] = f'{label_id}'
                if(json_center["distance"] > tmp_json_center["distance"]):
                    json_center["distance"] = tmp_json_center["distance"]
                    json_center["id"] = tmp_json_center["id"]

                    tmp_coord["x"] = x + w//2
                    tmp_coord["y"] = y + h//2
                    tmp_coord["id"] = label_id
                    relevant_info_id["middle"] = label_id

            _result[f'{label_id}'] = str(a.id())
            rotation_x = rotation_handle(int(180 * a.x_rotation() // 3.1415) - 180) * -1
            rotation_y = rotation_handle(int(180 * a.y_rotation() // 3.1415))
            rotation_z = rotation_handle(int(180 * a.z_rotation() // 3.1415))
            rotation[str(label_id)] = {'x': rotation_x, 'y': rotation_y, 'z': rotation_z}

            corners = a.corners()
            for i in range(4):
                corners[i][0] = corners[i][0] * img.width() // resize_img.width()
                corners[i][1] = corners[i][1] * img.height() // resize_img.height()
            w = a.w() * img.width() // resize_img.width()
            h = a.h() * img.height() // resize_img.height()
            x = a.x() * img.width() // resize_img.width()
            y = a.y() * img.height() // resize_img.height()
            cx = a.cx() * img.width() // resize_img.width()
            cy = a.cy() * img.height() // resize_img.height()
            # print(f'rotation:{180 * a.x_translation() // 3.1415}')
            for i in range(4):
                img.draw_line(corners[i][0], corners[i][1], corners[(i + 1) % 4][0], corners[(i + 1) % 4][1], image.COLOR_WHITE, 2)
            # 计算距离
            x_trans = a.x_translation()
            y_trans = a.y_translation()
            z_trans = a.z_translation()
            distance = int(calculate_distance(x_trans, y_trans, z_trans, k)) // 10
            rotation["x"] = rotation_handle(int(180 * a.x_rotation() // 3.1415))
            rotation["y"] = rotation_handle(int(180 * a.y_rotation() // 3.1415))
            rotation["z"] = rotation_handle(int(180 * a.z_rotation() // 3.1415))
            disp_content = f'{a.id()} | {distance}cm {rotation["y"]}°'

            disp_content_w, disp_content_h = image.string_size(disp_content)
            disp_content_margin_y = 4
            disp_content_margin_x = 10
            title_bg_h = 30
            img.draw_rect(corners[0][0] - 3, corners[0][1] - title_bg_h, get_str_width(disp_content) + 10, title_bg_h, image.Color.from_rgb(255, 255, 255), thickness = -1)
            img.draw_string(corners[0][0] + disp_content_margin_x, corners[0][1] - title_bg_h + disp_content_margin_y, disp_content, image.COLOR_BLACK)
            
            
            coord[str(label_id)] = { "x": x + w // 2, "y": y + h // 2, "w": w, "h": h, "distance": distance, "result": str(a.id()), "x_rotation": a.x_rotation(), "y_rotation": a.y_rotation(), "z_rotation": a.z_rotation(), "x_angle": rotation_x, "y_angle": rotation_y, "z_angle": rotation_z }
            img.draw_string(corners[0][0], corners[0][1] + h + 6, f'X:{coord[str(label_id)]["x"]} Y:{coord[str(label_id)]["y"]}', image.COLOR_WHITE)

    g_result = _result
    g_spatial_attributes = coord
    g_rotation = rotation
    g_relevant_info_id = relevant_info_id
    g_label_id = label_id
    g_mode = mode

g_result = {}
g_spatial_attributes = {}
g_rotation = {}
g_relevant_info_id = {}
g_label_id = 0
g_mode = ''

def relevant_info_id_get(data, key):
    if key is not None:
        reply_data = data[key]
    else:
        reply_data = None
    return reply_data

count = 0
send_server_buf = 'None'
server_push_buf = {"cmd":0x00, "type":0, "data":None}
mk_cmd_app_json_send = {"cmd":0x00, "type":0, "data":None}
label_id = 0
    
mk_sunfunc_switc = 0
led_status = 0
eixt_flag = False

class TimeTracker:
    def __init__(self):
        self.times = []
        self.total_time = 0
        self.count = 0
        self.max_time = float('-inf')
        self.min_time = float('inf')
        self._last_average = None
        self._last_max = None
        self._last_min = None

    def record_time(self, time):
        self.count += 1
        if self.count == 1:
            return
        self.times.append(time)
        self.total_time += time
        self.max_time = max(self.max_time, time)
        self.min_time = min(self.min_time, time)
        self.print_all_statistics(time)

    @property
    def average_time(self):
        return self.total_time / self.count if self.count - 1 > 0 else 0

    def print_all_statistics(self, time):
        print(f"Current statistics - {time}ms, Average: {round(self.average_time, 3)}ms, Max: {round(self.max_time, 2)}ms, Min: {round(self.min_time, 2)}ms, Count: {self.count}")

    def get_statistics(self):
        return {
            'average': self.average_time,
            'max': self.max_time,
            'min': self.min_time,
            'count': self.count
        }

time_tracker = TimeTracker()

rec_display = 1
if code == BARCODE_STR:
    rec_display = 2
elif code == APRILTAG_STR:
    rec_display = 3

# UI显示
rec_btn_pos = dict()
back_btn_pos = (icon_back_img_xywh[0], icon_back_img_xywh[1], 72, 72) # x, y, w, h
tab_btn_h = 48
tab_btn_bg_y = (back_btn_pos[1] + icon_back_img.height() // 2) - (tab_btn_h // 2)
tab_btn_y = tab_btn_bg_y + 10
apriltag_btn_pos = (back_btn_pos[2] + 53, tab_btn_y, get_str_width(APRILTAG_STR), 29)
qr_code_btn_pos = (apriltag_btn_pos[0] + apriltag_btn_pos[2] + 56, tab_btn_y, get_str_width(QRCODE_STR), 29)
barcode_btn_pos = (qr_code_btn_pos[0] + qr_code_btn_pos[2] + 56, tab_btn_y, get_str_width(BARCODE_STR), 29)

rec_btn_pos2 = {'1':(qr_code_btn_pos[0] - 20, tab_btn_bg_y, qr_code_btn_pos[2] + 40, tab_btn_h)}
rec_btn_pos3 = {'2':(barcode_btn_pos[0] - 20, tab_btn_bg_y, barcode_btn_pos[2] + 40, tab_btn_h)}
rec_btn_pos1 = {'3':(apriltag_btn_pos[0] - 20, tab_btn_bg_y, apriltag_btn_pos[2] + 40, tab_btn_h)}
# 把rec_btn_pos1， rec_btn_pos2， rec_btn_pos3合并到rec_btn_pos
rec_btn_pos.update(rec_btn_pos1)
rec_btn_pos.update(rec_btn_pos2)
rec_btn_pos.update(rec_btn_pos3)
# 删除rec_btn_pos1， rec_btn_pos2， rec_btn_pos3
del rec_btn_pos1, rec_btn_pos2, rec_btn_pos3

def draw_btns(img : image.Image):
    global back_btn_pos, rec_display

    if rec_display == 3:
        img.draw_image(111, 18, april_tag_btn_active_img)
    else:
        img.draw_image(111, 18, april_tag_btn_img)

    if rec_display == 1:
        img.draw_image(257, 18, qr_code_btn_active_img)
    else:
        img.draw_image(257, 18, qr_code_btn_img)

    if rec_display == 2:
        img.draw_image(407, 18, bacode_btn_active_img)
    else:
        img.draw_image(407, 18, bacode_btn_img)

    img.draw_image(back_btn_pos[0], back_btn_pos[1], icon_back_img)

    if rec_display == 3:
        img.draw_string(640 - (get_str_width(f'Size:{tag_size}cm') + 31), 438, f'Size:{tag_size}cm', image.COLOR_WHITE, 1)

def handler_socket_message(message):
    global rec_display, tag_size, k

    print('[收到消息]', message)
    print("g_spatial_attributes", g_spatial_attributes)
    print("relevant_info_id", g_relevant_info_id)

    cmds = message.split("\n")
    send_data = ""


    if cmds[0] == "global_set_camera_flip_status":
        value = cmds[1]
        if value == "1":
            set_camera_flip(1)
        else:
            set_camera_flip(0)
        return

    if len(cmds) < 2:
        return
    param1 = cmds[1]
    
    if cmds[0] == "set_label_mode":
        rec_display = int(param1)
        send_data = f"set_label_mode\nfull\n1"

    if cmds[0] == "set_april_tag_size":
        tag_size = round(float(param1), 2)
        k = tag_size*20 / Coefficient
        send_data = f"set_april_tag_size\nfull\n1"

    if cmds[0] == "get_label_info":
        param2 = cmds[2]

        if len(g_spatial_attributes) > 0:
            info_id = 0
            obj = {}

            if param1 == "1":
                info_id = g_relevant_info_id["middle"]

            if param1 == "2":
                info_id = g_relevant_info_id["x_min"]

            if param1 == "3":
                info_id = g_relevant_info_id["x_max"]
            
            if param1 == "4":
                info_id = g_relevant_info_id["y_min"]
            
            if param1 == "5":
                info_id = g_relevant_info_id["y_max"]

            if param1 == "6":
                info_id = g_relevant_info_id["area_max"]

            if param1 == "7":
                info_id = g_relevant_info_id["area_min"]
            
            if info_id == 0:
                send_data = f"get_label_info\nempty"
            else:
                # 根据 id 找到对应的扫码信息
                for key, item in g_spatial_attributes.items():
                    if int(key) == info_id:
                        obj = item
                        break
                
                value = ""

                if param2 == "1":
                    value = obj["x"]
                if param2 == "2":
                    value = obj["y"]
                if param2 == "3":
                    value = obj["w"]
                if param2 == "4":
                    value = obj["h"]
                
                send_data = f"get_label_info\nfull\n{value}"
        else:
            send_data = f"get_label_info\nempty"
    
    if cmds[0] == "get_label_parse_result":
        if len(g_spatial_attributes) > 0:
            info_id = 0
            obj = {}

            if param1 == "1":
                info_id = g_relevant_info_id["middle"]

            if param1 == "2":
                info_id = g_relevant_info_id["x_min"]

            if param1 == "3":
                info_id = g_relevant_info_id["x_max"]
            
            if param1 == "4":
                info_id = g_relevant_info_id["y_min"]
            
            if param1 == "5":
                info_id = g_relevant_info_id["y_max"]

            if param1 == "6":
                info_id = g_relevant_info_id["area_max"]

            if param1 == "7":
                info_id = g_relevant_info_id["area_min"]
            
            if info_id == 0:
                send_data = f"get_label_parse_result\nempty"
            else:
                # 根据 id 找到对应的扫码信息
                for key, item in g_spatial_attributes.items():
                    if int(key) == info_id:
                        obj = item
                        break

                if obj['result']:
                    result = obj["result"]
                    send_data = f"get_label_parse_result\nfull\n{result}"
                else:
                    send_data = f"get_label_parse_result\nempty"
        else:
            send_data = f"get_label_parse_result\nempty"

    if cmds[0] == "get_label_info_by_parse_result":
        if len(g_spatial_attributes) > 0:
            obj = {}

            # 根据扫码结果找到对应的扫码信息
            for key, item in g_spatial_attributes.items():
                if item["result"] == param1:
                    obj = item
                    break
            
            if len(obj) > 0:
                param2 = cmds[2]
                value = ""

                if param2 == "1":
                    value = obj["x"]
                if param2 == "2":
                    value = obj["y"]
                if param2 == "3":
                    value = obj["w"]
                if param2 == "4":
                    value = obj["h"]

                send_data = f"get_label_info_by_parse_result\nfull\n{value}"
            else:
                send_data = f"get_label_info_by_parse_result\nempty"
        else:
            send_data = f"get_label_info_by_parse_result\nempty"

    if cmds[0] == "get_april_tag_angle_offset":
        if len(g_spatial_attributes) > 0:
            info_id = 0
            obj = {}

            if param1 == "1":
                info_id = g_relevant_info_id["middle"]
            if param1 == "2":
                info_id = g_relevant_info_id["x_min"]
            if param1 == "3":
                info_id = g_relevant_info_id["x_max"]
            if param1 == "4":
                info_id = g_relevant_info_id["y_min"]
            if param1 == "5":
                info_id = g_relevant_info_id["y_max"]
            if param1 == "6":
                info_id = g_relevant_info_id["area_max"]
            if param1 == "7":
                info_id = g_relevant_info_id["area_min"]
            
            if info_id == 0:
                send_data = f"get_april_tag_angle_offset\nempty"
            else:
                # 根据 id 找到对应的扫码信息
                for key, item in g_spatial_attributes.items():
                    if int(key) == info_id:
                        obj = item
                        break
                
                param2 = cmds[2]
                value = ""

                if param2 == "1":
                    value = obj["y_angle"]
                if param2 == "2":
                    value = obj["x_angle"]
                if param2 == "3":
                    value = obj["z_angle"]
                
                send_data = f"get_april_tag_angle_offset\nfull\n{value}"
        else:
            send_data = f"get_april_tag_angle_offset\nempty"
    
    if cmds[0] == "get_april_tag_angle_offset_by_result":
        if len(g_spatial_attributes) > 0:
            obj = {}
            
            # 根据 id 找到对应的扫码信息
            for key, item in g_spatial_attributes.items():
                if item["result"] == param1:
                    obj = item
                    break
            
            if len(obj) > 0:
                param2 = cmds[2]
                value = ""

                if param2 == "1":
                    value = obj["y_angle"]
                if param2 == "2":
                    value = obj["x_angle"]
                if param2 == "3":
                    value = obj["z_angle"]
                    
                send_data = f"get_april_tag_angle_offset_by_result\nfull\n{value}"
            else:
                send_data = f"get_april_tag_angle_offset_by_result\nempty"
        else:
            send_data = f"get_april_tag_angle_offset_by_result\nempty"

    if cmds[0] == "get_april_tag_distance":
        if len(g_spatial_attributes) > 0:
            info_id = 0
            obj = {}

            if param1 == "1":
                info_id = g_relevant_info_id["middle"]

            if param1 == "2":
                info_id = g_relevant_info_id["x_min"]

            if param1 == "3":
                info_id = g_relevant_info_id["x_max"]
            
            if param1 == "4":
                info_id = g_relevant_info_id["y_min"]
            
            if param1 == "5":
                info_id = g_relevant_info_id["y_max"]

            if param1 == "6":
                info_id = g_relevant_info_id["area_max"]

            if param1 == "7":
                info_id = g_relevant_info_id["area_min"]
            
            if info_id == 0:
                send_data = f"get_april_tag_distance\nempty"
            else:
                # 根据 id 找到对应的扫码信息
                for key, item in g_spatial_attributes.items():
                    if int(key) == info_id:
                        obj = item
                        break
                
                distance = obj["distance"]
                send_data = f"get_april_tag_distance\nfull\n{distance}"
        else:
            send_data = f"get_april_tag_distance\nempty"

    if cmds[0] == "get_april_tag_distance_by_result":
        if len(g_spatial_attributes) > 0:
            obj = {}
            
            # 根据 id 找到对应的扫码信息
            for key, item in g_spatial_attributes.items():
                if item["result"] == param1:
                    obj = item
                    break
            
            if len(obj) > 0:
                distance = obj["distance"]
                send_data = f"get_april_tag_distance_by_result\nfull\n{distance}"
            else:
                send_data = f"get_april_tag_distance_by_result\nempty"
        else:
            send_data = f"get_april_tag_distance_by_result\nempty"

    if send_data:
        send_socket_message(send_data)
    
    print("send_data", repr(send_data))



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
                            try:
                                handler_socket_message(packet)
                            except Exception as e:
                                pass
                        
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

last_tick = time.time()

while not app.need_exit():
    if int(time.time()) - last_tick > 5:
        send_socket_message('PASS')
        last_tick = time.time()
    # 记录循环开始时间
    loop_start_time = time.ticks_ms()

    if last_camera_flip_status != camera_flip_status:
        cam.vflip(camera_flip_status)
        cam_min.vflip(camera_flip_status)
        last_camera_flip_status = camera_flip_status

    img = cam.read()
    img_min = cam_min.read()
    if img:
        res = False
        # if code == BARCODE_STR:
        if rec_display == 2:
            res = find_barcodes(img, img_min)
            # rec_display = 2
        # elif code == QRCODE_STR:
        elif rec_display == 1:
            res = find_qrcodes(img, img_min)
            # rec_display = 1
        # elif code == APRILTAG_STR:
        elif rec_display == 3:
            res = find_apriltags(img, img_min)
            # rec_display = 3
        else:
            print("error code: ", code)
            break

        if not disp.is_opened():
            break

        touch_x, touch_y, pressed = ts.read()

        # 防误触设计，模拟用户按压屏幕松开后才触发
        if touch_x != last_x or touch_y != last_y or pressed != last_pressed:
            last_x = touch_x
            last_y = touch_y
            last_pressed = pressed
        if pressed:
            pressed_already = True
        elif pressed_already:
            pressed_already = False

            if is_in_button(touch_x, touch_y, icon_back_img_xywh):
                app.set_exit_flag(True)
                break

            if mk_sunfunc_switc == 2 or is_button1(touch_x, touch_y) is True:
                rec_display = 2
                mk_sunfunc_switc = 0
                code = BARCODE_STR

            if mk_sunfunc_switc == 1 or is_button2(touch_x, touch_y) is True:
                rec_display = 1
                mk_sunfunc_switc = 0
                code = QRCODE_STR

            if mk_sunfunc_switc == 3 or is_button3(touch_x, touch_y) is True:
                rec_display = 3
                mk_sunfunc_switc = 0
                code = APRILTAG_STR

        # 显示exit按钮
        draw_btns(img)

        with display_show_lock:
            show_loading = False
            disp.show(img) # 显示到屏幕

        time.sleep_ms(10)
        
        # handler_socket_message("get_april_tag_distance_by_result\n0")

        # 记录循环结束时间，统计循环时间
        # loop_end_time = time.ticks_ms()
        # loop_execution_time = loop_end_time - loop_start_time
        # time_tracker.record_time(loop_execution_time)