from maix import camera, display, image, app, time, touchscreen, key, gpio, pinmap
import socket
import os
import threading
import math
import select
import json
import pickle
import copy
from loading import Loading

font_size = 20
image.load_font("sourcehansans", "/maixapp/share/font/SourceHanSansCN-Regular.otf", size = font_size)
image.set_default_font("sourcehansans")

disp = display.Display()
ts = touchscreen.TouchScreen()

_image_width = disp.width()
_image_height = disp.height()
_btn_width = _image_width//6
_btn_height = _image_height//6

saved_line_threshold = None
line_learned = False

_to_show_binary = False

cam_flip_init = True
show_loading = True
loaindg = Loading(disp)
loading_thread = None
display_show_lock = threading.Lock()

g_line_list = [] 
g_line_spatial_attribute = {} 
g_line_id = [] 

g_thresholds = [[0, 30, -128, 127, -128, 127]] 
g_area_threshold = 100 
g_pixels_threshold = 100 
g_x_stride = 2 
g_y_stride = 1 
g_roi = None

save_path = "/mk/line_data.pkl"

current_lab = [0, 0, 0]
current_rgb = [0, 0, 0]
touched_x = -1
touched_y = -1

line_threshold_clicked = False 
lab_clicked = False  
threshold_fixed = False 
fixed_threshold = None 

# 添加长按锁定标志变量
longpress_lock = False
# 添加闪光灯控制相关变量
flashlight_enabled = False

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
    last_key_time = time.ticks_ms()
    key_down_count = 0
    key_init()
    
    while not app.need_exit():
        if time.ticks_ms() - last_key_time > 60:
            last_key_time = time.ticks_ms() 
            if key_swith_mid.value() == 0:
                if key_down_count == 2:
                    print("中键退出")
                    app.set_exit_flag(True)
                    break
                key_down_count += 1
            elif key_swith_right.value() == 0:
                if key_down_count == 2:
                    print('d+1')  
                key_down_count += 1
            elif key_swith_left.value() == 0:
                if key_down_count == 2:
                    print('d-1')  
                key_down_count += 1
            else:
                key_down_count = 0
        time.sleep_ms(10)

def on_user_key(key_id, state):
    global last_x, last_y, touched_x, touched_y, current_lab, current_rgb
    global line_learned, line_threshold_clicked, lab_clicked, threshold_fixed, fixed_threshold
    global longpress_lock, flashlight_enabled
    
    print(f"key_id={key_id}, state={state}")
    
    try:
        if key_id == 352:
            if state == 0 and longpress_lock == False:
                # 352按键短按：保留原有的学习功能
                print("352按键短按：执行原有学习功能")
                if (line_learned and line_threshold_clicked) or (line_learned and not (touched_x >= 0 and touched_y >= 0)):
                    if clear_learned_threshold():
                        print("学习键(352):成功清除Line阈值")
                    else:
                        print("学习键(352):清除Line阈值失败")
                elif touched_x >= 0 and touched_y >= 0:
                    if current_lab:
                        if save_learned_threshold(current_lab):
                            print("学习键(352):成功保存Line阈值")
                            touched_x = -1
                            touched_y = -1
                            last_x = -1
                            last_y = -1
                            threshold_fixed = False 
                            fixed_threshold = None
                            line_threshold_clicked = True
                            lab_clicked = False
                        else:
                            print("学习键(352):保存Line阈值失败")
                    else:
                        print("学习键(352):没有LAB数据可保存")
                else:
                    xm = _image_width // 2
                    ym = _image_height // 2
                    last_x = xm
                    last_y = ym
                    touched_x = xm
                    touched_y = ym
                    lab_clicked = True
                    line_threshold_clicked = False
            elif state == 1:
                longpress_lock = False
            elif state == 2:
                longpress_lock = True
                flashlight_enabled = not flashlight_enabled
                cam.set_flashlight(flashlight_enabled)
    except Exception as e:
        print(f"按键回调错误: {e}")

key_thread = threading.Thread(target=key_scan, daemon=True)
key_thread.daemon = True
key_thread.start()

key_obj = key.Key(on_user_key)

def save_line_data(data):
    try:
        with open(save_path, 'wb') as f:
            pickle.dump(data, f)
        return True
    except Exception as e:
        print(f"保存数据失败: {e}")
        return False

