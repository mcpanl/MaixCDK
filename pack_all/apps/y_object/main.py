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

mode = 0

from maix import key

longpress_lock = False

def on_user_key(key_id, state):
    global show_delete_popup, rect_lock, longpress_lock

    if key_id == 352:
        if state == 0 and longpress_lock == False:
            if mode == 1:
                with rect_lock:
                    show_del_btn = False
                    
                    # 如果存在自学习后的对象
                    if len(rect_list) > 0:
                        for index, value in enumerate(rect_list):
                            obj = value["tracker"].track(img2)

                            # 如果物体在屏幕画面内才显示识别框
                            if obj.score > score_threshold:
                                # 如果屏幕的准心在该物体范围内
                                if is_in_button(screen_center_x, screen_center_y, [obj.x, obj.y, obj.w, obj.h]):
                                    show_del_btn = True
                                    break

                    if show_del_btn:
                        show_delete_popup = True
                    else:
                        learning()
        elif state == 1:
            longpress_lock = False
        elif state == 2:
            longpress_lock = True
    elif key_id == 352 and state == 0:
        pass

# 覆盖原来的用户按钮事件
key_obj = key.Key(on_user_key)

from maix import camera, app, nn, touchscreen, pinmap, gpio
import math
import select
import socket
import os
import queue
from utils import is_in_button
from drag_rect import DragRect
from delete_popup import DeletePopup

