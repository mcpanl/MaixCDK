from maix import display, image, time
import threading
import traceback
from loading import Loading


def boot_log(phase, detail=""):
    msg = "[y_hand/boot] " + str(phase) + (" | " + str(detail) if detail else "")
    print(msg)
    try:
        import sys
        sys.stdout.flush()
    except Exception:
        pass


font_size = 20
image.load_font("sourcehansans", "/maixapp/share/font/SourceHanSansCN-Regular.otf", size = font_size)  # 加载自定义字体
image.set_default_font("sourcehansans")  # 设置默认字体

disp = display.Display()
show_loading = True
loaindg = Loading(disp)
loading_thread = None
display_show_lock = threading.Lock()
mk_objs_info_list_lock = threading.Lock()

# 加载动画，import 逻辑放到加载动画后面用来以防黑屏等待提升用户体验
_loading_stall_last = 0


def loading_thread_main():
    global show_loading, loading_thread, _loading_stall_last

    should_break = False

    while 1:
        with display_show_lock:
            if show_loading:
                img = loaindg.draw()
                disp.show(img)
                t = time.time()
                if t - _loading_stall_last >= 2.0:
                    _loading_stall_last = t
                    boot_log(
                        "loading_ui",
                        "show_loading still True (main thread has not cleared loading yet)",
                    )
            else:
                should_break = True
        
        if should_break:
            break

        time.sleep_ms(5)

loading_thread = threading.Thread(target=loading_thread_main, daemon=True)
loading_thread.start()

tmp_time = 0
pressed_flag = [False, False, False, False]  # 按钮按下标志
rec_display = 0  # 模式标志

from maix import key

longpress_lock = False

def on_user_key(key_id, state):
    global key_state, pressed_flag, rec_display,learn, show_delete_popup, want_del_hand_class_id, tmp_time, longpress_lock

    key_state = state

    if key_id == 352:
        if state == 0 and longpress_lock == False:
            if rec_display == 0:
                pressed_flag[3] = not pressed_flag[3]
            elif rec_display == 1:
                show_del_btn = False

                with mk_objs_info_list_lock: # mk_objs_info_list_lock 避免遍历的途中发送数据
                    for obj in mk_objs_info_list:
                        gesture_name = name_classes[obj["class_idx"]] # 获取手势名称
                        
                        # 如果屏幕的准心在该已经学习的手势范围内
                        if is_in_button(screen_center_x, screen_center_y, [obj["coords"][0], obj["coords"][1], obj["coords"][2], obj["coords"][3]]) and learned_gestures[gesture_name] != "":
                            want_del_hand_class_id = obj["class_idx"]
                            show_del_btn = True
                            break
                
                # 如果点击的是删除
                if show_del_btn:
                    show_delete_popup = True
                else:
                    learn = True
        elif state == 1:
            longpress_lock = False
        elif state == 2:
            longpress_lock = True

# 覆盖原来的用户按钮事件
key_obj = key.Key(on_user_key)

from maix import camera, nn, app, touchscreen, pinmap, gpio
import asyncio,sys,re
import os
import select
import socket
from delete_popup import DeletePopup

AT_PRESENT_FUNC = "gesture_recognition"  # 当前应用功能标识

# 定义全局变量
back_btn = image.load("./assets/images/back_btn.png", image.Format.FMT_RGBA8888)
gesture_recognition_btn_section = image.load("./assets/images/gesture_recognition_btn_section.png", image.Format.FMT_RGBA8888)
gesture_recognition_active_btn = image.load("./assets/images/gesture_recognition_active_btn.png", image.Format.FMT_RGBA8888)
gesture_learning_btn_section = image.load("./assets/images/gesture_learning_btn_section.png", image.Format.FMT_RGBA8888)
gesture_learning_active_btn = image.load("./assets/images/gesture_learning_active_btn.png", image.Format.FMT_RGBA8888)
back_btn_disp_pos = (0, 0, 72, 72)  # 返回按钮在显示板中的位置
display_btn_pos = (0, 0, 72, 72)  # 显示按钮在显示板中的位置
recongnition_btn_disp_pos = (98, 18, gesture_recognition_btn_section.width(), gesture_recognition_btn_section.height())  # 检测按钮在显示板中的位置
recongnition_btn_disp_pos1 = (98, 18, gesture_recognition_btn_section.width(), gesture_recognition_btn_section.height())  # 检测按钮1在显示板中的位置
learn_btn_disp_pos = (373, 18, gesture_learning_btn_section.width(), gesture_learning_btn_section.height())  # 学习按钮在显示板中的位置
gesture_learning_active_btn_xywh = (326, 18, gesture_learning_active_btn.width(), gesture_learning_active_btn.height())  # 学习按钮在显示板中的位置
btn_need_flash = False  # 是否需要闪烁
center_hand_class_id = -1
want_del_hand_class_id = -1
screen_center_x = disp.width() // 2 # 屏幕中间的准心位置 x
screen_center_y = disp.height() // 2 # 屏幕中间的准心位置 y
obj_title_h = 30
pressed_already = False
last_x = 0
last_y = 0
last_pressed = False

# 初始化摄像头和显示屏
RESOLUTION = (640, 480)
cam = camera.Camera(640, 480, image.Format.FMT_RGB888)  # 摄像头采集 RGB888，read 后 to_format 为 RGBA8888 供绘制与检测
cam_min = cam.add_channel(320, 240)
scale_x = cam.width() / cam_min.width()
scale_y = cam.height() / cam_min.height()
cam_flip_init = False
boot_log(
    "camera module",
    "cam %sx%s cam_min %sx%s RGB888 (RGBA after read)"
    % (cam.width(), cam.height(), cam_min.width(), cam_min.height()),
)

def set_camera_flip(state):
    global cam_flip_init
    cam_min.vflip(state)
    # cam_min.hmirror(state)
    cam.vflip(state)
    # cam.hmirror(state)
    cam_flip_init = True


def wait_camera_flip_init():
    n = 0
    while not cam_flip_init:
        try:
            send_socket_message("global_get_camera_flip_status")
        except Exception as e:
            boot_log("wait_camera_flip_init: send failed", repr(e))
            traceback.print_exc()
        n += 1
        if n == 1 or n % 20 == 0:
            boot_log(
                "wait_camera_flip_init",
                "polling flip status, cam_flip_init=%s (n=%s)" % (cam_flip_init, n),
            )
        time.sleep_ms(250)
    boot_log("wait_camera_flip_init", "cam_flip_init done, stop polling")


# Unix 域套接字路径
socket_path = "/tmp/my_socket"

client_socket = None
client_socket_status = False

socket_rx_string_buffer = ''

mk_objs_info_list = []

def clamp(x, lo, hi):
    return max(lo, min(x, hi))


def clamp_coords(coords, max_width=640, max_height=480):
    x, y, w, h = coords

    w = clamp(w, 1, max_width)
    h = clamp(h, 1, max_height)

    x = clamp(x, 0, max_width - w)
    y = clamp(y, 0, max_height - h)

    return [x, y, w, h]