def load_line_data():
    try:
        if os.path.exists(save_path):
            with open(save_path, 'rb') as f:
                return pickle.load(f)
        else:
            print(f"文件不存在，新建文件:{save_path}")
            data = {
                'line_threshold': None,
                'line_lab': None,
                'line_rgb': None,
                'learned': False
            }
            save_line_data(data)
            return data
    except Exception as e:
        print(f"加载数据失败: {e}")
        data = {
            'line_threshold': None,
            'line_lab': None,
            'line_rgb': None,
            'learned': False
        }
        save_line_data(data)
        return data

def save_learned_threshold(lab_values):
    global saved_line_threshold, line_learned
    
    l_min = fetch_range('L', 'min', lab_values[0] - 15)  
    l_max = fetch_range('L', 'max', lab_values[0] + 15)
    a_min = fetch_range('A', 'min', lab_values[1] - 10)   
    a_max = fetch_range('A', 'max', lab_values[1] + 10)
    b_min = fetch_range('B', 'min', lab_values[2] - 10)   
    b_max = fetch_range('B', 'max', lab_values[2] + 10)
    
    threshold = [l_min, l_max, a_min, a_max, b_min, b_max]
    
    saved_line_threshold = threshold
    line_learned = True
    
    data = {
        'line_threshold': threshold,
        'line_lab': lab_values,
        'line_rgb': current_rgb,
        'learned': True
    }
    
    if save_line_data(data):
        print(f"成功保存Line阈值: {threshold}")
        global g_thresholds
        g_thresholds = [threshold]
        return True
    else:
        return False

def clear_learned_threshold():
    global saved_line_threshold, line_learned
    global line_threshold_clicked, fixed_threshold, lab_clicked
    global _to_show_binary  
    global g_thresholds
    
    saved_line_threshold = None
    line_learned = False
    line_threshold_clicked = False  
    fixed_threshold = None  
    
    has_lab = touched_x >= 0 and touched_y >= 0 and current_lab

    if _to_show_binary: 
        if has_lab:
            lab_clicked = True  
            lab_threshold = [
                fetch_range('L', 'min', current_lab[0] - 15),
                fetch_range('L', 'max', current_lab[0] + 15),
                fetch_range('A', 'min', current_lab[1] - 10),
                fetch_range('A', 'max', current_lab[1] + 10),
                fetch_range('B', 'min', current_lab[2] - 10),
                fetch_range('B', 'max', current_lab[2] + 10)
            ]
            g_thresholds = [lab_threshold]
        else:
            _to_show_binary = False
            lab_clicked = False  
            g_thresholds = [get_configured_threshold()]
    else:
        if has_lab:
            lab_clicked = True  
            lab_threshold = [
                fetch_range('L', 'min', current_lab[0] - 15),
                fetch_range('L', 'max', current_lab[0] + 15),
                fetch_range('A', 'min', current_lab[1] - 10),
                fetch_range('A', 'max', current_lab[1] + 10),
                fetch_range('B', 'min', current_lab[2] - 10),
                fetch_range('B', 'max', current_lab[2] + 10)
            ]
            g_thresholds = [lab_threshold]
        else:
            lab_clicked = False  
            g_thresholds = [get_configured_threshold()]

    data = {
        'line_threshold': None,
        'line_lab': None,
        'line_rgb': None,
        'learned': False
    }

    if save_line_data(data):
        print("成功清除Line阈值")
        if not has_lab:
            g_thresholds = [get_configured_threshold()]
        return True
    else:
        return False

line_data = load_line_data()
if line_data['learned'] and line_data['line_threshold']:
    saved_line_threshold = line_data['line_threshold']
    line_learned = True
    line_threshold_clicked = True  
    lab_clicked = False  
    g_thresholds = [saved_line_threshold]

def set_configured_threshold(threshold):
    if len(threshold) < 6:
        return 

    app.set_app_config_kv('demo_find_line', 'lmin', str(threshold[0]), False)
    app.set_app_config_kv('demo_find_line', 'lmax', str(threshold[1]), False)
    app.set_app_config_kv('demo_find_line', 'amin', str(threshold[2]), False)
    app.set_app_config_kv('demo_find_line', 'amax', str(threshold[3]), False)
    app.set_app_config_kv('demo_find_line', 'bmin', str(threshold[4]), False)
    app.set_app_config_kv('demo_find_line', 'bmax', str(threshold[5]), True)