cam_flip_init = False
rect_lock = threading.Lock()
touch = touchscreen.TouchScreen()
status = 0  # 0: select target box, 1: tracking
target = nn.Object()
btn_str = "Select"
font_size = image.string_size(btn_str)
img_back = image.load("/maixapp/share/icon/ret.png", image.Format.FMT_RGBA8888)
icon_back_img = image.load("./assets/images/icon_back.png", image.Format.FMT_RGBA8888)
object_recognition_btn_img = image.load("./assets/images/object_recognition_btn.png", image.Format.FMT_RGBA8888)
object_recognition_btn_active_img = image.load("./assets/images/object_recognition_btn_active.png", image.Format.FMT_RGBA8888)
object_learning_btn_img = image.load("./assets/images/object_learning_btn.png", image.Format.FMT_RGBA8888)
object_learning_btn_active_img = image.load("./assets/images/object_learning_btn_active.png", image.Format.FMT_RGBA8888)
learn_btn_img = image.load("./assets/images/learn_btn.png", image.Format.FMT_RGBA8888)
clear_learn_btn_img = image.load("./assets/images/clear_learn_btn.png", image.Format.FMT_RGBA8888)
screen_width = disp.width()
screen_height = disp.height()
screen_center_x = disp.width() // 2 # 屏幕中间的准心位置 x
screen_center_y = disp.height() // 2 # 屏幕中间的准心位置 y
rect_list = [] # 方框列表
rect_learn_list = [] # 方框学习列表
closest_middle_rect = None # 最中间的方框
closest_middle_rect_min_distance = float('inf')
leftmost_rect = None # 最靠左的方框
rightmost_rect = None # 最靠右的方框
topmost_rect = None # 最靠上的方框
bottommost_rect = None # 最靠下的方框
max_score = None # 置信度最高的方框
min_score = None # 置信度最高的方框
max_area = None # 最大的面积
min_area = None # 最小的面积
max_rect = None # 最大的方框
min_rect = None # 最小的方框
rect_id = 0 # 方框 ID
obj_text_scale = 1
obj_title_h = 30
back_btn_x = 6
back_btn_y = 6
tab_x = 85
tab_y = back_btn_y + (icon_back_img.height() // 2) - (object_learning_btn_img.height() // 2)
tab2_x = 345
tab2_y = tab_y
learn_btn_x = 561
learn_btn_y = 6
title_y_margin = 4
dragRect = DragRect(screen_width, screen_height) # 屏幕中间可拖拽的方框
delete_popup = DeletePopup(disp) # 删除确认弹窗
show_delete_popup = False
touch_x = None
touch_y = None
score_threshold = 0.8
want_del_rect_index = -1
pressed_already = False
last_x = 0
last_y = 0
last_pressed = False
event_queue = queue.Queue()

# 初始化物体检测模型
detector = nn.YOLOv5(model="/root/models/yolov5s.mud", dual_buff=False)
# detector = nn.YOLOv8(model="/root/models/yolov8n.mud", dual_buff=True)
# detector = nn.YOLO11(model="/root/models/yolo11n.mud", dual_buff=True)
cam = camera.Camera(disp.width(), disp.height(), image.Format.FMT_RGBA8888)
cam2 = cam.add_channel(disp.width(), disp.height(), detector.input_format())
switch_camera_lock = threading.Thread()

# 初始化自学习检测器模型
model_path = "/root/models/nanotrack.mud"
tracker = nn.NanoTrack(model_path)

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
    global rec_display, back, mode, rect_list, rect_id, cam2
    
    key_init()

    last_key_time = time.ticks_ms()
    last_key_state = 0
    key_down_count = 0
    
    while not app.need_exit():
        # 实现10ms扫描一次按键
        if time.ticks_ms() - last_key_time > 60:
            last_key_time = time.ticks_ms()
            
            if key_swith_mid.value() == 0:
                if key_down_count == 2:
                    back = True
                    app.set_exit_flag(True)
                    break
                key_down_count += 1
            elif key_swith_right.value() == 0:
                with rect_lock:
                    if key_down_count == 1:
                        if mode < 1:
                            print('d+1')
                            mode = 1
                            rect_id = 0
                            rect_list = rect_learn_list
                            # del cam2 
                            # cam2 = cam.add_channel(disp.width(), disp.height(), tracker.input_format())
                key_down_count += 1
            elif key_swith_left.value() == 0:
                with rect_lock:
                    if key_down_count == 1:
                        if mode > 0:
                            print('d-1')
                            mode = 0
                            rect_id = 0
                            # del cam2
                            # cam2 = cam.add_channel(disp.width(), disp.height(), detector.input_format())
                key_down_count += 1
            else:
                key_down_count = 0
        time.sleep_ms(10)

key_thread = threading.Thread(target=key_scan)
key_thread.daemon = True
key_thread.start()

def learning():
    global rect_id, rect_list
    
    target.x = dragRect.rect_x
    target.y = dragRect.rect_y
    target.w = dragRect.rect_width
    target.h = dragRect.rect_height

    if(len(rect_list) >= 5):
        del rect_list[0] # 只保留最新的 5 条

    timestamp = int(time.time() * 1000)
    rect_id += 1

    dict_data = {
        "name": 'ID' + str(rect_id),
        "tracker": nn.NanoTrack(model_path),
        "img": img,
        "x": target.x,
        "y": target.y,
        "w": target.w,
        "h": target.h,
        "score": target.score,
        "created_time":timestamp
    }

    dict_data["tracker"].init(img2, target.x, target.y, target.w, target.h)
    
    rect_list.append(dict_data)

# Unix 域套接字路径
socket_path = "/tmp/my_socket"

client_socket = None
client_socket_status = False

socket_rx_string_buffer = ''

def send_socket_message(message):
    message_packet = f"@#{message}#@"
    print('[发送成功]\n', message_packet)
    if client_socket:
        client_socket.sendall(message_packet.encode())


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
                tmp_str = data.decode(errors='ignore')
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
                            with rect_lock:
                                handler_socket_message(packet)
                        
                        socket_rx_string_buffer = socket_rx_string_buffer[end_idx+2:]
                    else:
                        # 没有完整数据包时退出循环
                        break
            else:
                # 如果接收到空数据，说明连接已关闭
                print("Connection closed by server")

        # time.sleep_ms(25)

def set_camera_flip(state):
    global cam_flip_init
    cam.vflip(state)
    cam2.vflip(state)
    cam_flip_init = True
    # cam.hmirror(state)

def wait_camera_flip_init():
    while(not cam_flip_init):
        try:
            send_socket_message('global_get_camera_flip_status')
        except Exception as e:
            print(e)
        time.sleep_ms(250)

def rename_rect_id(rect_list, old_rect_name, new_rect_name):
    for rect in rect_list:
        if rect["name"] == old_rect_name:
            rect["name"] = new_rect_name
            break
    return rect_list

# 获取两点之间的的距离
def distance(x1, y1, x2, y2):
    return math.sqrt((x2 - x1) ** 2 + (y2 - y1) ** 2)

# 获取方框的中心坐标
def get_rect_center(x1, y1, x2, y2):
    center_x = (x1 + x2) // 2
    center_y = (y1 + y2) // 2
    return center_x, center_y

def format_score(score):
    return round(score * 100)

def handler_socket_message(message):
    global rect_list, rect_id

    print('[收到消息]', message)

    cmds = message.split("\n")
    send_data = ""

    if len(cmds) > 1:
        param1 = cmds[1]
    else:
        param1 = 0

    if cmds[0] == "global_set_camera_flip_status":
        value = cmds[1]
        if value == "1":
            set_camera_flip(1)
        else:
            set_camera_flip(0)

    if cmds[0] == "get_object_count":
        send_socket_message(f"get_object_count\n{len(rect_list)}")

    if cmds[0] == "set_object_name":
        str1 = cmds[1]
        str2 = cmds[2]

        print(f"需要将{str1}重命名为{str2}")
        try:
            rect_list = rename_rect_id(rect_list, str1, str2)
        except Exception as e:
            print(e)
        

    if cmds[0] == "delete_object_name":
        str1 = cmds[1]

        try:
            # 根据名称删除
            del_idnex = -1

            for index, item in enumerate(rect_list):
                if item["name"] == str1:
                    del_idnex = index
                    break
            
            if del_idnex != -1:
                del rect_list[del_idnex] # 使用索引方式来删除才不会导致引用丢失
        except Exception as e:
            print(e)

    if cmds[0] == "get_object_info":
        param2 = cmds[2]
        obj = None
        
        if param1 == "1":
            obj = closest_middle_rect

        if param1 == "2":
            obj = leftmost_rect

        if param1 == "3":
            obj = rightmost_rect
        
        if param1 == "4":
            obj = topmost_rect
        
        if param1 == "5":
            obj = bottommost_rect

        if param1 == "6":
            obj = max_area

        if param1 == "7":
            obj = min_area

        if param1 == "8":
            obj = max_score

        if param1 == "9":
            obj = min_score
        
        if obj != None and len(rect_list) > 0:
            value = ""

            if param2 == "1":
                value = obj['x'] + obj['w'] // 2
            if param2 == "2":
                value = obj['y'] + obj['h'] // 2
            if param2 == "3":
                value = obj['w']
            if param2 == "4":
                value = obj['h']
            if param2 == "5":
                value = format_score(obj['score'])
            
            send_data = f"get_object_info\nfull\n{value}"
        else:
            send_data = f"get_object_info\nempty"

    if cmds[0] == "get_object_name":
        obj = {}
        
        if param1 == "1":
            obj = closest_middle_rect

        if param1 == "2":
            obj = leftmost_rect

        if param1 == "3":
            obj = rightmost_rect
        
        if param1 == "4":
            obj = topmost_rect
        
        if param1 == "5":
            obj = bottommost_rect

        if param1 == "6":
            obj = max_area

        if param1 == "7":
            obj = min_area

        if param1 == "8":
            obj = max_score

        if param1 == "9":
            obj = min_score
        
        if obj != None and len(rect_list) > 0:
            send_data = f"get_object_name\nfull\n{obj['name']}"
        else:
            send_data = f"get_object_name\nempty"

    if cmds[0] == "get_object_info_by_name":
        param2 = cmds[2]
        obj = {}
        is_find = False
        min_distance = float('inf')

        # 如果画面中有，则返回该物体对应的信息
        for value in rect_list:
            if param1 == value["name"] and value["x"] != -1 and value["y"] != -1:
                # 计算当前物体中心到屏幕中心的距离
                rect_center_x, rect_center_y = get_rect_center(value["x"], value["y"], value["x"] + value["w"], value["y"] + value["h"])
                dist = distance(rect_center_x, rect_center_y, screen_center_x, screen_center_y)
                
                # 选择最靠近屏幕中心的物体
                if dist < min_distance:
                    min_distance = dist
                    obj = value
                    is_find = True

        if obj != None and len(obj) > 0 and is_find:
            value = ""

            if param2 == "1":
                value = obj["x"] + obj["w"] // 2

            if param2 == "2":
                value = obj["y"] + obj["h"] // 2
                
            if param2 == "3":
                value = obj["w"]

            if param2 == "4":
                value = obj["h"]

            if param2 == "5":
                value = format_score(obj["score"])
            
            send_data = f"get_object_info_by_name\nfull\n{value}"
        else:
            send_data = f"get_object_info_by_name\nempty"

    if send_data:
        send_socket_message(send_data)
    
    # print('send_data', repr(send_data))

def set_info(obj):
    global closest_middle_rect, closest_middle_rect_min_distance, leftmost_rect, rightmost_rect, topmost_rect, bottommost_rect, max_score, min_score, max_area, min_area
    
    # 计算当前方框的中心坐标
    rect_center_x, rect_center_y = get_rect_center(obj["x"], obj["y"], obj["x"] + obj["w"], obj["y"] + obj["h"])

    # 计算当前方框中心到屏幕中心的距离
    dist = distance(rect_center_x, rect_center_y, screen_center_x, screen_center_y)

    # 找到最靠近中间的方框
    if closest_middle_rect == None or dist < closest_middle_rect_min_distance:
        closest_middle_rect = obj
        closest_middle_rect_min_distance = dist

    # 找到最靠左的方框
    if leftmost_rect == None or obj["x"] < leftmost_rect["x"]:
        leftmost_rect = obj
    
    # 找到最靠右的方框
    if rightmost_rect == None or obj["x"] > rightmost_rect["x"]:
        rightmost_rect = obj
    
    # 找到最靠上的方框
    if topmost_rect == None or obj["y"] < topmost_rect["y"]:
        topmost_rect = obj
    
    # 找到最靠下的方框
    if bottommost_rect == None or obj["y"] > bottommost_rect["y"]:
        bottommost_rect = obj
    
    # 找到最大面积的
    if max_area == None or obj["area"] > max_area["area"]:
        max_area = obj

    # 找到最小面积的
    if min_area == None or obj["area"] < min_area["area"]:
        min_area = obj

    # 找到置信度最高的
    if max_score == None or obj["score"] > max_score["score"]:
        max_score = obj
    
    # 找到置信度最低的
    if min_score == None or obj["score"] < min_score["score"]:
        min_score = obj
    
    # print('最居中的方框', closest_middle_rect)
    # print('最靠左的方框', leftmost_rect)
    # print('最靠右的方框', rightmost_rect)
    # print('置信度最高的', min_score)
    # print('置信度最低的', max_score)

accept_thread = threading.Thread(target=socket_worker)
accept_thread.daemon = True
accept_thread.start()

accept_thread2 = threading.Thread(target=wait_camera_flip_init)
accept_thread2.daemon = True
accept_thread2.start()

try:
    last_tick = time.time()

    while not app.need_exit():
        if int(time.time()) - last_tick > 5:
            send_socket_message('PASS') # 发送心跳保持连接
            last_tick = time.time()
        
        with rect_lock:
            img = cam.read()
            img2 = cam2.read()
            touch_x, touch_y, pressed = touch.read()

            img.draw_image(back_btn_x, back_btn_y, icon_back_img) # 绘制后退按钮

            closest_middle_rect_min_distance = float('inf')
            closest_middle_rect = None
            leftmost_rect = None
            rightmost_rect = None
            topmost_rect = None
            bottommost_rect = None
            max_area = None
            min_area = None
            max_score = None
            min_score = None
            show_del_btn = False
            want_del_rect_index = -1

            if mode == 0:
                img.draw_image(tab_x, tab_y, object_recognition_btn_active_img) # 绘制导航栏
                img.draw_image(tab2_x, tab2_y, object_learning_btn_img) # 绘制导航栏

                objs = detector.detect(img2, conf_th = 0.5, iou_th = 0.45)
                title_x_margin = 10

                rect_list = []

                # print("检测到的物体数量", len(objs))

                for index, value in enumerate(objs):
                    img.draw_rect(value.x, value.y, value.w, value.h, image.Color.from_rgb(9, 232, 50), thickness=2)

                    name = detector.labels[value.class_id].capitalize()
                    msg = f'{name} {value.score:.2f}'
                    name_w, name_h = image.string_size(msg, obj_text_scale) # 获取物体名称的宽高
                    img.draw_rect(value.x, value.y - obj_title_h, name_w + title_x_margin * 2, obj_title_h, image.Color.from_rgb(9, 232, 50), thickness=-1) # 绘制物体名称的背景
                    img.draw_string(value.x + title_x_margin, value.y - obj_title_h // 2 - name_h // 2, msg, image.Color.from_rgb(255, 255, 255), obj_text_scale) # 绘制物体名称

                    timestamp = int(time.time() * 1000)

                    dict_data = {
                        "x": value.x,
                        "y": value.y,
                        "w": value.w,
                        "h": value.h,
                        "score": value.score,
                        "created_time": timestamp,
                        "name": name,
                        "area": value.w * value.h
                    }

                    rect_list.append(dict_data)
                    set_info(dict_data)

            if mode == 1:
                img.draw_image(tab_x, tab_y, object_recognition_btn_img) # 绘制导航栏
                img.draw_image(tab2_x, tab2_y, object_learning_btn_active_img) # 绘制导航栏

                dragRect.drag_and_draw(img, touch_x, touch_y, pressed)

                # 如果存在自学习后的对象
                if len(rect_list) > 0:
                    for index, value in enumerate(rect_list):
                        obj = value["tracker"].track(img2)

                        # 更新数据
                        value["w"] = obj.w
                        value["h"] = obj.h
                        value["score"] = obj.score
                        value["area"] = obj.w * obj.h

                        # 如果物体在屏幕画面内才显示识别框
                        if obj.score > score_threshold:
                            value["x"] = obj.x
                            value["y"] = obj.y
                            title_x_margin = 10

                            set_info(value)

                            # 绘制自学习后的对象
                            img.draw_rect(obj.x, obj.y, obj.w, obj.h, image.Color.from_rgb(9, 232, 50), 4)
                            id_w, id_h = image.string_size(value['name'], obj_text_scale) # 获取物体ID的宽高
                            img.draw_rect(obj.x - 2, obj.y - obj_title_h, id_w + title_x_margin * 2, obj_title_h, image.Color.from_rgb(9, 232, 50), thickness=-1) # 绘制物体ID的背景
                            img.draw_string(obj.x + title_x_margin, obj.y - obj_title_h // 2 - id_h // 2, value['name'], image.Color.from_rgb(255, 255, 255), obj_text_scale)
                            
                            # 如果屏幕的准心在该物体范围内
                            if is_in_button(screen_center_x, screen_center_y, [obj.x, obj.y, obj.w, obj.h]):
                                show_del_btn = True
                                want_del_rect_index = index
                                
                        else:
                            value["x"] = -1
                            value["y"] = -1
                    
                if show_del_btn:
                    img.draw_image(learn_btn_x, learn_btn_y, clear_learn_btn_img) # 绘制删除学习按钮
                else:
                    img.draw_image(learn_btn_x, learn_btn_y, learn_btn_img) # 绘制学习按钮
            
            # 防误触设计，模拟用户按压屏幕松开后才触发
            if touch_x != last_x or touch_y != last_y or pressed != last_pressed:
                last_x = touch_x
                last_y = touch_y
                last_pressed = pressed
            if pressed:
                pressed_already = True
            elif pressed_already:
                pressed_already = False
                # 如果当前显示的是删除弹窗
                if show_delete_popup:
                    # 如果点击删除弹窗的取消按钮或关闭按钮
                    if delete_popup.is_touch_cancel(touch_x, touch_y) or delete_popup.is_touch_close(touch_x, touch_y):
                        show_delete_popup = False

                    # 如果点击删除弹窗的确认按钮
                    elif delete_popup.is_touch_confirm(touch_x, touch_y):
                        show_delete_popup = False

                        del rect_list[want_del_rect_index]
                else:
                    # 如果点击导航栏的标签1
                    if is_in_button(touch_x, touch_y, [tab_x, tab_y, object_recognition_btn_img.width(), object_recognition_btn_img.height()]) and mode != 0:
                        mode = 0
                        rect_id = 0

                        del cam2
                        cam2 = cam.add_channel(disp.width(), disp.height(), detector.input_format())
                    
                    # 如果点击导航栏的标签2
                    elif is_in_button(touch_x, touch_y, [tab2_x, tab2_y, object_learning_btn_img.width(), object_learning_btn_img.height()]) and mode != 1:
                        mode = 1
                        rect_id = 0
                        rect_list = rect_learn_list

                        del cam2
                        cam2 = cam.add_channel(disp.width(), disp.height(), tracker.input_format())

                    # 如果点击后退按钮
                    elif is_in_button(touch_x, touch_y, [back_btn_x, back_btn_y, icon_back_img.width(), icon_back_img.height()]):
                        app.set_exit_flag(True)
                    
                    # 如果点击删除学习按钮
                    elif is_in_button(touch_x, touch_y, [learn_btn_x, learn_btn_y, clear_learn_btn_img.width(), clear_learn_btn_img.height()]) and mode == 1 and show_del_btn:
                        show_delete_popup = True
                    
                    # 如果点击学习按钮
                    elif is_in_button(touch_x, touch_y, [learn_btn_x, learn_btn_y, learn_btn_img.width(), learn_btn_img.height()]) and mode == 1 and not show_del_btn:
                        learning()
            
            # handler_socket_message("get_object_info_by_name\nCell phone\n3")

            if show_delete_popup:
                delete_popup.draw(img)
            
            with display_show_lock:
                show_loading = False
                disp.show(img) # 显示到屏幕
        
        time.sleep_ms(5) # 休眠一些时间来释放一些CPU使用
except Exception as e:
    print(e)