def handler_socket_message(message):
    global main_text_buffer, last_main_text_buffer, first_text_at, mk_objs_info_list
    cmds = message.split("\n")

    print(cmds)

    if cmds[0] == "global_set_camera_flip_status":
        value = cmds[1]
        if value == "1":
            set_camera_flip(1)
        else:
            set_camera_flip(0)
    elif cmds[0] == 'get_gesture_info':
        with mk_objs_info_list_lock: # mk_objs_info_list_lock 避免遍历的途中发送数据
            hand_index = None
            key = int(cmds[1])
            key_reply = int(cmds[2])
            reply_data = None
            min_distances: dict = {}

            if type(key) == int:
                for i,obj_info in enumerate(mk_objs_info_list):
                    if key == 1:
                        min_distance_to_center = sys.maxsize
                        if min_distance_to_center > (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2:
                            min_distance_to_center = (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2
                            hand_index = i
                            min_distances[i] = min_distance_to_center
                    elif key == 2:
                        if hand_index is None or obj_info["coords"][0] < mk_objs_info_list[hand_index]["coords"][0]:
                            hand_index = i
                    elif key == 3:
                        if hand_index is None or (obj_info["coords"][0] + obj_info["coords"][2]) > (mk_objs_info_list[hand_index]["coords"][0] + mk_objs_info_list[hand_index]["coords"][2]):
                            hand_index = i
                    elif key == 4:
                        if hand_index is None or obj_info["coords"][1] < mk_objs_info_list[hand_index]["coords"][1]:
                            hand_index = i
                    elif key == 5:
                        if hand_index is None or (obj_info["coords"][1] + obj_info["coords"][3]) > (mk_objs_info_list[hand_index]["coords"][1] + mk_objs_info_list[hand_index]["coords"][3]):
                            hand_index = i
                    elif key == 6:
                        max_conf = 0
                        if max_conf < obj_info["pred_conf"]:
                            max_conf = obj_info["pred_conf"]
                            hand_index = i
                    elif key == 7:
                        min_conf = 1
                        if min_conf > obj_info["pred_conf"]:
                            min_conf = obj_info["pred_conf"]
                            hand_index = i

                print("INdex", hand_index)
                
                if hand_index is not None:
                    if key == 1:
                        hand_index = min(min_distances.items(), key=lambda x: x[1])[0] if min_distances else hand_index
                    print('mk_objs_info_list[hand_index]', mk_objs_info_list[hand_index])

                    obj_info = mk_objs_info_list[hand_index]
                    if key_reply == 1:
                        reply_data = f"{obj_info['coords'][0]+obj_info['coords'][2]//2}"
                    elif key_reply == 2:
                        reply_data = f"{obj_info['coords'][1]+obj_info['coords'][3]//2}"
                    elif key_reply == 3:
                        reply_data = f"{obj_info['coords'][2]}"
                    elif key_reply == 4:
                        reply_data = f"{obj_info['coords'][3]}"
                    elif key_reply == 6:
                        # reply_data = f"{name_classes[obj_info['class_idx']]}"
                        pass
                    elif key_reply == 5:
                        reply_data = int(obj_info['pred_conf'] * 100)
                    cmd_app_json_send = {"cmd":0x2F, "data":reply_data}
                    print(cmd_app_json_send)
                    send_socket_message(f'get_gesture_info\nfull\n{reply_data}')
                else:
                    send_socket_message(f'get_gesture_info\nempty')
    elif cmds[0] == 'get_gesture_info_by_type':
        with mk_objs_info_list_lock: # mk_objs_info_list_lock 避免遍历的途中发送数据
            key_name_index = int(cmds[1]) - 1
            key_reply = int(cmds[2])
            hand_index = None
            reply_data = None
            min_distance_to_center = sys.maxsize

            for i,obj_info in enumerate(mk_objs_info_list):
                if obj_info["class_idx"] == key_name_index:
                    if min_distance_to_center > (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2:
                        min_distance_to_center = (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2
                        hand_index = i
            if hand_index is not None:
                obj_info = mk_objs_info_list[hand_index]
                if key_reply == 1:
                    reply_data = f"{obj_info['coords'][0]+obj_info['coords'][2]//2}"
                elif key_reply == 2:
                    reply_data = f"{obj_info['coords'][1]+obj_info['coords'][3]//2}"
                elif key_reply == 3:
                    reply_data = f"{obj_info['coords'][2]}"
                elif key_reply == 4:
                    reply_data = f"{obj_info['coords'][3]}"
                elif key_reply == 6:
                    pass
                    # reply_data = f"{name_classes[obj_info['class_idx']]}"
                elif key_reply == 5:
                    reply_data = int(obj_info['pred_conf'] * 100)
                cmd_app_json_send = {"cmd":0x30, "data":reply_data}
                send_socket_message(f'get_gesture_info_by_type\nfull\n{reply_data}')
            else:
                send_socket_message(f'get_gesture_info_by_type\nempty')
    elif cmds[0] == 'get_gesture_info_by_point':
        with mk_objs_info_list_lock: # mk_objs_info_list_lock 避免遍历的途中发送数据
            key = int(cmds[1])
            # key_point_id = int(cmds[2]) - 1
            key_point_id = int(cmds[2])
            key_reply = int(cmds[3])
            reply_data = None
            hand_index = None
            min_distances: dict = {}

            for i,obj_info in enumerate(mk_objs_info_list):
                if key == 1:
                    min_distance_to_center = sys.maxsize
                    if min_distance_to_center > (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2:
                        min_distance_to_center = (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2
                        hand_index = i
                        min_distances[i] = min_distance_to_center
                elif key == 2:
                    if hand_index is None or obj_info["coords"][0] < mk_objs_info_list[hand_index]["coords"][0]:
                        hand_index = i
                elif key == 3:
                    if hand_index is None or (obj_info["coords"][0] + obj_info["coords"][2]) > (mk_objs_info_list[hand_index]["coords"][0] + mk_objs_info_list[hand_index]["coords"][2]):
                        hand_index = i
                elif key == 4:
                    if hand_index is None or obj_info["coords"][1] < mk_objs_info_list[hand_index]["coords"][1]:
                        hand_index = i
                elif key == 5:
                    if hand_index is None or (obj_info["coords"][1] + obj_info["coords"][3]) > (mk_objs_info_list[hand_index]["coords"][1] + mk_objs_info_list[hand_index]["coords"][3]):
                        hand_index = i
                elif key == 6:
                    max_conf = 0
                    if max_conf < obj_info["pred_conf"]:
                        max_conf = obj_info["pred_conf"]
                        hand_index = i
                elif key == 7:
                    min_conf = 1
                    if min_conf > obj_info["pred_conf"]:
                        min_conf = obj_info["pred_conf"]
                        hand_index = i

            if hand_index is not None:
                if key == 1:
                    hand_index = min(min_distances.items(), key=lambda x: x[1])[0] if min_distances else hand_index

                obj_info = mk_objs_info_list[hand_index]
                if key_reply == 1:
                    reply_data = f'{obj_info["points"][key_point_id*3+8]}'
                elif key_reply == 2:
                    reply_data = f'{obj_info["points"][key_point_id*3+9]}'
                
                cmd_app_json_send = {"cmd":0x31, "data":reply_data}
                print(cmd_app_json_send)
                send_socket_message(f'get_gesture_info_by_point\nfull\n{reply_data}')
            else:
                print(None)
                send_socket_message(f'get_gesture_info_by_point\nempty')
    elif cmds[0] == 'get_gesture_info_by_type_point':
        with mk_objs_info_list_lock: # mk_objs_info_list_lock 避免遍历的途中发送数据
            key_name_index = int(cmds[1]) - 1
            # key_point_id = int(cmds[2]) - 1
            key_point_id = int(cmds[2])
            key_reply = int(cmds[3])
            reply_data = None
            hand_index = None
            min_distance_to_center = sys.maxsize
            
            for i,obj_info in enumerate(mk_objs_info_list):
                if obj_info["class_idx"] == key_name_index:
                    if min_distance_to_center > (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2:
                        min_distance_to_center = (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2
                        hand_index = i

            if hand_index is not None:
                obj_info = mk_objs_info_list[hand_index]
                if key_reply == 1:
                    reply_data = f'{obj_info["points"][key_point_id*3+8]}'
                elif key_reply == 2:
                    reply_data = f'{obj_info["points"][key_point_id*3+9]}'
                    print(f"================={obj_info}, point is {key_point_id*3+9}")
                cmd_app_json_send = {"cmd":0x32, "data":reply_data}
                print(reply_data)
                send_socket_message(f'get_gesture_info_by_type_point\nfull\n{reply_data}')
            else:
                send_socket_message(f'get_gesture_info_by_type_point\nempty')
    elif cmds[0] == 'get_gesture_type':
        with mk_objs_info_list_lock: # mk_objs_info_list_lock 避免遍历的途中发送数据
            key = int(cmds[1])
            reply_data = None
            hand_index = None
            min_distances: dict = {}

            for i,obj_info in enumerate(mk_objs_info_list):
                if key == 1:
                    min_distance_to_center = sys.maxsize
                    if min_distance_to_center > (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][2]//2 - 480//2)**2:
                        min_distance_to_center = (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][2]//2 - 480//2)**2
                        hand_index = i
                        min_distances[i] = min_distance_to_center
                elif key == 2:
                    if hand_index is None or obj_info["coords"][0] < mk_objs_info_list[hand_index]["coords"][0]:
                        hand_index = i
                elif key == 3:
                    if hand_index is None or (obj_info["coords"][0] + obj_info["coords"][2]) > (mk_objs_info_list[hand_index]["coords"][0] + mk_objs_info_list[hand_index]["coords"][2]):
                        hand_index = i
                elif key == 4:
                    if hand_index is None or obj_info["coords"][1] < mk_objs_info_list[hand_index]["coords"][1]:
                        hand_index = i
                elif key == 5:
                    if hand_index is None or (obj_info["coords"][1] + obj_info["coords"][3]) > (mk_objs_info_list[hand_index]["coords"][1] + mk_objs_info_list[hand_index]["coords"][3]):
                        hand_index = i
                elif key == 6:
                    max_conf = 0
                    if max_conf < obj_info["pred_conf"]:
                        max_conf = obj_info["pred_conf"]
                        hand_index = i
                elif key == 7:
                    min_conf = 1
                    if min_conf > obj_info["pred_conf"]:
                        min_conf = obj_info["pred_conf"]
                        hand_index = i
            if hand_index is not None:
                if key == 1:
                    hand_index = min(min_distances.items(), key=lambda x: x[1])[0] if min_distances else hand_index
                obj_info = mk_objs_info_list[hand_index]
                
                print(obj_info)

                class_name = None

                # 如果是检测模式
                if rec_display == 0:
                    if obj_info['class_idx'] == 0:
                        class_name = 'One'
                    elif obj_info['class_idx'] == 1:
                        class_name = 'Five'
                    elif obj_info['class_idx'] == 2:
                        class_name = 'Fist'
                    elif obj_info['class_idx'] == 3:
                        class_name = 'OK'
                    elif obj_info['class_idx'] == 4:
                        class_name = 'Hand heart'
                    elif obj_info['class_idx'] == 5:
                        class_name = 'Two'
                    elif obj_info['class_idx'] == 6:
                        class_name = 'Three'
                    elif obj_info['class_idx'] == 7:
                        class_name = 'Four'
                    elif obj_info['class_idx'] == 8:
                        class_name = 'Six'
                    elif obj_info['class_idx'] == 9:
                        class_name = 'ILY'
                    elif obj_info['class_idx'] == 10:
                        class_name = 'Gun'
                    elif obj_info['class_idx'] == 11:
                        class_name = 'Thumb up'
                else:
                    gesture_name = name_classes[obj_info["class_idx"]] # 获取手势名称
                    class_name = learned_gestures[gesture_name]

                if class_name:
                    send_socket_message(f'get_gesture_type\nfull\n{class_name}')
                else:
                    send_socket_message(f'get_gesture_type\nempty')
            else:
                send_socket_message(f'get_gesture_type\nempty')

def send_socket_message(message):
    try:
        message_packet = f"@#{message}#@"
        print("[发送成功]\n", message_packet)
        if client_socket:
            client_socket.sendall(message_packet.encode())
    except Exception as e:
        boot_log("send_socket_message: error", repr(e))
        traceback.print_exc()


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
                                boot_log("socket global_debug: set_debug_info failed", repr(e))
                                traceback.print_exc()
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

accept_thread2 = threading.Thread(target=wait_camera_flip_init)
accept_thread2.daemon = True
accept_thread2.start()
boot_log("threads", "camera_flip poller started after send_socket_message is defined")


def is_in_button(x, y, btn_pos):
    return (x > btn_pos[0] and x < btn_pos[0] + btn_pos[2] and
            y > btn_pos[1] and y < btn_pos[1] + btn_pos[3])

#全局变量
rec_btn_pos = (0, 0, 0, 0)  # 识别按钮位置
learn_btn_pos = (0, 0, 0, 0)  # 学习按钮位置
rec_btn_pos1 = (0, 0, 0, 0)  # 识别按钮1位置
learn_btn_pos1 = (0, 0, 0, 0)  # 学习按钮1位置
back_btn_pos = (0, 0, 0, 0)  # 返回按钮位置
recongnition_btn_pos = (0, 0, 0, 0)  # 检测按钮位置
recongnition_btn_pos1 = (0, 0, 0, 0)  # 检测按钮1位置

def find_obj_id(mk_info_dict):
    if len(mk_objs_info_list) == 0:
        mk_objs_info_list.append(mk_info_dict)
        return
    min_distance = sys.maxsize
    min_id = 0
    min_index = -1  # 用于存储最小距离的对象索引
    hand_ids = []
    for index, old_obj in enumerate(mk_objs_info_list):
        # print(f"old_obj:{old_obj}") #id1 true
        if old_obj["new"] == False:
            distance = (mk_info_dict["coords"][0] - old_obj["coords"][0])**2 + (mk_info_dict["coords"][1] - old_obj["coords"][1])**2
            if distance < min_distance:
                min_distance = distance
                min_id = old_obj["id"]
                min_index = index  # 记录最小距离对象的索引

    if min_id != 0:
        mk_objs_info_list.pop(min_index)  # 使用索引删除对象
        mk_info_dict["id"] = min_id
        mk_objs_info_list.append(mk_info_dict)
        return
    
    """
    找到中连续的最大 Hand ID。
    """
    for obj_info in mk_objs_info_list:
        if obj_info["new"]:
            hand_ids.append(obj_info["id"])

    if not hand_ids:
        return 0

    # 排序 Hand ID
    hand_ids.sort()
    # 找到连续的最大值
    max_continue_id = hand_ids[0]
    for i in range(1, len(hand_ids)):
        if hand_ids[i] == hand_ids[i - 1] + 1:
            max_continue_id = hand_ids[i]
        else:
            break
    mk_info_dict["id"] = max_continue_id + 1
    mk_objs_info_list.append(mk_info_dict)
    return


def draw_btns(img:image.Image):
    if rec_display == 0:
        img.draw_image(recongnition_btn_disp_pos[0], recongnition_btn_disp_pos[1], gesture_recognition_active_btn)
        img.draw_image(learn_btn_disp_pos[0], learn_btn_disp_pos[1], gesture_learning_btn_section)
    elif rec_display == 1:
        img.draw_image(recongnition_btn_disp_pos[0], recongnition_btn_disp_pos[1], gesture_recognition_btn_section)
        img.draw_image(gesture_learning_active_btn_xywh[0], gesture_learning_active_btn_xywh[1], gesture_learning_active_btn)

    img.draw_image(back_btn_pos[0], back_btn_pos[1], back_btn)

def adjustment_ratio(img_max, img_min, x, y, w = -1, h = -1):
    return image.resize_map_pos_reverse(img_max.width(), img_max.height(), img_min.width(), img_min.height(), image.Fit.FIT_CONTAIN, x, y, w, h)

last_tick = time.time()
delete_popup = DeletePopup(disp) # 删除确认弹窗
show_delete_popup = False

def app_main(disp):
    
    global rec_btn_pos, learn_btn_pos, rec_btn_pos1, learn_btn_pos1, back_btn_pos, recongnition_btn_pos, recongnition_btn_pos1
    global rec_display, pressed_flag
    global display_btn_pos, back_btn_disp_pos, recongnition_btn_disp_pos, recongnition_btn_disp_pos1
    global back,learn
    global mk_objs_info_list,name_classes,learned_gestures
    global last_tick, show_loading, display_show_lock, pressed_already, last_pressed, last_x, last_y
    global cam, cam_min, show_delete_popup, center_hand_class_id, want_del_hand_class_id, objs                                    # [649: 在屏幕上显示这张图]  

    boot_log("app_main", "enter")
    # 创建手部关键点检测器，加载模型
    detector = nn.HandLandmarks(model="/root/models/hand_landmarks.mud")
    boot_log("app_main", "HandLandmarks detector ready")
    # detector = nn.HandLandmarks(model="/root/models/hand_landmarks_bf16.mud")  # 可选的BF16模型
    landmarks_rel = False  # 是否关联手部关键点

    # cam = camera.Camera(320,224)  # 创建摄像头对象
    # 初始化触摸屏
    ts = touchscreen.TouchScreen()
    boot_log("app_main", "TouchScreen ready")

    # 定义按钮文案和图标
    recongnition_str_size = image.string_size("Gesture Recongnition", scale=1)  # [684: 检测按钮的文字尺寸]
    learning_str_size = image.string_size("Gesture Learning", scale=1)    # [685: 学习按钮的文字尺寸]
    recongnition_str1_size = image.string_size("ection", scale=1)  # [689: 另一种检测按钮的文字尺寸]
    learning_str1_size = image.string_size("ture Learning", scale=1)    # [690: 学习按钮1的文字尺寸]
    
    exit_str = ' < '  # 返回按钮文案
    _display_img = image.load("./assets/images/display.png", image.Format.FMT_RGBA8888)  # 加载显示图标
    display_img = _display_img.resize(_display_img.width(), _display_img.height(), image.Fit.FIT_CONTAIN)

    _undisplay_img = image.load("./assets/images/undisplay.png", image.Format.FMT_RGBA8888)  # 加载不显示图标
    undisplay_img = _undisplay_img.resize(_display_img.width(), _display_img.height(), image.Fit.FIT_CONTAIN)

    _learning_img = image.load("./assets/images/learning.png", image.Format.FMT_RGBA8888)  # 加载学习图标
    learning_img = _learning_img.resize(_learning_img.width(), _learning_img.height(), image.Fit.FIT_CONTAIN)

    _forget_img = image.load("./assets/images/forget.png", image.Format.FMT_RGBA8888)  # 加载清除图标
    forget_img = _forget_img.resize(_forget_img.width(), _forget_img.height(), image.Fit.FIT_CONTAIN)
    
    # 初始化按钮和图形的位置
    back_btn_pos = (6, 6, 72, 72)                   # [683: 返回按钮的位置定义]
    recongnition_btn_pos = (110, 18, recongnition_str_size[0], 29)  # [684: 检测按钮的位置]
    rec_btn_pos = (recongnition_btn_pos[0] - 20, 18, recongnition_btn_pos[2] + 40, 48)  # [685: 识别按钮位置]
    learn_btn_pos = (rec_btn_pos[0] + rec_btn_pos[2] + 36, 18, learning_str_size[0], 29)  # [686: 学习按钮位置]

    recongnition_btn_pos1 = (110, 18, recongnition_str1_size[0], 29)  # [689: 另一种检测按钮尺寸配置]
    learn_btn_pos1 = (recongnition_btn_pos1[0] + recongnition_btn_pos1[2] + 36, 18, learning_str_size[0], 29)
                                                           # [690: 学习按钮1位置]
    rec_btn_pos1 = (learn_btn_pos1[0] - 20, 18 - 10, learn_btn_pos1[2] + 40, 48)
                                                           # [691: 识别按钮1位置]

    learn_btn_pos2 = (110, 18, learning_str1_size[0], 29)  # [694: 学习按钮2位置]

    display_btn_pos = image.resize_map_pos(cam.width(), cam.height(), disp.width(), disp.height(),
                                           image.Fit.FIT_CONTAIN, cam.width() - display_img.width(), 0, display_img.width(), display_img.height())
                                                           # [699: 按适配规则在画面上定位显示按钮的位置]
    back_btn_disp_pos = image.resize_map_pos(cam.width(), cam.height(), disp.width(), disp.height(),
                                             image.Fit.FIT_CONTAIN, back_btn_pos[0], back_btn_pos[1], back_btn_pos[2], back_btn_pos[3])
                                                           # [701: 返回按钮在显示板中的映射位置]
    boot_log(
        "app_main",
        "UI layout maps ok, disp %sx%s" % (disp.width(), disp.height()),
    )

    # 导入必要的模块
    import numpy as np

    from LinearSVC import LinearSVC, LinearSVCManager  # 导入线性支持向量分类器及其管理器

    # 定义一个计时器上下文管理器
    from contextlib import contextmanager
    @contextmanager
    def timer(name):
        import time
        result = {'passed': 0.}  # 使用字典存储结果
        start = time.time()
        yield result
        end = time.time()
        passed = end - start
        result['passed'] = passed
        print(f"{name} 耗时: {passed:.6f} 秒")  # 打印计时结果

    print("hello")
    boot_log("app_main", "before trainSets.npz / fine2.npz")
    # 定义类别名称元组和上一次训练时间
    name_classes = ("One", "Five", "Fist", "OK", "Hand heart", "Two", "Three", "Four", "Six", "ILY", "Gun", "Thumb up")# "nine", "pink")
    learned_gestures = {name: "" for name in name_classes}
    last_train_time = 0

    # 加载训练集
    boot_log("app_main", "np.load(trainSets.npz) ...")
    npzfile = np.load("trainSets.npz")  # 加载NPZ文件
    X_train = npzfile["X"]  # 获取训练特征
    y_train = npzfile["y"]  # 获取训练标签
    assert(len(X_train) == len(y_train))  # 确保特征和标签长度一致
    print(f"X_train_len: {len(X_train)}")  # 打印训练集长度
    mask_gt_4_lt_12 = (y_train >= 4) & (y_train < 12)
    # 训练或加载线性SVC模型
    # if 0:  # 如果为真，则加载预训练模型
    with timer("加载") as r:  # 使用计时器
        clfm = LinearSVCManager(LinearSVC.load("fine2.npz"), X_train[mask_gt_4_lt_12], y_train[mask_gt_4_lt_12], pretrained=True)  # 加载模型管理器
    last_train_time = r['passed']  # 记录加载时间
    # else:  # 否则，训练模型
        # 创建掩码（mask），用于区分训练集的前半部分和后半部分a
    # mask_lt_4 = y_train < 4   # 小于 4 的掩码
    # mask_ge_4 = y_train >= 4  # 大于等于 4 的掩码
  
    # # with timer("训练前半部分") as r:  # 使用计时器a
    #     # clfm = LinearSVCManager(LinearSVC(C=1.0, learning_rate=0.01, max_iter=100), X_train[mask_lt_4], y_train[mask_lt_4])  # 训练线性SVC模型
    # # last_train_time = r['passed']  # 记录训练时间
    # indices_ge_4 = np.where(mask_ge_4)[0]
    # clfm.rm(indices_ge_4)
                        
    # with timer("训练后半部分") as r:  # 训练后半部分代码被注释掉了
    #     clfm.add(X_train[mask_gt_4_lt_12], y_train[ mask_gt_4_lt_12])
    # last_train_time = r['passed']
    
    

    # 测试模型回归性能
    with timer("回归") as r:  # 使用计时器
        labels, confs = clfm.test(clfm.samples[0])  # 使用第一个样本测试模型
        recall_count = len(clfm.samples[1])  # 获取样本标签长度
        right_count = np.sum(labels == clfm.samples[1])  # 计算预测正确的样本数量
        print(f"right/all= {right_count}/{recall_count}, acc: {right_count/recall_count}")  # 打印预测准确率

    # 打印模型类型
    print(type(clfm.clf))
    # clf1 = LinearSVC.load("clf_dump.npz")  # 加载模型的代码被注释掉了
    # print(f"{clfm.clf._W-clf1._W}, {clfm.clf._B-clf1._B}")  # 比较模型参数的代码被注释掉了
    clfm.clf.save("/mk/fine2.npz")  # 保存模型
    boot_log("app_main", "SVC load/regression/save done, about to first cam.read (blocks here if VI/driver stuck)")

    # 定义预处理函数，将手部关键点转换为特征向量
    def preprocess(hand_landmarks, is_left=False, boundary=(1,1,1)):
        hand_landmarks = np.array(hand_landmarks).reshape((21, -1))  # 将关键点数据转换为21x3的数组
        vector = hand_landmarks[:,:2]  # 获取关键点的x和y坐标
        vector = vector[1:] - vector[0]  # 计算关键点相对于第一个点的向量
        vector = vector.astype('float64') / boundary[:vector.shape[1]]  # 归一化向量
        if not is_left:  # 如果不是左手，则镜像处理
            vector[:,0] *= -1  # 反转x坐标
        return vector  # 返回处理后的特征向量

    # 主循环
    class_nums_changing = False  # 类别数量是否正在更改
    boot_log("app_main", "pre-loop: cam.read() + to_format(RGBA8888) ...")
    try:
        img = cam.read().to_format(image.Format.FMT_RGBA8888)  # 读取摄像头图像
        img_min = cam_min.read().to_format(image.Format.FMT_RGBA8888)  # 读取摄像头图像
    except Exception as e:
        boot_log("app_main: pre-loop cam.read FAILED", repr(e))
        traceback.print_exc()
        raise
    try:
        _fmt = img.format()
    except Exception:
        _fmt = "?"
    boot_log(
        "app_main",
        "pre-loop first frame ok img %sx%s fmt=%s" % (img.width(), img.height(), _fmt),
    )
    while_tick = 0
    first_disp_done = False
    _main_loop_i = 0
    # img = img.copy() # 创建一个RGB565格式的图像
    objs = []
    objs_info_list = []
    back = False

    mk_info_dict = {
        "id": 0,
        "pred_conf": 0.0,
        "class_idx": 0,
        "coords": [0, 0, 0, 0],
        "points": [],
        "new": False
    }
    
    mk_objs_info_list = []
    
    while not app.need_exit():  # 循环直到应用程序需要退出
        if _main_loop_i == 0:
            boot_log(
                "main_loop",
                "entered while; show_loading clears on disp.show this iteration",
            )

        loop_start_time = time.ticks_ms()

        if int(time.time()) - last_tick > 5:
            send_socket_message('PASS')
            last_tick = time.time()

        # 读取触摸屏输入
        touch_x, touch_y, pressed = ts.read()
        
        img = cam.read().to_format(image.Format.FMT_RGBA8888)  # 读取摄像头图像
        img_min = cam_min.read().to_format(image.Format.FMT_RGBA8888)  # 读取摄像头图像
        # img = img.copy().to_format(image.Format.FMT_RGBA8888)  # 创建一个RGB565格式的图像
        hands_index = -1  # 手部数量
        center_hand_index = -1  # 中心手部索引
        center_hand_class_id = -1  # 中心手部类别
        # 检测图像中的手部关键点
        # objs = []
        # for obj_info in mk_objs_info_list:
        #     print(f"id:{obj_info['id']} pred_conf:{obj_info['pred_conf']}")
        #     if obj_info["new"]:
        #         obj_info["new"] = False
            # print(f"obj_info:{obj_info}") id2 false
        _mk_objs_info_list = []
        draw_btns(img)  # 绘制按钮
        if while_tick % 2 == 0:
            objs = detector.detect(img_min, conf_th = 0.7, iou_th = 0.45, conf_th2 = 0.8, landmarks_rel = landmarks_rel)
            objs_info_list = []        
        min_distance = sys.maxsize  # 最小距离

        with mk_objs_info_list_lock:
            for obj in objs:  # 遍历检测到的手部对象
                hands_index += 1  # 计数手部数量
                time_start = time.time_us()  # 记录开始时间
                x, y, w, h = adjustment_ratio(img, img_min, clamp(obj.x, 0, 640), clamp(obj.y, 0, 480), clamp(obj.w, 0, 640), clamp(obj.h, 0, 480))  # 调整手部检测框的位置和尺寸
                
                if while_tick % 2 == 0:
                    hand_landmarks = preprocess(obj.points[8:8+21*3], obj.class_id == 0, (img_min.width(), img_min.height(), 1))  # 预处理手部关键点
                    features = np.array([hand_landmarks.flatten()])  # 将特征展平为一维数组
                    class_idx, pred_conf = clfm.test(features)  # 使用模型进行预测
                    objs_info_list.append((class_idx,pred_conf))
                else:
                    class_idx, pred_conf = objs_info_list[hands_index]
                    
                time_predict = time.time_us()  # 记录预测结束时间
                
                class_idx, pred_conf = class_idx[0], pred_conf[0]  # 提取预测类别和置信度
                if class_idx == 12:
                    class_idx = 10
                if class_idx == 13:
                    continue
                
                gesture_name = name_classes[class_idx] # 获取手势名称
                _color = image.Color.from_rgb(9, 232, 50) if (rec_display == 1 and learned_gestures[gesture_name] != "") else image.COLOR_WHITE #根据是否学习过手势设置颜色
                scaled_points = [point * 2 for point in obj.points]  # 缩放关键点坐标
                
                mk_info_dict["pred_conf"] = pred_conf
                mk_info_dict["class_idx"] = class_idx
                mk_info_dict["coords"] = clamp_coords([x, y, w, h])
                mk_info_dict["points"] = scaled_points
                #print(f"mk_info:{mk_objs_info_list}") 输出mk_objs_info_list[成员]“new”为false
                # mk_info_dict["new"] = True
                #print(f"mk_info:{mk_objs_info_list}") 输出mk_objs_info_list[成员]“new”为true?
                # mk_info_dict["id"] = 1
                # find_obj_id(mk_info_dict.copy())
                _mk_objs_info_list.append(mk_info_dict.copy())
                
                # print(f'---------dir(obj):{dir(obj)}-----------')  # 打印对象的所有属性和方法
                # for attr in dir(obj):
                #     if not attr.startswith('__'):
                #         value = getattr(obj, attr)
                #         print(f"--------{attr}: {value}-----------")

                # 在图像上绘制检测结果
                # img.draw_rect(obj.x, obj.y, obj.w, obj.h, color = _color,thickness=2)  # 绘制手部检测框的代码被注释掉了
                img.draw_rect(x, y, w, h, _color, thickness = 2)  # 绘制手部检测框

                if rec_display == 0:  # 如果是检测模式
                    msg = f'{name_classes[class_idx]} {pred_conf:.2f}'  # 构建显示信息
                    msg_size = image.string_size(msg, scale=1.0, thickness=1)  # 计算msg的尺寸
                    padding = 10
                    # 绘制比msg稍大的矩形
                    # img.draw_rect(obj.x, obj.y - 24 - padding, msg_size[0] + 2 * padding, msg_size[1] + 2 * padding, color=image.COLOR_WHITE, thickness=-1)
                    # img.draw_string(obj.x + padding, obj.y - 24, msg, color = image.COLOR_BLACK, scale = 0.8, thickness = -1)  # 在图像上绘制信息
                    
                    #限制信息框的位置，不超出边界
                    x_info = x
                    y_info = y
                    if x < 0:
                        x_info = 0
                    if y - 24 - padding < 0:
                        y_info = 24 + padding
                        
                    img.draw_rect(x_info, y_info - obj_title_h, msg_size[0] + 2 * padding, obj_title_h, image.COLOR_WHITE, thickness=-1)
                    img.draw_string(x_info + padding, y_info - obj_title_h // 2 - msg_size[1] // 2, msg, image.COLOR_BLACK, scale = 1, thickness = -1)  # 在图像上绘制信息
                    # msg = f'{detector.labels[obj.class_id]}: {obj.score:.2f}\n{name_classes[class_idx]}({class_idx})={pred_conf*100:.2f}%\n{time_predict-time_start}us'  # 构建显示信息
                    if pressed_flag[3]: # 如果是显示模式则显示关键点
                        detector.draw_hand(img, obj.class_id, scaled_points, 4, 10, box=False)  # 绘制手部关键点
                        #将前8个元素切掉
                        scaled_points_tmp = scaled_points[8:]
                        #在各关键点上画出相应的数字
                        # for i in range(21):
                        #     x = scaled_points_tmp[i*3]
                        #     y = scaled_points_tmp[i*3+1]
                        #     img.draw_string(x, y, str(i), color=image.COLOR_YELLOW, scale=0.5, thickness=1)
                        # 通过直线连接相邻关键点
                        x1 = scaled_points_tmp[0*3]
                        y1 = scaled_points_tmp[0*3+1]
                        x2 = scaled_points_tmp[5*3]
                        y2 = scaled_points_tmp[5*3+1]
                        x3 = scaled_points_tmp[9*3]
                        y3 = scaled_points_tmp[9*3+1]
                        x4 = scaled_points_tmp[13*3]
                        y4 = scaled_points_tmp[13*3+1]
                        x5 = scaled_points_tmp[17*3]
                        y5 = scaled_points_tmp[17*3+1]
                        img.draw_line(x1, y1, x2, y2, color=image.COLOR_WHITE, thickness=2)
                        img.draw_line(x2, y2, x3, y3, color=image.COLOR_WHITE, thickness=2)
                        img.draw_line(x3, y3, x4, y4, color=image.COLOR_WHITE, thickness=2)
                        img.draw_line(x4, y4, x5, y5, color=image.COLOR_WHITE, thickness=2)
                        img.draw_line(x5, y5, x1, y1, color=image.COLOR_WHITE, thickness=2)   
                        for i in range(20):
                            if i % 4 == 0 and i != 0:
                                continue
                            x1 = scaled_points_tmp[i*3]
                            y1 = scaled_points_tmp[i*3+1]
                            x2 = scaled_points_tmp[(i+1)*3]
                            y2 = scaled_points_tmp[(i+1)*3+1]
                            img.draw_line(x1, y1, x2, y2, color=image.COLOR_WHITE, thickness=2)
                        
                        
                        
                        # if landmarks_rel:  # 如果显示关键点关系
                        #     img.draw_rect(0, 0, detector.input_width(detect=False), detector.input_height(detect=False), color = image.COLOR_YELLOW)  # 绘制关键点区域
                        #     for i in range(21):  # 遍历21个关键点
                        #         x = obj.points[8 + 21*3 + i * 2]  # 获取关键点x坐标
                        #         y = obj.points[8 + 21*3 + i * 2 + 1]  # 获取关键点y坐标
                        #         img.draw_circle(x, y, 3, color = image.COLOR_YELLOW)  # 绘制关键点
                elif rec_display == 1:    # 学习模式
                    #计算离中心最近的图像
                    # if min_distance > (obj.x - img.width()//2)**2 + (obj.y - img.height()//2)**2:
                    #     center_hand_index = hands_index
                    #     center_hand_class_id = class_idx
                    #     min_distance = (obj.x - img.width()//2)**2 + (obj.y - img.height()//2)**2
                    if min_distance > (x + w//2 - img.width()//2)**2 + (y + h//2 - img.height()//2)**2:
                        center_hand_index = hands_index
                        center_hand_class_id = class_idx
                        min_distance = (x + w//2 - img.width()//2)**2 + (y + h//2 - img.height()//2)**2
                        
                    if learned_gestures[gesture_name] != "":
                        msg = f'{learned_gestures[gesture_name]}'
                        msg_size = image.string_size(msg, scale=1.0, thickness=1)  # 计算msg的尺寸
                        padding = 10
                        # 绘制比msg稍大的矩形
                        # img.draw_rect(obj.x, obj.y - 24 - padding, msg_size[0] + 2 * padding, msg_size[1] + 2 * padding, color=image.COLOR_GREEN, thickness=-1)
                        # img.draw_string(obj.x + padding, obj.y - 24, msg, color = image.COLOR_WHITE, scale = 0.8, thickness = -1)  # 在图像上绘制信息
                        img.draw_rect(x, y - obj_title_h, msg_size[0] + 2 * padding, obj_title_h, image.Color.from_rgb(9, 232, 50), thickness=-1)
                        img.draw_string(x + padding, y - obj_title_h // 2 - msg_size[1] // 2, msg, image.COLOR_WHITE, scale = 1)  # 在图像上绘制信息

            mk_objs_info_list = _mk_objs_info_list

            center_hand = objs[center_hand_index] if center_hand_index != -1 else None

        # 删除 mk_objs_info_list 中 "new" 为 False 的元素
        # mk_objs_info_list = [obj_info for obj_info in mk_objs_info_list if obj_info["new"]]

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
                    learned_gestures[name_classes[want_del_hand_class_id]] = ""  # 清除手势ID
            else:
                if rec_display == 0:  # 在检测模式
                    # 如果点击后退 
                    if is_in_button(touch_x, touch_y, back_btn_disp_pos):
                        return
                    elif is_in_button(touch_x, touch_y, learn_btn_disp_pos):
                        if rec_display != 1:
                            pressed_flag[3] = False
                        rec_display = 1
                        print("learn btn click")
                    elif is_in_button(touch_x, touch_y, display_btn_pos):
                        # 翻转 pressed_flag[3]
                        pressed_flag[3] = not pressed_flag[3]
                        print("display btn click")
                    elif is_in_button(touch_x, touch_y, recongnition_btn_disp_pos):
                        if rec_display != 0:
                            pressed_flag[3] = False
                        rec_display = 0
                        print("recongnition btn click")

                elif rec_display == 1:  # 在学习模式
                    # 如果点击后退 
                    if is_in_button(touch_x, touch_y, back_btn_disp_pos):
                        return
                    elif is_in_button(touch_x, touch_y, gesture_learning_active_btn_xywh):
                        if rec_display != 1:
                            pressed_flag[3] = False
                        rec_display = 1
                        print("learn btn click")
                    # 如果点击的是删除
                    elif is_in_button(touch_x, touch_y, display_btn_pos) and learned_gestures[name_classes[center_hand_class_id]] != "":
                        want_del_hand_class_id = center_hand_class_id
                        show_delete_popup = True
                    # 如果点击的是学习
                    elif is_in_button(touch_x, touch_y, display_btn_pos) and learned_gestures[name_classes[center_hand_class_id]] == "":
                        learn = True
                    elif is_in_button(touch_x, touch_y, recongnition_btn_disp_pos1):
                        if rec_display != 0:
                            pressed_flag[3] = False
                        rec_display = 0
                        print("recongnition btn click")
                print(f"rec_display: {rec_display}, pressed_flag: {pressed_flag}")      

        def find_max_continue_hand_id(learned_gestures):
            """
            找到 learned_gestures 中连续的最大 Hand ID。
            """
            hand_ids = []
            # 提取所有 Hand ID
            for gesture_name, gesture_id in learned_gestures.items():
                if gesture_id.startswith("Hand ID"):
                    match = re.search(r'\d+', gesture_id)
                    if match:
                        hand_ids.append(int(match.group()))

            if not hand_ids:
                return 0

            # 排序 Hand ID
            hand_ids.sort()
            # 找到连续的最大值
            max_continue_id = hand_ids[0]
            for i in range(1, len(hand_ids)):
                if hand_ids[i] == hand_ids[i - 1] + 1:
                    max_continue_id = hand_ids[i]
                else:
                    break
            return max_continue_id

        if rec_display == 0:
            if pressed_flag[3]:
                img.draw_image(cam.width() - display_img.width() - 6, 6, display_img)
            else:
                img.draw_image(cam.width() - undisplay_img.width() - 6, 6, undisplay_img)  # 绘制显示按钮
        elif rec_display == 1 and center_hand is not None:
            if learned_gestures[name_classes[center_hand_class_id]] == "":
                img.draw_image(cam.width() - learning_img.width() - 6, 6, learning_img)
                if learn: # 如果按下学习按钮,则给手势命名记录
                    min_id = find_max_continue_hand_id(learned_gestures)  # 查找最大ID
                    learned_gestures[name_classes[center_hand_class_id]] = f"Hand ID{min_id+1}"  # 记录手势ID
            else:
                img.draw_image(cam.width() - forget_img.width() - 6, 6, forget_img)
                        
        learn = False  # 学习、清除、返回标志位重置

        if show_delete_popup:
            delete_popup.draw(img)
        
        with display_show_lock:
            show_loading = False
            disp.show(img) # 显示到屏幕
            if not first_disp_done:
                first_disp_done = True
                boot_log(
                    "display",
                    "first disp.show(); loading thread should exit next",
                )

        loop_end_time = time.ticks_ms()
        loop_execution_time = loop_end_time - loop_start_time
        # time_tracker.record_time(loop_execution_time)
        _main_loop_i += 1
        if _main_loop_i % 180 == 0:
            boot_log(
                "main_loop heartbeat",
                "iter=%s loop_ms=%s" % (_main_loop_i, loop_execution_time),
            )
        time.sleep_ms(5) # 休眠一些时间来释放一些CPU使用

        # # 获取当前模型的类别数量
        # current_n_classes = len(clfm.clf.classes)

        # # 定义获取颜色的lambda函数
        # get_color = lambda n: image.COLOR_GREEN if current_n_classes == n else image.COLOR_RED
        # # 在图像上绘制类别数量状态指示圆
        # img.draw_circle(300, 20, 30, color = get_color(14))
        # img.draw_string(300-22, 20-18, "class 14", color = get_color(14))
        # img.draw_circle(300, 224-1-20, 30, color = get_color(4))
        # img.draw_string(300-22, 224-1-20-18, "class 4", color = get_color(4))
        # # 读取触摸屏输入
        # x, y = 0, 0
        # x, y, preesed = ts.read()
        # x = int(x / disp.width() * img.width())  # 将触摸屏x坐标映射到图像坐标
        # y = int(y / disp.height() * img.height())  # 将触摸屏y坐标映射到图像坐标
        # if x >= 300-30:  # 如果触摸点在类别14的指示圆附近
        #     if y <= 20+30:  # 如果触摸点在类别14的指示圆上方
        #         if preesed:  # 如果触摸点被按下
        #             if not class_nums_changing and current_n_classes == 4:  # 如果类别数量正在更改且当前类别数量为4
        #                 class_nums_changing = True  # 设置类别数量正在更改
        #             if class_nums_changing:  # 如果类别数量正在更改
        #                 img.draw_string(30, 112, "Release to upgrade to class 14\n and please wait for Training be done.", color = image.COLOR_RED)  # 提示用户释放触摸点以升级类别数量
        #         else:  # 如果触摸点未被按下
        #             if class_nums_changing:  # 如果类别数量正在更改
        #                 class_nums_changing = False  # 重置类别数量更改标志
        #                 with timer("训练后半部分") as r:  # 使用计时器
        #                     mask_lt_4 = y_train < 4   # 小于 4 的掩码
        #                     mask_ge_4 = y_train >= 4  # 大于等于 4 的掩码
        #                     clfm.add(X_train[mask_ge_4], y_train[mask_ge_4])  # 添加后半部分样本到模型
        #                 last_train_time = r['passed']  # 记录训练时间
        #                 print("success changed to 14")  # 打印成功信息
        #     elif y >= 224-1-20-30:  # 如果触摸点在类别4的指示圆下方
        #         if preesed:  # 如果触摸点被按下
        #             if not class_nums_changing and current_n_classes == 14:  # 如果类别数量正在更改且当前类别数量为14
        #                 class_nums_changing = True  # 设置类别数量正在更改
        #             if class_nums_changing:  # 如果类别数量正在更改
        #                 img.draw_string(30, 112, "Release to retrain to class 4\n and please wait for Training be done.", color = image.COLOR_RED)  # 提示用户释放触摸点以重新训练类别数量
        #         else:  # 如果触摸点未被按下
        #             if class_nums_changing:  # 如果类别数量正在更改
        #                 class_nums_changing = False  # 重置类别数量更改标志
        #                 with timer("移除后半部分") as r:  # 使用计时器
        #                     mask_lt_4 = y_train < 4   # 小于 4 的掩码
        #                     mask_ge_4 = clfm.samples[1] >= 4  # 大于等于 4 的掩码
        #                     indices_ge_4 = np.where(mask_ge_4)[0]  # 获取大于等于4的索引
        #                     clfm.rm(indices_ge_4)  # 移除索引对应样本
        #                 last_train_time = r['passed']  # 记录移除时间
        #                 print("success changed to 4")  # 打印成功信息
        #     elif preesed:  # 如果触摸点在指示圆内
        #         img.draw_string(30, 112, "Press Red circle to make it\n Green(active).", color = image.COLOR_RED)  # 提示用户按红色圆圈以激活

        #         img.draw_string(0, 0, f'last_train_time= {last_train_time:.6f}s', color = image.COLOR_GREEN)  # 显示上一次训练时间
        # elif preesed:  # 如果触摸点在图像其他区域且被按下
        #     img.draw_string(30, 112, "Press Red circle to make it\n Green(active).", color = image.COLOR_RED)  # 提示用户按红色圆圈以激活

        #     img.draw_string(0, 0, ','.join(name_classes[:4]), color = image.COLOR_GREEN)  # 显示前4个类别名称
        #     img.draw_string(0, 20, '\n'.join(name_classes[4:]), color = image.COLOR_YELLOW if current_n_classes == 4 else image.COLOR_GREEN)  # 显示剩余类别名称
        # disp.show(img)  # 显示图像到显示屏

def cal_corner(points_coord):
    #计算3个点所夹的锐角,points_coord为3个点的坐标
    a = np.sqrt((points_coord[0][0] - points_coord[1][0])**2 + (points_coord[0][1] - points_coord[1][1])**2)
    b = np.sqrt((points_coord[0][0] - points_coord[2][0])**2 + (points_coord[0][1] - points_coord[2][1])**2)
    c = np.sqrt((points_coord[1][0] - points_coord[2][0])**2 + (points_coord[1][1] - points_coord[2][1])**2)
    cosB = (a**2 + c**2 - b**2) / (2*a*c)
    #将B转化为度数
    cor_B = np.arccos(cosB) * 180 / np.pi
    return cor_B
    

def request_servers(cmd_app_json_send):
    pass
    """
    if cmd_app_json_send["cmd"] == 0x2F: #返回符合特定空间特征的手势的特定特征，条件特征1~7分别为最中间、x最小、x最大、y最小、y最大,置信度最高，最低，返回特征为1~6分别为X坐标、Y坐标、宽度、高度、手势类型、置信度；
        hand_index = None
        key = int(cmd_app_json_send["data"])
        key_reply = cmd_app_json_send["reply"]
        reply_data = None
        if type(key) == int:
            for i,obj_info in enumerate(mk_objs_info_list):
                if key == 1:
                    min_distance_to_center = sys.maxsize
                    if min_distance_to_center > (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2:
                        min_distance_to_center = (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2
                        hand_index = i
                elif key == 2:
                    min_x = sys.maxsize
                    if min_x > obj_info["coords"][0]:
                        min_x = obj_info["coords"][0]
                        hand_index = i
                elif key == 3:
                    max_x = 0
                    if max_x < obj_info["coords"][0]:
                        max_x = obj_info["coords"][0]
                        hand_index = i
                elif key == 4:
                    min_y = sys.maxsize
                    if min_y > obj_info["coords"][1]:
                        min_y = obj_info["coords"][1]
                        hand_index = i
                elif key == 5:
                    max_y = 0
                    if max_y < obj_info["coords"][1]:
                        max_y = obj_info["coords"][1]
                        hand_index = i
                elif key == 6:
                    max_conf = 0
                    if max_conf < obj_info["pred_conf"]:
                        max_conf = obj_info["pred_conf"]
                        hand_index = i
                elif key == 7:
                    min_conf = 1
                    if min_conf > obj_info["pred_conf"]:
                        min_conf = obj_info["pred_conf"]
                        hand_index = i
            if hand_index is not None:
                obj_info = mk_objs_info_list[hand_index]
                if key_reply == 1:
                    reply_data = f"{obj_info['coords'][0]+obj_info['coords'][2]//2}"
                elif key_reply == 2:
                    reply_data = f"{obj_info['coords'][1]+obj_info['coords'][3]//2}"
                elif key_reply == 3:
                    reply_data = f"{obj_info['coords'][2]}"
                elif key_reply == 4:
                    reply_data = f"{obj_info['coords'][3]}"
                elif key_reply == 5:
                    reply_data = f"{name_classes[obj_info['class_idx']]}"
                elif key_reply == 6:
                    reply_data = f"{obj_info['pred_conf']:.2f}"
                cmd_app_json_send = {"cmd":0x2F, "data":reply_data}
                return cmd_app_json_send
    elif cmd_app_json_send["cmd"] == 0x30: #根据手势类型确定符合该手势且最靠中心的对象,返回特征为1~6分别为X坐标、Y坐标、宽度、高度、手势类型、置信度；
        key_name_index = cmd_app_json_send["data"] - 1
        key_reply = cmd_app_json_send["reply"]
        hand_index = None
        reply_data = None
        min_distance_to_center = sys.maxsize
        for i,obj_info in enumerate(mk_objs_info_list):
            if obj_info["class_idx"] == key_name_index:
                if min_distance_to_center > (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2:
                    min_distance_to_center = (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2
                    hand_index = i
        if hand_index is not None:
            obj_info = mk_objs_info_list[hand_index]
            if key_reply == 1:
                reply_data = f"{obj_info['coords'][0]+obj_info['coords'][2]//2}"
            elif key_reply == 2:
                reply_data = f"{obj_info['coords'][1]+obj_info['coords'][3]//2}"
            elif key_reply == 3:
                reply_data = f"{obj_info['coords'][2]}"
            elif key_reply == 4:
                reply_data = f"{obj_info['coords'][3]}"
            elif key_reply == 5:
                reply_data = f"{name_classes[obj_info['class_idx']]}"
            elif key_reply == 6:
                reply_data = f"{obj_info['pred_conf']:.2f}"
            cmd_app_json_send = {"cmd":0x30, "data":reply_data}
            return cmd_app_json_send
    elif cmd_app_json_send["cmd"] == 0x31: #根据空间特征确定手势对象，并根据关键点id返回关键点坐标
        key = cmd_app_json_send["data"]
        key_point_id = int(cmd_app_json_send["data1"]) - 1
        key_reply = cmd_app_json_send["reply"]
        reply_data = None
        hand_index = None
        for i,obj_info in enumerate(mk_objs_info_list):
            if key == 1:
                min_distance_to_center = sys.maxsize
                if min_distance_to_center > (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2:
                    min_distance_to_center = (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2
                    hand_index = i
            elif key == 2:
                if hand_index is None or obj_info["coords"][0] < mk_objs_info_list[hand_index]["coords"][0]:
                    hand_index = i
            elif key == 3:
                if hand_index is None or (obj_info["coords"][0] + obj_info["coords"][2]) > (mk_objs_info_list[hand_index]["coords"][0] + mk_objs_info_list[hand_index]["coords"][2]):
                    hand_index = i
            elif key == 4:
                if hand_index is None or obj_info["coords"][1] < mk_objs_info_list[hand_index]["coords"][1]:
                    hand_index = i
            elif key == 5:
                if hand_index is None or (obj_info["coords"][1] + obj_info["coords"][3]) > (mk_objs_info_list[hand_index]["coords"][1] + mk_objs_info_list[hand_index]["coords"][3]):
                    hand_index = i
            elif key == 6:
                max_conf = 0
                if max_conf < obj_info["pred_conf"]:
                    max_conf = obj_info["pred_conf"]
                    hand_index = i
            elif key == 7:
                min_conf = 1
                if min_conf > obj_info["pred_conf"]:
                    min_conf = obj_info["pred_conf"]
                    hand_index = i
        if hand_index is not None:
            obj_info = mk_objs_info_list[hand_index]
            if key_reply == 1:
                reply_data = f'{obj_info["points"][key_point_id*3+8]}'
            elif key_reply == 2:
                reply_data = f'{obj_info["points"][key_point_id*3+9]}'
            cmd_app_json_send = {"cmd":0x31, "data":reply_data}
            return cmd_app_json_send
    elif cmd_app_json_send["cmd"] == 0x32: #根据手势类型确定符合该手势且最靠中心的对象，并根据关键点id,返回关键点坐标      
        key_name_index = cmd_app_json_send["data"] - 1
        key_point_id = int(cmd_app_json_send["data1"]) - 1
        key_reply = cmd_app_json_send["reply"]
        reply_data = None
        hand_index = None
        min_distance_to_center = sys.maxsize
        for i,obj_info in enumerate(mk_objs_info_list):
            if obj_info["class_idx"] == key_name_index:
                if min_distance_to_center > (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2:
                    min_distance_to_center = (obj_info["coords"][0] + obj_info["coords"][2]//2 - 640//2)**2 + (obj_info["coords"][1] + obj_info["coords"][3]//2 - 480//2)**2
                    hand_index = i
        if hand_index is not None:
            obj_info = mk_objs_info_list[hand_index]
            if key_reply == 1:
                reply_data = f'{obj_info["points"][key_point_id*3+8]}'
            elif key_reply == 2:
                reply_data = f'{obj_info["points"][key_point_id*3+9]}'
            cmd_app_json_send = {"cmd":0x32, "data":reply_data}
            return cmd_app_json_send
    cmd_app_json_send = {"cmd":0x2F, "data":'0'}
    return cmd_app_json_send
    """
        


def mk_gesture_cmd(join_data):
    global camera_flip_status
    if join_data["cmd"] == 0xFF:
        camera_flip_status = join_data["data"]
    elif join_data["cmd"] == 0xFD:
        join_data['data'] = AT_PRESENT_FUNC
        return join_data
    else:
        data = request_servers(join_data)
        print(f"send data: {data}")
        return data
    return None

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

def application_tasks():
    try:
        app_main(disp)  # 进入主函数
    except Exception:
        msg = traceback.format_exc()  # 获取异常信息
        boot_log("application_tasks: app_main crashed", msg)
        traceback.print_exc()
        img = image.Image(disp.width(), disp.height())  # 创建画布
        img.draw_string(0, 0, msg, image.COLOR_WHITE)  # 绘制异常信息
        disp.show(img)  # 显示画布
        while not app.need_exit():
            time.sleep_ms(100)  # 小延时等待退出

def key_init():
    global key_swith_mid, key_swith_right, key_swith_left
    pinmap.set_pin_function("A22", "GPIOA22")
    pinmap.set_pin_function("A23", "GPIOA23")
    pinmap.set_pin_function("A25", "GPIOA25")

    key_swith_mid = gpio.GPIO("GPIOA22", gpio.Mode.IN)
    key_swith_right = gpio.GPIO("GPIOA23", gpio.Mode.IN)
    key_swith_left = gpio.GPIO("GPIOA25", gpio.Mode.IN)
    print("key_init done")

def key_scan():
    global rec_display, back
    last_key_time = time.ticks_ms()
    last_key_state = 0
    key_down_count = 0
    key_init()
    while True:
        # 实现50ms扫描一次按键
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
                if key_down_count == 2:
                    if rec_display < 1:
                        rec_display += 1
                key_down_count += 1
            elif key_swith_left.value() == 0:
                if key_down_count == 2:
                    if rec_display > 0:
                        rec_display -= 1
                key_down_count += 1
            else:
                key_down_count = 0
        time.sleep_ms(10)

# 实现key_scan线程
key_thread = threading.Thread(target=key_scan)
key_thread.daemon = True
key_thread.start()            

application_tasks()  # 执行应用程序任务
colse_flag = True  # 标记停止所有任务
asyncio.get_event_loop().stop()  # 停止异步事件循环
print("main stop")  # 主程序结束
time.sleep_ms(100)  # 最后延时以防止马上退出