def get_configured_threshold():
    threshold = [0, 100, -128, 127, -128, 127]

    value_str = app.get_app_config_kv('demo_find_line', 'lmin','', False)
    if len(value_str) > 0:
        threshold[0] = int(value_str)
    value_str = app.get_app_config_kv('demo_find_line', 'lmax','', False)
    if len(value_str) > 0:
        threshold[1] = int(value_str)
    value_str = app.get_app_config_kv('demo_find_line', 'amin','', False)
    if len(value_str) > 0:
        threshold[2] = int(value_str)
    value_str = app.get_app_config_kv('demo_find_line', 'amax','', False)
    if len(value_str) > 0:
        threshold[3] = int(value_str)
    value_str = app.get_app_config_kv('demo_find_line', 'bmin','', False)
    if len(value_str) > 0:
        threshold[4] = int(value_str)
    value_str = app.get_app_config_kv('demo_find_line', 'bmax','', False)
    if len(value_str) > 0:
        threshold[5] = int(value_str)
    return threshold

def rgb_to_lab(r, g, b):
    def rgb_to_xyz(r, g, b):
        # Normalize RGB values
        r /= 255.0
        g /= 255.0
        b /= 255.0

        # Apply gamma correction
        r = r / 12.92 if r <= 0.04045 else ((r + 0.055) / 1.055) ** 2.4
        g = g / 12.92 if g <= 0.04045 else ((g + 0.055) / 1.055) ** 2.4
        b = b / 12.92 if b <= 0.04045 else ((b + 0.055) / 1.055) ** 2.4

        # Convert RGB to XYZ
        x = r * 0.4124564 + g * 0.3575761 + b * 0.1804375
        y = r * 0.2126729 + g * 0.7151522 + b * 0.0721750
        z = r * 0.0193339 + g * 0.1191920 + b * 0.9503041

        return x, y, z

    def xyz_to_lab(x, y, z):
        # Reference white point
        Xn, Yn, Zn = 0.9505, 1.0000, 1.0890

        # Normalize XYZ
        x /= Xn
        y /= Yn
        z /= Zn

        # Convert to LAB
        x = x ** (1 / 3) if x > 0.008856 else (x * 7.787 + 16 / 116)
        y = y ** (1 / 3) if y > 0.008856 else (y * 7.787 + 16 / 116)
        z = z ** (1 / 3) if z > 0.008856 else (z * 7.787 + 16 / 116)

        l = max(0, min(100, (116 * y) - 16 + 10)) 
        a = max(-128, min(127, 500 * (x - y)))
        b = max(-128, min(127, 200 * (y - z)))

        return round(l, 3), round(a, 3), round(b, 3)

    x, y, z = rgb_to_xyz(r, g, b)
    return xyz_to_lab(x, y, z)

def fetch_range(lab_type, args, vla):
    tmp_val = int(vla)
    if lab_type in 'L':
        if args in 'min':
            if tmp_val < 0:
                tmp_val = 0
            return int(tmp_val)
        elif args in 'max':
            if tmp_val > 100:
                tmp_val = 100
            return int(tmp_val)
    elif lab_type in 'A':
        if args in 'min':
            if tmp_val < -128:
                tmp_val = -128
            return int(tmp_val)
        elif args in 'max':
            if tmp_val > 127:
                tmp_val = 127
            return int(tmp_val)
    elif lab_type in 'B':
        if args in 'min':
            if tmp_val < -128:
                tmp_val = -128
            return int(tmp_val)
        elif args in 'max':
            if tmp_val > 127:
                tmp_val = 127
            return int(tmp_val)
    return int(tmp_val)

def draw_line_with_arrow(img, x1, y1, x2, y2, color, thickness=3, arrow_size=16):
    img.draw_line(x1, y1, x2, y2, color, thickness)
    
    arrow_x, arrow_y = x2, y2
    dx, dy = x2 - x1, y2 - y1  
    
    length = math.sqrt(dx*dx + dy*dy)
    if length == 0:
        return
    
    ux = dx / length
    uy = dy / length
    
    arrow_tip_x = arrow_x
    arrow_tip_y = arrow_y
    
    cos30 = 0.866  
    sin30 = 0.5    

    left_x = arrow_tip_x - arrow_size * (ux * cos30 + uy * sin30)
    left_y = arrow_tip_y - arrow_size * (uy * cos30 - ux * sin30)
    
    right_x = arrow_tip_x - arrow_size * (ux * cos30 - uy * sin30)
    right_y = arrow_tip_y - arrow_size * (uy * cos30 + ux * sin30)
    
    if (0 <= left_x < disp.width() and 0 <= left_y < disp.height() and
        0 <= right_x < disp.width() and 0 <= right_y < disp.height()):
        img.draw_line(int(arrow_tip_x), int(arrow_tip_y), int(left_x), int(left_y), color, thickness)
        img.draw_line(int(arrow_tip_x), int(arrow_tip_y), int(right_x), int(right_y), color, thickness)

def detect_lines(img):
    local_line_list = []
    local_line_spatial_attribute = {}
    local_line_id = []
    
    try:
        lines = img.get_regression(
            g_thresholds,
            invert=False,
            roi=g_roi,
            x_stride=g_x_stride,
            y_stride=g_y_stride,
            area_threshold=g_area_threshold,
            pixels_threshold=g_pixels_threshold
        )
        for i, line in enumerate(lines):
            try:
                orig_x1, orig_y1 = line.x1(), line.y1()
                orig_x2, orig_y2 = line.x2(), line.y2()
                theta = line.theta()
                rho = line.rho()
                
                if orig_y1 < orig_y2:
                    x1, y1 = orig_x2, orig_y2  
                    x2, y2 = orig_x1, orig_y1  
                else:
                    x1, y1 = orig_x1, orig_y1  
                    x2, y2 = orig_x2, orig_y2  
                
                if theta > 90:
                    converted_theta = 270 - theta
                else:
                    converted_theta = 90 - theta
                
                xm = (x1 + x2) // 2
                ym = (y1 + y2) // 2
                
                frame_center_x = disp.width() // 2
                offset = xm - frame_center_x
                
                length = math.sqrt((x2-x1)**2 + (y2-y1)**2)
                
                line_data = {
                    'id': i,
                    'x1': int(x1), 'y1': int(y1),  
                    'x2': int(x2), 'y2': int(y2),  
                    'xm': int(xm), 'ym': int(ym),
                    'angle': int(converted_theta),
                    'offset': int(offset),
                    'length': int(length),
                    'theta': int(theta),  
                    'rho': int(rho)
                }
                
                local_line_list.append(line_data)
                local_line_id.append(i)
                
                local_line_spatial_attribute[str(i)] = {
                    'x': int(xm),
                    'y': int(ym),
                    'w': int(x2-x1),
                    'h': int(y2-y1),
                    'id': i
                }
                
            except Exception as e:
                print(f"处理线条 {i} 错误: {e}")
                continue
        
        for line_data in local_line_list:
            try:
                x1, y1, x2, y2 = line_data['x1'], line_data['y1'], line_data['x2'], line_data['y2']
                offset, angle, rho = line_data['offset'], line_data['angle'], line_data['rho']
                i = line_data['id']
                
                draw_line_with_arrow(img, x1, y1, x2, y2, image.COLOR_GREEN)
            except Exception as e:
                print(f"处理线条 {i} 错误: {e}")
                continue
        
        local_line_list.sort(key=lambda x: x['length'], reverse=True)
        
        global g_line_list, g_line_spatial_attribute, g_line_id
        g_line_list = local_line_list
        g_line_spatial_attribute = local_line_spatial_attribute  
        g_line_id = local_line_id
        
        return len(local_line_list)
        
    except Exception as e:
        print(f"直线检测错误: {e}")
        return 0

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

cam = camera.Camera(disp.width(), disp.height(), image.Format.FMT_RGB888)
cam_min = cam.add_channel(disp.width(), disp.height())

g_roi = [1, 1, disp.width()-1, disp.height()-1]

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

                # send_socket_message('global_get_camera_flip_status')  # 禁用相机翻转状态请求
                print("相机翻转状态请求已禁用")
            time.sleep_ms(500)
            continue
        
        if not client_socket:
            time.sleep_ms(500)
            continue

        ready_to_read, _, _ = select.select([client_socket], [], [], 0.25)

        if ready_to_read:
            data = client_socket.recv(1024)
            if data:
                print(f"Received from server: {data.decode()}")
                tmp_str = data.decode(errors='ignore')
                socket_rx_string_buffer += tmp_str
                while True:
                    start_idx = socket_rx_string_buffer.find('@#')
                    end_idx = socket_rx_string_buffer.find('#@')
                    if start_idx != -1 and end_idx != -1 and start_idx < end_idx:
                        packet = socket_rx_string_buffer[start_idx+2 : end_idx]
                        
                        print('[收到完整数据包]\n', packet)
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
                        break
            else:
                print("Connection closed by server")
                time.sleep_ms(500)
        
        time.sleep_ms(25)

accept_thread = threading.Thread(target=socket_worker)
accept_thread.daemon = True
accept_thread.start()

def set_camera_flip(state):
    global cam_flip_init
    # cam.vflip(state)
    # cam_min.vflip(state)
    cam_flip_init = True
    print(f"相机翻转功能已禁用，忽略翻转设置: {state}")

def send_socket_message(message):
    try:
        message_packet = f"@#{message}#@"
        print('[发送成功]\n', message_packet)
        if client_socket:
            client_socket.sendall(message_packet.encode())
    except Exception as e:
        print(e)

def clamp_short(value):
    return max(-32768, min(32767, int(round(value)))) 

def handler_socket_message(message):
    print('[收到消息]', message)

    cmds = message.split("\n")
    send_data = ""

    if cmds[0] == "global_set_camera_flip_status":
        # value = cmds[1]
        # if value == "1":
        #     set_camera_flip(1)
        # else:
        #     set_camera_flip(0)
        print("相机翻转功能已禁用，忽略翻转命令")

    if cmds[0] == "get_line_angle_offset":
        if (lab_clicked or line_threshold_clicked) and len(g_line_list) > 0:
            try:
                main_line = g_line_list[0]
                angle = main_line['angle']
                
                angle_offset = 100 - (angle / 180.0) * 200
                angle_offset = max(-100, min(100, int(round(angle_offset)))) 
                
                print(f"angle: {angle}, angle_offset: {angle_offset}")
                send_data = f"get_line_angle_offset\nfull\n{angle_offset}"
            except (KeyError, IndexError, TypeError) as e:
                print(f"get_line_angle_offset error: {e}")
                send_data = "get_line_angle_offset\nfull\n-99999"
        else:
            send_data = "get_line_angle_offset\nfull\n-99999"

    if cmds[0] == "get_line_detected":
        if lab_clicked or line_threshold_clicked:
            detected = 1 if len(g_line_list) > 0 else 0
        else:
            detected = 0
            
        send_data = f"get_line_detected\nfull\n{detected}"

    if cmds[0] == "get_line_coordinate":
        param1 = cmds[1]
            
        if (lab_clicked or line_threshold_clicked) and len(g_line_list) > 0:
            line = g_line_list[0]  
            x1, y1 = line['x1'], line['y1']  
            x2, y2 = line['x2'], line['y2']  
            xm, ym = line['xm'], line['ym']  
            
            if param1 ==  "1":
                send_data = f"get_line_coordinate\nfull\n{x1}"
            elif param1 == "2":
                send_data = f"get_line_coordinate\nfull\n{y1}"
            elif param1 == "3":
                send_data = f"get_line_coordinate\nfull\n{xm}"
            elif param1 == "4":
                send_data = f"get_line_coordinate\nfull\n{ym}"
            elif param1 == "5":
                send_data = f"get_line_coordinate\nfull\n{x2}"
            elif param1 == "6":
                send_data = f"get_line_coordinate\nfull\n{y2}"
            else:
                send_data = f"get_line_coordinate\nempty"
        else:
            send_data = f"get_line_coordinate\nempty"

    if send_data:
        send_socket_message(send_data)

# def wait_camera_flip_init():
    # while(not cam_flip_init):
    #     try:
    #         send_socket_message('global_get_camera_flip_status')
    #     except Exception as e:
    #         print(e)
    #     time.sleep_ms(250)
    

# 禁用相机翻转初始化线程
# accept_thread2 = threading.Thread(target=wait_camera_flip_init, daemon=True)
# accept_thread2.daemon = True
# accept_thread2.start()


print("=== Line Tracking AI Application ===")

if not line_learned:
    g_thresholds = [get_configured_threshold()]
print("当前使用阈值:", g_thresholds)

time.sleep_ms(1000)
show_loading = False

icon_back_rgba = image.load("./assets/images/icon_back.png", image.Format.FMT_RGBA8888)
icon_back = icon_back_rgba.resize(icon_back_rgba.width(), icon_back_rgba.height(), image.Fit.FIT_CONTAIN)

_img_eye_open_rgba = image.load("./assets/images/img_eye_open.png", image.Format.FMT_RGBA8888)
img_eye_open = _img_eye_open_rgba.resize(_img_eye_open_rgba.width(), _img_eye_open_rgba.height(), image.Fit.FIT_CONTAIN)

_img_eye_close_rgba = image.load("./assets/images/img_eye_close.png", image.Format.FMT_RGBA8888)
img_eye_close = _img_eye_close_rgba.resize(_img_eye_close_rgba.width(), _img_eye_close_rgba.height(), image.Fit.FIT_CONTAIN)

_line_learning_btn_rgba = image.load("./assets/images/line_learning_btn.png", image.Format.FMT_RGBA8888)
line_learning_btn = _line_learning_btn_rgba.resize(_line_learning_btn_rgba.width(), _line_learning_btn_rgba.height(), image.Fit.FIT_CONTAIN)

_learning_save_rgba = image.load("./assets/images/learning.png", image.Format.FMT_RGBA8888)
learning_save_btn = _learning_save_rgba.resize(_learning_save_rgba.width(), _learning_save_rgba.height(), image.Fit.FIT_CONTAIN)

_forget_rgba = image.load("./assets/images/forget.png", image.Format.FMT_RGBA8888)
forget_btn = _forget_rgba.resize(_forget_rgba.width(), _forget_rgba.height(), image.Fit.FIT_CONTAIN)

box_border_img = image.load("./assets/images/box_border.png", image.Format.FMT_RGBA8888)
box_bg_img = image.load("./assets/images/box_bg.png", image.Format.FMT_RGBA8888)
box_bg2_img = image.load("./assets/images/box_bg2.png", image.Format.FMT_RGBA8888)
box_bg2_active_img = image.load("./assets/images/box_bg2_active.png", image.Format.FMT_RGBA8888)

exit_btn_pos = [6, 6, icon_back.width(), icon_back.height()]
binary_btn_pos = [_image_width - img_eye_open.width() - 100, (icon_back.height() - img_eye_open.height()) // 2 + 6 , img_eye_open.width(), img_eye_open.height()]

line_learning_btn_pos = [(_image_width - line_learning_btn.width())//2 , (icon_back.height() - line_learning_btn.height()) // 2 + 6, line_learning_btn.width(), line_learning_btn.height()]

learning_save_btn_pos = [_image_width - learning_save_btn.width() - 6, 6, learning_save_btn.width(), learning_save_btn.height()]

forget_btn_pos = [_image_width - forget_btn.width() - 6, 6, forget_btn.width(), forget_btn.height()]

line_threshold_display_pos = [_image_width - box_bg2_img.width() - 10, _image_height - box_bg2_img.height() - 10, box_bg2_img.width(), box_bg2_img.height()]

last_x = -1
last_y = -1
last_tick = time.time()

def is_in_button(x, y, btn_pos):
    return (btn_pos[0] <= x <= btn_pos[0] + btn_pos[2] and 
            btn_pos[1] <= y <= btn_pos[1] + btn_pos[3])

def has_available_threshold():
    return line_learned or (touched_x >= 0 and touched_y >= 0)

def draw_ui_buttons(img):
    draw_transparent_image(img, exit_btn_pos[0], exit_btn_pos[1], icon_back)
    
    if has_available_threshold():
        eye_icon = img_eye_close if _to_show_binary else img_eye_open
        draw_transparent_image(img, binary_btn_pos[0], binary_btn_pos[1], eye_icon)
    
    draw_transparent_image(img, line_learning_btn_pos[0], line_learning_btn_pos[1], line_learning_btn)
    
    if line_learned and line_threshold_clicked:
        draw_transparent_image(img, forget_btn_pos[0], forget_btn_pos[1], forget_btn)
    elif touched_x >= 0 and touched_y >= 0:
        draw_transparent_image(img, learning_save_btn_pos[0], learning_save_btn_pos[1], learning_save_btn)
    elif line_learned:
        draw_transparent_image(img, forget_btn_pos[0], forget_btn_pos[1], forget_btn)

def draw_line_threshold_info(img):
    if line_learned and saved_line_threshold:
        bg_x, bg_y, bg_w, bg_h = line_threshold_display_pos
        
        if line_threshold_clicked:
            draw_transparent_image(img, bg_x, bg_y, box_bg2_active_img)
            text_color = image.COLOR_BLACK
        else:
            draw_transparent_image(img, bg_x, bg_y, box_bg2_img)
            text_color = image.COLOR_WHITE
        
        color_rect_w = 16
        color_rect_h = 16
        color_rect_x = bg_x + 12
        color_rect_y = bg_y + box_bg2_img.height() // 2 - color_rect_h // 2
        
        line_data = load_line_data()
        if line_data['learned'] and line_data['line_rgb']:
            line_rgb = line_data['line_rgb']
        else:
            line_rgb = current_rgb if current_rgb != [0, 0, 0] else [128, 128, 128]
        
        img.draw_rect(color_rect_x, color_rect_y, color_rect_w, color_rect_h, 
                      image.Color.from_rgb(line_rgb[0], line_rgb[1], line_rgb[2]), -1)
        
        title_text = "Line"
        title_x = color_rect_x + color_rect_w + 8
        title_y = bg_y + (box_bg2_img.height() - 16) // 2  
        img.draw_string(title_x, title_y, title_text, text_color, 1)
        

def draw_learning_lab_info(img):
    if last_x >= 0 and last_y >= 0:
        lab_bg_x = 10
        lab_bg_y = 420
        
        draw_transparent_image(img, lab_bg_x, lab_bg_y, box_bg_img)
        
        color_size = 16
        color_x = lab_bg_x + 16
        color_y = lab_bg_y + (box_bg_img.height() // 2) - (color_size // 2)
        img.draw_rect(color_x, color_y, color_size, color_size, 
                      image.Color.from_rgb(current_rgb[0], current_rgb[1], current_rgb[2]), -1)
        
        lab_start_x = color_x + color_size + 8
        lab_y = lab_bg_y + (box_bg_img.height() // 2)-8
        
        lab_text = f"L {int(current_lab[0])},"
        img.draw_string(lab_start_x, lab_y, lab_text, image.COLOR_WHITE, 1)
        lab_text = f"A {int(current_lab[1])},"
        img.draw_string(lab_start_x + 60, lab_y, lab_text, image.COLOR_WHITE, 1)
        lab_text = f"B {int(current_lab[2])}"
        img.draw_string(lab_start_x + 120, lab_y, lab_text, image.COLOR_WHITE, 1)
        
        if lab_clicked:
            draw_transparent_image(img, lab_bg_x, lab_bg_y, box_border_img)

def handle_touch_events():
    global _to_show_binary, last_x, last_y, ts
    global touched_x, touched_y, current_lab, current_rgb
    global line_threshold_clicked, lab_clicked, threshold_fixed, fixed_threshold
    global g_thresholds  
    
    touch_x, touch_y, pressed = ts.read()
    
    if pressed:
        if is_in_button(touch_x, touch_y, exit_btn_pos):
            print("退出按钮被按下")
            app.set_exit_flag(True)
            return
        
        if has_available_threshold() and is_in_button(touch_x, touch_y, binary_btn_pos):
            print("二值化按钮被按下")
            _to_show_binary = not _to_show_binary
            time.sleep_ms(200)  # 防抖动
            return
        
        if line_learned and line_threshold_clicked and is_in_button(touch_x, touch_y, forget_btn_pos):
            if clear_learned_threshold():
                print("成功清除Line阈值")
            else:
                print("清除Line阈值失败")
            time.sleep_ms(200)  
            return
        elif touched_x >= 0 and touched_y >= 0 and is_in_button(touch_x, touch_y, learning_save_btn_pos):
            if save_learned_threshold(current_lab):
                print("成功保存Line阈值")
                touched_x = -1
                touched_y = -1
                last_x = -1
                last_y = -1
                threshold_fixed = False  
                fixed_threshold = None
                line_threshold_clicked = True
                lab_clicked = False
            else:
                print("保存Line阈值失败")
            time.sleep_ms(200)  
            return
        elif line_learned and is_in_button(touch_x, touch_y, forget_btn_pos):
            if clear_learned_threshold():
                print("成功清除Line阈值")
            else:
                print("清除Line阈值失败")
            time.sleep_ms(200)  
            return
        
        if last_x >= 0 and last_y >= 0:
            lab_bg_x = 10
            lab_bg_y = 420
            lab_area = [lab_bg_x, lab_bg_y, box_bg_img.width(), box_bg_img.height()]
            if is_in_button(touch_x, touch_y, lab_area):
                lab_clicked = True
                line_threshold_clicked = False  # 互斥：取消Line选中
                if current_lab and len(current_lab) >= 3:
                    threshold_fixed = True
                    fixed_threshold = [
                        fetch_range('L', 'min', current_lab[0] - 15),  
                        fetch_range('L', 'max', current_lab[0] + 15),
                        fetch_range('A', 'min', current_lab[1] - 10),  
                        fetch_range('A', 'max', current_lab[1] + 10),
                        fetch_range('B', 'min', current_lab[2] - 10),   
                        fetch_range('B', 'max', current_lab[2] + 10)
                    ]
                    g_thresholds = [fixed_threshold]
                time.sleep_ms(200)  
                return
        
        if line_learned and is_in_button(touch_x, touch_y, line_threshold_display_pos):
            line_threshold_clicked = True
            lab_clicked = False  # 互斥：取消LAB选中
            g_thresholds = [saved_line_threshold]
            time.sleep_ms(200)  
            return
        
        line_threshold_clicked = False
        lab_clicked = True  
        threshold_fixed = False  
        fixed_threshold = None
        
        touched_x = touch_x
        touched_y = touch_y
        last_x = touch_x
        last_y = touch_y
        return

def draw_transparent_image(target_img, x, y, source_img):
    try:
        if (source_img.format() == image.Format.FMT_RGBA8888 and 
            target_img.format() == image.Format.FMT_RGB888):
            
            x_end = min(x + source_img.width(), target_img.width())
            y_end = min(y + source_img.height(), target_img.height())
            width = x_end - x
            height = y_end - y
            
            if width <= 0 or height <= 0:
                return
            
            try:
                temp_canvas = image.Image(width, height, image.Format.FMT_RGBA8888)
                
                bg_region = target_img.crop(x, y, width, height)
                temp_canvas.draw_image(0, 0, bg_region.to_format(image.Format.FMT_RGBA8888))
                
                temp_canvas.draw_image(0, 0, source_img)
                
                result_rgb = temp_canvas.to_format(image.Format.FMT_RGB888)
                target_img.draw_image(x, y, result_rgb)
                
            except Exception as crop_error:
                print(f"crop方法失败: {crop_error}，尝试备选方案")
                try:
                    rgb_source = source_img.to_format(image.Format.FMT_RGB888)
                    target_img.draw_image(x, y, rgb_source)
                except Exception as convert_error:
                    print(f"格式转换失败: {convert_error}，使用直接绘制")
                    target_img.draw_image(x, y, source_img)
        else:
            target_img.draw_image(x, y, source_img)
    except Exception as e:
        print(f"绘制透明图像错误: {e}")
        try:
            target_img.draw_image(x, y, source_img)
        except:
            print("跳过这个图标")

def draw_circle_target(img, x, y, rgb_color):
    if x >= 0 and y >= 0:
        circle_radius = 23 // 2 
        img.draw_circle(x, y, circle_radius, image.Color.from_rgb(rgb_color[0], rgb_color[1], rgb_color[2]), -1)
        img.draw_circle(x, y, circle_radius, image.Color.from_rgb(255, 255, 255), 2)

while not app.need_exit():
    if int(time.time()) - last_tick > 5:
        send_socket_message('PASS')
        last_tick = time.time()
        
    try:
        img = cam.read()
        if img is None:
            continue
        
        handle_touch_events()
        
        if last_x >= 0 and last_y >= 0 and not threshold_fixed:
            rgb = img.get_pixel(last_x, last_y, True)
            if rgb:
                current_rgb = list(rgb)
                current_lab = list(rgb_to_lab(rgb[0], rgb[1], rgb[2]))
                touched_x = last_x
                touched_y = last_y
                
                if lab_clicked:
                    threshold_fixed = True
                    fixed_threshold = [
                        fetch_range('L', 'min', current_lab[0] - 15),  
                        fetch_range('L', 'max', current_lab[0] + 15),
                        fetch_range('A', 'min', current_lab[1] - 10),  
                        fetch_range('A', 'max', current_lab[1] + 10),
                        fetch_range('B', 'min', current_lab[2] - 10),  
                        fetch_range('B', 'max', current_lab[2] + 10)
                    ]
                    g_thresholds = [fixed_threshold]
            else:
                    temp_threshold = [
                        fetch_range('L', 'min', current_lab[0] - 15),  
                        fetch_range('L', 'max', current_lab[0] + 15),
                        fetch_range('A', 'min', current_lab[1] - 10),  
                        fetch_range('A', 'max', current_lab[1] + 10),
                        fetch_range('B', 'min', current_lab[2] - 10),  
                        fetch_range('B', 'max', current_lab[2] + 10)
                    ]
        
        line_count = 0
        if lab_clicked or line_threshold_clicked:
            line_count = detect_lines(img)
        else:
            g_line_list = []
            g_line_spatial_attribute = {}
            g_line_id = []
        
        if _to_show_binary and has_available_threshold():
            img = img.binary(g_thresholds, False)
            if (lab_clicked or line_threshold_clicked) and line_count > 0:
                for line_data in g_line_list:   
                    try:
                        x1, y1, x2, y2 = line_data['x1'], line_data['y1'], line_data['x2'], line_data['y2']
                        draw_line_with_arrow(img, x1, y1, x2, y2, image.COLOR_GREEN)
                    except Exception as e:
                        print(f"二值化模式下绘制线条错误: {e}")
                        continue
        elif _to_show_binary and not has_available_threshold():
            _to_show_binary = False
        
        if last_x >= 0 and last_y >= 0:
            draw_circle_target(img, last_x, last_y, current_rgb)
        
        draw_ui_buttons(img)
        
        draw_line_threshold_info(img)
        
        draw_learning_lab_info(img)
        
        with display_show_lock:
            disp.show(img)
            
        time.sleep_ms(10)
    except Exception as e:
        print(f"Main loop error: {e}")
        time.sleep_ms(100)
