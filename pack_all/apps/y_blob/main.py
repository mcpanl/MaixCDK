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
rect_disp_lock = threading.Lock()
g_relevant_info_id_lock = threading.Lock()

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

    global s_data_storage, learning_flag, longpress_lock

    try:
        if key_id == 352:
            if state == 0 and longpress_lock == False:
                with rect_disp_lock:
                    add_xy_draw_color(image_center_x, image_center_y)
            elif state == 1:
                longpress_lock = False
            elif state == 2:
                longpress_lock = True
    except Exception as e:
        pass

# 覆盖原来的用户按钮事件
key_obj = key.Key(on_user_key)

from maix import touchscreen, app, pinmap, gpio, camera
from collections import deque
import pickle, math
import os
import select
import socket
import copy

cam = camera.Camera(disp.width(), disp.height(), image.Format.FMT_RGBA8888)
cam_min = cam.add_channel(disp.width(), disp.height())
ts = touchscreen.TouchScreen()  # 初始化触摸屏设备
img = None
img_min = None
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

key_thread = threading.Thread(target=key_scan, daemon=True)
key_thread.daemon = True
key_thread.start()

# 存储地址
save_path = "/mk/data.pkl"

def set_camera_flip(state):
    global cam_flip_init
    cam.vflip(state)
    # cam.hmirror(state)
    cam_min.vflip(state)
    # cam_min.hmirror(state)
    cam_flip_init = True

def send_socket_message(message):
    try:
        message_packet = f"@#{message}#@"
        print('[发送成功]\n', message_packet)
        if client_socket:
            client_socket.sendall(message_packet.encode())
    except Exception as e:
        print(e)

def handler_socket_message(message):
    print('[收到消息]', message)

    cmds = message.split("\n")
    send_data = ""

    if cmds[0] == "global_set_camera_flip_status":
        value = cmds[1]
        if value == "1":
            set_camera_flip(1)
        else:
            set_camera_flip(0)

    if cmds[0] == "get_color_count":
        param1 = cmds[1]
        count = 0
        
        if param1 == "0":
            count = len(g_blob_list) # 所有颜色的色块数量
            send_data = f"get_color_count\nfull\n{count}"

        elif len(g_blob_list) > 0:
            # 根据 id 找到对应的 blob
            for item in g_blob_list:
                if item["data_storage_index"] + 1 == int(param1):
                    count += 1
            
        send_data = f"get_color_count\nfull\n{count}"
    
    if cmds[0] == "get_color_space_info":
        param1 = cmds[1]
        param2 = cmds[2]

        with g_relevant_info_id_lock: # 等色块遍历完成得到 g_relevant_info_id 后再执行，不然会导致遍历色块途中多线程出现发送数据 
            if len(g_blob_list) > 0:
                info_id = 0
                obj = {}

                if int(param1) <= 7:
                    if param1 == "1":
                        info_id = g_relevant_info_id["center_distance_min"]
                    elif param1 == "2":
                        info_id = g_relevant_info_id["x_min"]
                    elif param1 == "3":
                        info_id = g_relevant_info_id["x_max"]
                    elif param1 == "4":
                        info_id = g_relevant_info_id["y_min"]
                    elif param1 == "5":
                        info_id = g_relevant_info_id["y_max"]
                    elif param1 == "6":
                        info_id = g_relevant_info_id["area_max"]
                    elif param1 == "7":
                        info_id = g_relevant_info_id["area_min"]
                    
                    # 根据 id 找到对应的 blob
                    for item in g_blob_list:
                        if item["id"] == info_id:
                            obj = item
                else:
                    # 找到右下角 ID 列表中最中间的色块
                    target_color_index = int(param1) - 7
                    target_blobs = []
                    
                    # 收集指定颜色的所有色块
                    for item in g_blob_list:
                        if item["data_storage_index"] + 1 == target_color_index:
                            target_blobs.append(item)
                    
                    if len(target_blobs) > 0:
                        # 计算所有色块的中心点
                        center_x = image_center_x
                        center_y = image_center_y
                        
                        # 找到距离图像中心最近的色块（最中间的色块）
                        min_distance = float('inf')
                        for item in target_blobs:
                            blob_center_x = item['x'] + item['w'] // 2
                            blob_center_y = item['y'] + item['h'] // 2
                            distance = math.sqrt((blob_center_x - center_x) ** 2 + (blob_center_y - center_y) ** 2)
                            
                            if distance < min_distance:
                                min_distance = distance
                                obj = item
                    else:
                        obj = {}
                
                if len(obj) > 0:
                    if param2 == "1":
                        send_data = f"get_color_space_info\nfull\n{obj['x'] + obj['w'] // 2}"
                    elif param2 == "2":
                        send_data = f"get_color_space_info\nfull\n{obj['y'] + obj['h'] // 2}"
                    elif param2 == "3":
                        send_data = f"get_color_space_info\nfull\n{obj['w']}"
                    elif param2 == "4":
                        send_data = f"get_color_space_info\nfull\n{obj['h']}"
            else:
                send_data = f"get_color_space_info\nempty"

    if cmds[0] == "get_color_info":
        param1 = cmds[1]
        
        with g_relevant_info_id_lock: # 等色块遍历完成得到 g_relevant_info_id 后再执行，不然会导致遍历色块途中多线程出现发送数据 
            if len(g_blob_list) > 0:
                info_id = 0

                if param1 == "1":
                    info_id = g_relevant_info_id["center_distance_min"]

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
                    send_data = f"get_color_info\nempty"
                else:
                    current_index = -1

                    # 根据 id 找到对应的 blob
                    for item in g_blob_list:
                        if item["id"] == info_id:
                            current_index = item["data_storage_index"]
                    
                    send_data = f"get_color_info\nfull\n{current_index + 1}"
            
            else:
                send_data = f"get_color_info\nempty"

    if send_data:
        send_socket_message(send_data)

    # print(repr('send_data ' + send_data))

def wait_camera_flip_init():
    while(not cam_flip_init):
        try:
            send_socket_message('global_get_camera_flip_status')
        except Exception as e:
            print(e)
        time.sleep_ms(250)

accept_thread2 = threading.Thread(target=wait_camera_flip_init, daemon=True)
accept_thread2.daemon = True
accept_thread2.start()

def get_str_width(str, thickness = 1):
    return len(str) * disp_str_size // 2 * thickness

def get_str_height(thickness = 1):
    return disp_str_size * thickness

# 屏幕中心坐标
center_coordinate = (disp.width() // 2, disp.height() // 2)
image_center_x = center_coordinate[0]
image_center_y = center_coordinate[1]

disp_str_size = 24
# draw exit button
# 加载工程配置图片
exit_str = ' < '
exit_label = image.load("./assets/images/icon_back.png", image.Format.FMT_RGBA8888)
exit_btn_pos = [6, 6, 72, 72]
# print(f'disp:{disp.width()},{disp.height()}, cam:{cam.width()},{cam.height()}')
_learning_img = image.load("./assets/images/learning.png", image.Format.FMT_RGBA8888)
learning_img = _learning_img.resize(_learning_img.width(), _learning_img.height(), image.Fit.FIT_CONTAIN)

_forget_img = image.load("./assets/images/forget.png", image.Format.FMT_RGBA8888)
forget_img = _forget_img.resize(_forget_img.width(), _forget_img.height(), image.Fit.FIT_CONTAIN)

box_border_img = image.load("./assets/images/box_border.png", image.Format.FMT_RGBA8888)
box_bg_img = image.load("./assets/images/box_bg.png", image.Format.FMT_RGBA8888)
box_bg2_img = image.load("./assets/images/box_bg2.png", image.Format.FMT_RGBA8888)
box_bg2_active_img = image.load("./assets/images/box_bg2_active.png", image.Format.FMT_RGBA8888)

# 学习模式组件坐标
learning_btn_pos = [disp.width() - learning_img.width(), 0, learning_img.width(), learning_img.height()]
# 学习操作标志位
learning_flag = False
# 间隔
intarval = 32
# 开始的高度
start_height = disp.height() - 30 - disp_str_size
start_width = 580
learned_coordinate = dict()
rect_coordinate = dict()
for learned_key in range(7):
    learned_coordinate[str(learned_key)] = [start_width, start_height]
    rect_coordinate[str(learned_key)] = [start_width - 40, start_height - 12, get_str_width(f'ID{learned_key}') + 58, disp_str_size + 24]
    start_height = start_height - intarval - disp_str_size
# print(learned_coordinate)



blob_learning_btn_img = image.load("./assets/images/blob_learning_btn.png", image.Format.FMT_RGBA8888)
Blob_str = "Blob Recognition"
Blob_str_size = image.string_size(Blob_str, scale=1.16)
text_size = Blob_str_size
# 计算字幕的居中位置
# x_str = (disp.width() - text_size[0]) // 2
x_str = (disp.width() - blob_learning_btn_img.width()) // 2
y_str = (exit_btn_pos[1])
# 计算学习按钮的居中位置
# Central_coordinate = {'x1':cam.width()//2, 'y1':cam.height()//4, 'x2':cam.width()//2, 'y2':cam.height()//4 * 3,
# 'x3':cam.width()//4, 'y3':cam.height()//2, 'x4':cam.width()//4 * 3, 'y4':cam.height()//2}

Central_coordinate = {'x1':cam.width()//2, 'y1':cam.height()//2 - 72//2, 'x2':cam.width()//2, 'y2':cam.height()//2 + 72//2,
'x3':cam.width()//2 - 72//2, 'y3':cam.height()//2, 'x4':cam.width()//2 + 72//2, 'y4':cam.height()//2}

# 定义表格的区域和样式
table_x, table_y = 52, disp.height() - 142 - disp_str_size
row_height = 32
table_width, table_height = 217, disp_str_size + (12 * 2)
block_height=row_height - 5
area_threshold = 1000
pixels_threshold = 1000
block_disp_size = (16, 16)
last_update_time = time.time_ms()
camera_flip_status = 0  # 镜头翻转状态
last_camera_flip_status = 0

closest_middle_rect = None # 最中间的方框
closest_middle_rect_min_distance = float('inf') # 距离中间最小的距离
leftmost_rect = None # 最靠左的方框
rightmost_rect = None # 最靠右的方框
topmost_rect = None # 最靠上的方框
bottommost_rect = None # 最靠下的方框
max_score = None # 最大的方框
min_score = None # 最小的方框

def set_bit(value, bit_position):
    """设置指定位置的位为1"""
    return value | (1 << bit_position)

def clear_bit(value, bit_position):
    """设置指定位置的位为0"""
    return value & ~(1 << bit_position)

def toggle_bit(value, bit_position):
    """切换指定位置的位（0变1，1变0）"""
    return value ^ (1 << bit_position)

def get_bit(value, bit_position):
    """获取指定位置的位"""
    return (value >> bit_position) & 1

def bitwise_and(value1, value2):
    """按位与操作"""
    return value1 & value2

def bitwise_or(value1, value2):
    """按位或操作"""
    return value1 | value2

def clear_all_bits():
    """清除所有位"""
    return 0x00

bit_value = 0b00000000  # 8位的0

def find_index(lst, value):
    try:
        index = lst.index(value)
        return index
    except ValueError:
        return -1  # 或者你可以返回None，表示未找到

# 转换 RGB 到 LAB 数值
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

        l = max(0, min(100, (116 * y) - 16 + 10))  # 增加明亮度的调整
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
            
def draw_lab_table():
    global _pressed, row_y, s_lab, s_rgb, thresholds, bt_count, img

    if img_min == None:
        return

    tmp_lab = s_lab
    tmp_rgb = s_rgb
    _pressed = True

    bt_count += 1
    if bt_count > 4:
        bt_count = 4
     # 绘制触摸点的 LAB 值到表格
    for idx, point in enumerate(touch_points):
        if idx >= 3:
            break
        x, y = point
        # 确保触摸点在图像的有效范围内
        # if table_width <= x <= 120+img.width() and 60 <= y <= 60+img.height(): 
        if bt_count == 4:
            if len(s_lab) > idx + 1:
                s_lab[idx][0] = tmp_lab[idx + 1][0]
                s_lab[idx][1] = tmp_lab[idx + 1][1]
                s_lab[idx][2] = tmp_lab[idx + 1][2]

                s_rgb[idx][0] = tmp_rgb[idx + 1][0]
                s_rgb[idx][1] = tmp_rgb[idx + 1][1]
                s_rgb[idx][2] = tmp_rgb[idx + 1][2]
                row_y[idx] = table_y + (idx * row_height) + (idx * disp_str_size)
                
        if len(touch_points) - 1 == idx:
            rgb = img_min.get_pixel(x, y, True)  # 修正相对位置
            if len(rgb) >= 3:  # 确保 rgb 列表有足够的元素
                s_rgb[idx][0], s_rgb[idx][1], s_rgb[idx][2] = round(rgb[0], 3), round(rgb[1], 3), round(rgb[2], 3)
                s_lab[idx][0], s_lab[idx][1], s_lab[idx][2] = rgb_to_lab(s_rgb[idx][0], s_rgb[idx][1], s_rgb[idx][2])

                row_y[idx] = table_y + (idx * row_height) + (idx * disp_str_size)

# 将坐标添加到颜色绘制
def add_xy_draw_color(x, y):
    touch_points.append((x, y))

    draw_lab_table() # 绘制 LAB 表格
    
    num = 0

    if len(touch_points) >= 3:
        num = 3
    else:
        num = len(touch_points)
    
    select_left_bottom_box(num)

# 存储最新的三个触点信息
touch_points = deque(maxlen=3)
s_lab = [0, 0, 0],[0, 0, 0],[0, 0, 0]
s_rgb = [0, 0, 0],[0, 0, 0],[0, 0, 0]
row_y = [-1, -1, -1]
rect_disp = 0
_pressed = bool(False)
bt_count = 0
thresholds_list_record = []
init_flag = bool(True)

COMPENSATIO = int(15)
L_COMPENSATIO = int(15)

count = 0
key_state = 0

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

def relevant_info_id_get(data, key):
    if key is not None:
        reply_data = data[key]
    else:
        reply_data = None
    return reply_data


# 保存数据函数
def save_to_binary(data):
    print("保存文件！！！")
    with open(save_path, 'wb') as f:
        pickle.dump(data, f)

# 加载数据函数
def load_from_binary():
    try:
        if os.path.exists(save_path):
            with open(save_path, 'rb') as f:
                return pickle.load(f)
        else:
            print(f"文件不存在，新建文件:{save_path}")
            data_storage = {
                's_lab': {},
                's_rgb': {}
            }
            save_to_binary(data_storage)
            return data_storage
    except Exception as e:
        print(e)
        data_storage = {
            's_lab': {},
            's_rgb': {}
        }
        save_to_binary(data_storage)
        return data_storage

# 检测文件是否存在，如果存在则加载数据，否则新建文件
s_data_storage = load_from_binary()

# 选中左下角哪个的按钮
def select_left_bottom_box(num):
    global rect_disp

    l_thresholds = {'L_min':fetch_range('L', 'min', int(s_lab[num - 1][0] - 15)), 'L_max':fetch_range('L', 'min', int(s_lab[num - 1][0] + 10))}
    a_thresholds = {'A_min':fetch_range('A', 'min', s_lab[num - 1][1] - COMPENSATIO), 'A_max':fetch_range('A', 'min',s_lab[num - 1][1] + COMPENSATIO)}
    b_thresholds = {'B_min':fetch_range('B', 'min', s_lab[num - 1][2] - COMPENSATIO), 'B_max':fetch_range('B', 'min',s_lab[num - 1][2] + COMPENSATIO)}
    rect_disp = num
    _thresholds = [[l_thresholds["L_min"], l_thresholds["L_max"], a_thresholds["A_min"], a_thresholds["A_max"], b_thresholds["B_min"], b_thresholds["B_max"]]]
    threshold_clear()
    thresholds.append(_thresholds)

# 选中右下角哪个按钮
def select_right_bottom(num):
    global rect_disp, bit_value, thresholds, thresholds_list_record

    index = num - 1

    if rect_disp <= 3:
        threshold_clear()

    if get_bit(bit_value, index) == 0:
        bit_value = set_bit(bit_value, index)
        l_thresholds = {'L_min':fetch_range('L', 'min', int(s_data_storage['s_lab'][index][0] - 15)), 'L_max':fetch_range('L', 'min', int(s_data_storage['s_lab'][index][0] + 10))}
        a_thresholds = {'A_min':fetch_range('A', 'min', s_data_storage['s_lab'][index][1] - COMPENSATIO), 'A_max':fetch_range('A', 'min',s_data_storage['s_lab'][index][1] + COMPENSATIO)}
        b_thresholds = {'B_min':fetch_range('B', 'min', s_data_storage['s_lab'][index][2] - COMPENSATIO), 'B_max':fetch_range('B', 'min',s_data_storage['s_lab'][index][2] + COMPENSATIO)}
        _thresholds = [[l_thresholds["L_min"], l_thresholds["L_max"], a_thresholds["A_min"], a_thresholds["A_max"], b_thresholds["B_min"], b_thresholds["B_max"]]]
        thresholds.append(_thresholds)
        thresholds_list_record.append(index)
    else:
        bit_value = clear_bit(bit_value, index)
        tmp_value = find_index(thresholds_list_record, index)
        # print(f'tmp_value:{tmp_value}')
        if tmp_value != -1:
            # print(f'thresholds:{thresholds}')
            thresholds.pop(tmp_value)
            thresholds_list_record.remove(index)
            # print(f'clear_thresholds:{thresholds}')

    rect_disp = num + 3

# 添加数据函数
def add_data(key = -1):
    global s_data_storage

    lab_value = copy.deepcopy(s_lab[rect_disp - 1])
    rgb_value = copy.deepcopy(s_rgb[rect_disp - 1])

    if len(s_data_storage['s_lab']) >= 7:
        print("存储已满，无法添加更多数据")
        return -1

    # 检查lab_value或rgb_value是否已经存在
    if lab_value in s_data_storage['s_lab'].values() or rgb_value in s_data_storage['s_rgb'].values():
        print("相同的lab_value或rgb_value已存在，无法添加数据")
        return -1

    if key != -1:
        if key in s_data_storage['s_lab']:
            print(f"键 {key} 已存在，无法添加数据")
            return -1
        s_data_storage['s_lab'][key] = lab_value
        s_data_storage['s_rgb'][key] = rgb_value
    else:
        # 检测不存在的key，并且优先存不存在最小key
        for i in range(7):
            if i not in s_data_storage['s_lab']:
                key = i
                print(f"不存在key:{key}，添加数据")
                break
        else:
            print("存储已满，无法添加更多数据")
            return -1
        
        s_data_storage['s_lab'][key] = lab_value
        s_data_storage['s_rgb'][key] = rgb_value
    
    save_to_binary(s_data_storage)
    select_right_bottom(key + 1)

# 判断是否删除色块数据和是否高亮右下角方框
def delete_data_and_draw_rect():
    global s_data_storage, rect_disp

    if img == None:
        return

    print('delete_data_and_draw_rect', rect_disp)
    
    for key in range(7):
        bit_value_tmp = get_bit(bit_value, key)

        # 如果右下角按钮是选中状态
        if bit_value_tmp == 1:
            if key in s_data_storage['s_rgb']:
                img.draw_image(learning_btn_pos[0] - 6, 6, forget_img)
                tmp_rect_coord = rect_coordinate[str(key)]
                tmp_coord = learned_coordinate[str(key)]
                min_color_rect = (tmp_coord[0] - 8 - 16, tmp_coord[1])

                img.draw_image(tmp_rect_coord[0], tmp_rect_coord[1], box_bg2_active_img)

                id_text = f'ID{key + 1}'
                id_text_scale = 1
                id_text_w, id_text_h = image.string_size(id_text, scale=id_text_scale)

                img.draw_string(tmp_coord[0], tmp_rect_coord[1] + box_bg2_img.height() // 2 - id_text_h // 2, id_text, image.COLOR_BLACK, scale=id_text_scale)

                color_rect_w = 16
                color_rect_h = 16

                img.draw_rect(min_color_rect[0], tmp_rect_coord[1] + box_bg2_img.height() // 2 - color_rect_h // 2, color_rect_w, color_rect_h, image.Color.from_rgb(s_data_storage['s_rgb'][key][0], s_data_storage['s_rgb'][key][1], s_data_storage['s_rgb'][key][2]), -1)
                
                if learning_flag == True:
                    if not os.path.exists(save_path):
                        print(f"文件不存在，无法删除数据")
                        return
                    
                    if key in s_data_storage['s_lab']:
                        del s_data_storage['s_lab'][key]
                    else:
                        print(f"键 {key} 不存在，无法删除数据")
                        return
                    if key in s_data_storage['s_rgb']:
                        del s_data_storage['s_rgb'][key]
                    else:
                        print(f"键 {key} 不存在，无法删除数据")
                        return
                            
                    save_to_binary(s_data_storage)

                    # 如果删除全部右下角的色块则重置选中状态
                    if len(s_data_storage['s_rgb']) <= 0:
                        rect_disp = 0
                        threshold_clear()
            else:
                threshold_clear()
        else:
            if key_state == 1:
                pass
    
# 检测文件是否存在，如果存在则加载数据，否则新建文件
s_data_storage = load_from_binary()

def area_compute(w, h):
    return w * h

def calculate_center_distance(element, screen_center_x, screen_center_y):
    # 计算矩形的中心点
    element_center_x = element['x'] + element['w'] / 2
    element_center_y = element['y'] + element['h'] / 2
    
    # 计算元素中心与屏幕中心的距离
    distance = ((element_center_x - screen_center_x) ** 2 + (element_center_y - screen_center_y) ** 2) ** 0.5
    
    return distance

def find_closest_to_screen_center(rectangles, screen_center_x = center_coordinate[0], screen_center_y = center_coordinate[1]):

    closest_rectangle_id = None
    min_distance = float('inf') # 初始距离设为无穷大
    
    for rectangle in rectangles:
        distance = round(calculate_center_distance(rectangle, screen_center_x, screen_center_y), 2)
        # print(f'----------rectangle:{rectangle}-----{distance}----{min_distance}------------')
        if distance < min_distance:
            min_distance = distance
            closest_rectangle_id = rectangle['id']
    
    return closest_rectangle_id

thresholds = []

def threshold_clear():
    global thresholds, thresholds_list_record, bit_value
    thresholds = []
    thresholds_list_record = []
    bit_value = clear_all_bits()

# 调整显示bitmap大小
def adjustment_ratio(img_max, img_min, x, y, w = -1, h = -1):
    return image.resize_map_pos_reverse(img_max.width(), img_max.height(), img_min.width(), img_min.height(), image.Fit.FIT_CONTAIN, x, y, w, h)


if len(s_data_storage['s_lab']) > 0:
    for i in range(7):
        if i in s_data_storage['s_lab']:
            l_thresholds = {'L_min':fetch_range('L', 'min', int(s_data_storage['s_lab'][i][0] - 15)), 'L_max':fetch_range('L', 'min', int(s_data_storage['s_lab'][i][0] + 10))}
            a_thresholds = {'A_min':fetch_range('A', 'min', s_data_storage['s_lab'][i][1] - COMPENSATIO), 'A_max':fetch_range('A', 'min',s_data_storage['s_lab'][i][1] + COMPENSATIO)}
            b_thresholds = {'B_min':fetch_range('B', 'min', s_data_storage['s_lab'][i][2] - COMPENSATIO), 'B_max':fetch_range('B', 'min',s_data_storage['s_lab'][i][2] + COMPENSATIO)}
            _thresholds = [[l_thresholds["L_min"], l_thresholds["L_max"], a_thresholds["A_min"], a_thresholds["A_max"], b_thresholds["B_min"], b_thresholds["B_max"]]]

            thresholds.append(_thresholds)
            thresholds_list_record.append(i)
            bit_value = set_bit(bit_value, i)
            rect_disp = 4

# del s_data_storage

g_blob_learn_index = []
g_spatial_attribute = {}
g_blob_id = []
g_learn_id = 0
g_blob_rect = [[]]
g_relevant_info_id = {
    'middle':0,
    'x_min':0,
    'x_max':0,
    'y_min':0,
    'y_max':0,
    'area_min':0,
    'area_max':0,
    "center_distance_min": 0,
    "center_distance_min_color1": 0,
    "center_distance_min_color2": 0,
    "center_distance_min_color3": 0,
    "center_distance_min_color4": 0,
    "center_distance_min_color5": 0,
    "center_distance_min_color6": 0,
    "center_distance_min_color7": 0,
}
g_blob_list = []

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

accept_thread = threading.Thread(target=socket_worker, daemon=True)
accept_thread.daemon = True
accept_thread.start()

# global last_camera_flip_status, camera_flip_status, img
# global g_blob_learn_index, g_spatial_attribute, g_blob_id, g_learn_id, g_blob_rect, g_relevant_info_id, g_blob_list

last_tick = time.time()

while not app.need_exit():
    if int(time.time()) - last_tick > 5:
        send_socket_message('PASS')
        last_tick = time.time()
    

    loop_start_time = time.ticks_ms()  # 记录循环开始时间

    relevant_info_data = {
        "area_max": 0,
        "area_min": float('inf'),
        "x_min": float('inf'),
        "x_max": float('-inf'),
        "y_min": float('inf'),
        "y_max": float('-inf'),
        "center_distance_min": float('inf'),
        "center_distance_min_color1": float('inf'),
        "center_distance_min_color2": float('inf'),
        "center_distance_min_color3": float('inf'),
        "center_distance_min_color4": float('inf'),
        "center_distance_min_color5": float('inf'),
        "center_distance_min_color6": float('inf'),
        "center_distance_min_color7": float('inf')
    }

    relevant_info_id = {
        "area_max": 0,
        "area_min": 0,
        "x_min": 0,
        "x_max": 0,
        "y_min": 0,
        "y_max": 0,
        "center_distance_min": 0,
        "center_distance_min_color1": 0,
        "center_distance_min_color2": 0,
        "center_distance_min_color3": 0,
        "center_distance_min_color4": 0,
        "center_distance_min_color5": 0,
        "center_distance_min_color6": 0,
        "center_distance_min_color7": 0,
    }

    spatial_attribute = {}
    blob_list = []

    if last_camera_flip_status != camera_flip_status:
        cam.vflip(camera_flip_status)
        cam_min.vflip(camera_flip_status)
        last_camera_flip_status = camera_flip_status
    img = cam.read()
    img_min = cam_min.read()
    blob_id = 0
    blob_id_list = []
    blob_rect = [[]]
    blob_learn_index = []

    # 处理触摸点和 LAB 值
    x, y, pressed = ts.read()
    current_time = time.time_ms()

    # 防误触设计，模拟用户按压屏幕松开后才触发
    if x != last_x or y != last_y or pressed != last_pressed:
        last_x = x
        last_y = y
        last_pressed = pressed
    if pressed:
        pressed_already = True
    elif pressed_already:
        pressed_already = False
        if (x, y) not in touch_points and (current_time - last_update_time > 50):
            if exit_btn_pos[0] < x < exit_btn_pos[2] and exit_btn_pos[1] < y < exit_btn_pos[3]:
                app.set_exit_flag(True)
                break
            elif row_y[0] != -1 and table_x - 40 < x < table_width + table_x and row_y[0] - 16 < y < row_y[0] + table_height:
                select_left_bottom_box(1)
            elif row_y[1] != -1 and table_x - 40 < x < table_width + table_x and row_y[1] - 16 < y < row_y[1] + table_height:
                select_left_bottom_box(2)
            elif row_y[2] != -1 and table_x - 40 < x < table_width + table_x and row_y[2] - 16 < y < row_y[2] + table_height:
                select_left_bottom_box(3)
            elif learning_btn_pos[0] < x < learning_btn_pos[2] + learning_btn_pos[0] and learning_btn_pos[1] < y < learning_btn_pos[3] + learning_btn_pos[1]:
                print('learning')
                learning_flag = True
            elif 0 in s_data_storage['s_lab'] and rect_coordinate['0'][0] < x < rect_coordinate['0'][2] + rect_coordinate['0'][0] and rect_coordinate['0'][1] < y < rect_coordinate['0'][3] + rect_coordinate['0'][1]:
                select_right_bottom(1)
            elif 1 in s_data_storage['s_lab'] and rect_coordinate['1'][0] < x < rect_coordinate['1'][2] + rect_coordinate['1'][0] and rect_coordinate['1'][1] < y < rect_coordinate['1'][3] + rect_coordinate['1'][1]:
                select_right_bottom(2)
            elif 2 in s_data_storage['s_lab'] and rect_coordinate['2'][0] < x < rect_coordinate['2'][2] + rect_coordinate['2'][0] and rect_coordinate['2'][1] < y < rect_coordinate['2'][3] + rect_coordinate['2'][1]:
                select_right_bottom(3)
            elif 3 in s_data_storage['s_lab'] and rect_coordinate['3'][0] < x < rect_coordinate['3'][2] + rect_coordinate['3'][0] and rect_coordinate['3'][1] < y < rect_coordinate['3'][3] + rect_coordinate['3'][1]:
                select_right_bottom(4)
            elif 4 in s_data_storage['s_lab'] and rect_coordinate['4'][0] < x < rect_coordinate['4'][2] + rect_coordinate['4'][0] and rect_coordinate['4'][1] < y < rect_coordinate['4'][3] + rect_coordinate['4'][1]:
                select_right_bottom(5)
            elif 5 in s_data_storage['s_lab'] and rect_coordinate['5'][0] < x < rect_coordinate['5'][2] + rect_coordinate['5'][0] and rect_coordinate['5'][1] < y < rect_coordinate['5'][3] + rect_coordinate['5'][1]:
                select_right_bottom(6)
            elif 6 in s_data_storage['s_lab'] and rect_coordinate['6'][0] < x < rect_coordinate['6'][2] + rect_coordinate['6'][0] and rect_coordinate['6'][1] < y < rect_coordinate['6'][3] + rect_coordinate['6'][1]:
                select_right_bottom(7)
            else:
                last_update_time = current_time
                add_xy_draw_color(x, y)
                
    with rect_disp_lock:
        for idx, point in enumerate(touch_points):
            x, y = point
            box_x = table_x - 40
            box_y = row_y[idx] - 12
            lab_color_x = box_x + 16

            img.draw_image(box_x, box_y, box_bg_img)
            img.draw_circle(x, y, 23 // 2, image.Color.from_rgb(s_rgb[idx][0], s_rgb[idx][1], s_rgb[idx][2]), -1)
            img.draw_circle(x, y, 23 // 2, image.Color.from_rgb(255, 255, 255), 2)
            img.draw_rect(lab_color_x, row_y[idx] + 3, block_disp_size[0], block_disp_size[1], image.Color.from_rgb(s_rgb[idx][0], s_rgb[idx][1], s_rgb[idx][2]),-1)

            lab_text_scale = 1
            lab_text = f"L {int(s_lab[idx][0])},"
            lab_text_w, lab_text_h = image.string_size(lab_text, scale=lab_text_scale)
            lab_text_x_base = lab_color_x + block_disp_size[0] + 8
            lab_text_y = box_y + box_bg_img.height() // 2 - lab_text_h // 2
            lab_text_color = image.Color.from_rgb(255, 255, 255)

            img.draw_string(lab_text_x_base, lab_text_y, lab_text, lab_text_color, scale=lab_text_scale)

            lab_text = f"A {int(s_lab[idx][1])},"

            img.draw_string(lab_text_x_base + 56, lab_text_y, lab_text, lab_text_color, scale=lab_text_scale)

            lab_text = f"B {int(s_lab[idx][2])}"
            
            img.draw_string(lab_text_x_base + 112, lab_text_y, lab_text, lab_text_color, scale=lab_text_scale)
        
        img.draw_image(exit_btn_pos[0], exit_btn_pos[1], exit_label)
        img.draw_image(x_str - 10, y_str + exit_label.height() // 2 - blob_learning_btn_img.height() // 2, blob_learning_btn_img)

        # 取dict key和value
        for _key, _value in s_data_storage['s_rgb'].items():
            tmp_coord = learned_coordinate[str(_key)]
            tmp_rect_coord = rect_coordinate[str(_key)]
            min_color_rect = (tmp_coord[0] - 8 - 16, tmp_coord[1])
            img.draw_image(tmp_rect_coord[0], tmp_rect_coord[1], box_bg2_img)

            id_text = f'ID{_key + 1}'
            id_text_scale = 1
            id_text_w, id_text_h = image.string_size(id_text, scale=id_text_scale)

            img.draw_string(tmp_coord[0], tmp_rect_coord[1] + box_bg2_img.height() // 2 - id_text_h // 2, id_text, image.COLOR_WHITE, scale=id_text_scale)

            color_rect_w = 16
            color_rect_h = 16

            img.draw_rect(min_color_rect[0], tmp_rect_coord[1] + box_bg2_img.height() // 2 - color_rect_h // 2, color_rect_w, color_rect_h, image.Color.from_rgb(s_data_storage['s_rgb'][_key][0], s_data_storage['s_rgb'][_key][1], s_data_storage['s_rgb'][_key][2]), -1)

        if rect_disp >= 1 and rect_disp <= 3:
            img.draw_image(table_x - 40, row_y[rect_disp - 1] - 12, box_border_img)
            img.draw_image(learning_btn_pos[0] - 6, 6, learning_img)
            
            if learning_flag:
                add_data()
                learning_flag = False

        elif rect_disp > 3:
            delete_data_and_draw_rect()
            learning_flag = False 

        with g_relevant_info_id_lock:
            for threshold_index in range(len(thresholds)):
                blobs = img_min.find_blobs(thresholds[threshold_index] , area_threshold = 1000, pixels_threshold = 1000)

                for b in blobs:
                    blob_id +=1
                    blob_id_list.append(blob_id)
                    mini_corners = b.mini_corners()
                    rect = b.rect()
                    # corners = b.corners()
                    # top_left_index = 0
                    for i in range(4):
                        x1, y1 = adjustment_ratio(img, img_min, mini_corners[i][0], mini_corners[i][1])
                        x2, y2 = adjustment_ratio(img, img_min, mini_corners[(i + 1) % 4][0], mini_corners[(i + 1) % 4][1])
                        img.draw_line(x1, y1, x2, y2, image.Color.from_rgb(9, 232, 50), 2)
                        # img.draw_line(mini_corners[i][0], mini_corners[i][1], mini_corners[(i + 1) % 4][0], mini_corners[(i + 1) % 4][1], image.COLOR_GREEN, 2)
                    x, y, w, h = adjustment_ratio(img, img_min, rect[0], rect[1], rect[2], rect[3])
                    if rect_disp > 3 and len(thresholds_list_record) > threshold_index:
                        id = thresholds_list_record[threshold_index] + 1
                        title_text = f'ID' + str(id)
                        title_text_w, title_text_h = image.string_size(title_text, scale=1)
                        rect_h = 30
                        rect_y = y - rect_h

                        img.draw_rect(x, rect_y, title_text_w + 20, rect_h, image.Color.from_rgb(9, 232, 50), -1)
                        img.draw_string(x + 10, rect_y + rect_h // 2 - title_text_h // 2, title_text, image.COLOR_WHITE, scale= 1)

                    data_storage_index = -1
                    
                    for key, value in s_data_storage['s_lab'].items():
                        l_thresholds = {'L_min':fetch_range('L', 'min', int(value[0] - 15)), 'L_max':fetch_range('L', 'min', int(value[0] + 10))}
                        a_thresholds = {'A_min':fetch_range('A', 'min', value[1] - COMPENSATIO), 'A_max':fetch_range('A', 'min',value[1] + COMPENSATIO)}
                        b_thresholds = {'B_min':fetch_range('B', 'min', value[2] - COMPENSATIO), 'B_max':fetch_range('B', 'min',value[2] + COMPENSATIO)}

                        # 找到当前这个颜色是存储在本地的第几个索引
                        if thresholds[threshold_index][0][0] == l_thresholds["L_min"] and thresholds[threshold_index][0][1] == l_thresholds["L_max"] and thresholds[threshold_index][0][2] == a_thresholds["A_min"] and thresholds[threshold_index][0][3] == a_thresholds["A_max"] and thresholds[threshold_index][0][4] == b_thresholds["B_min"] and thresholds[threshold_index][0][5] == b_thresholds["B_max"]:
                            data_storage_index = key
                            break

                    tmp_blob_rect = {'x':x, 'y':y, 'w':w, 'h':h, 'id':blob_id, 'index':threshold_index, "data_storage_index": data_storage_index}
                    blob_list.append(tmp_blob_rect)

                    if len(thresholds_list_record) > threshold_index:
                        blob_learn_index.append(thresholds_list_record[threshold_index] + 1)

                    spatial_attribute[str(blob_id)] = {'x':x+w//2, 'y':y+h//2, 'w':w, 'h':h, 'id':blob_id}

                    # 计算当前 blob 的面积
                    area = area_compute(w, h)

                    # 计算当前 blob 的中心点
                    blob_center_x = x + w // 2
                    blob_center_y = y + h // 2

                    # 计算当前 blob 中心点到图像中心点的距离
                    center_distance = math.sqrt((blob_center_x - image_center_x) ** 2 + (blob_center_y - image_center_y) ** 2)

                    # 更新最大面积
                    if area > relevant_info_data["area_max"]:
                        relevant_info_data["area_max"] = area
                        relevant_info_id["area_max"] = blob_id

                    # 更新最小面积
                    if area < relevant_info_data["area_min"]:
                        relevant_info_data["area_min"] = area
                        relevant_info_id["area_min"] = blob_id

                    # 更新 x 最小
                    if x < relevant_info_data["x_min"]:
                        relevant_info_data["x_min"] = x
                        relevant_info_id["x_min"] = blob_id
                    
                    print('relevant_info_data["x_max"]', relevant_info_data["x_max"], x + w)

                    # 更新 x 最大
                    if x + w > relevant_info_data["x_max"]:
                        print('x + w', x + w)
                        relevant_info_data["x_max"] = x + w
                        relevant_info_id["x_max"] = blob_id

                    # 更新 y 最小
                    if y < relevant_info_data["y_min"]:
                        relevant_info_data["y_min"] = y
                        relevant_info_id["y_min"] = blob_id

                    # 更新 y 最大
                    if y + h > relevant_info_data["y_max"]:
                        relevant_info_data["y_max"] = y + h
                        relevant_info_id["y_max"] = blob_id
                    
                    # 更新全部色块里面最靠中心的
                    if center_distance < relevant_info_data["center_distance_min"]:
                        relevant_info_data["center_distance_min"] = center_distance
                        relevant_info_id["center_distance_min"] = blob_id
                    
                    color_index = str(threshold_index + 1)
                    
                    # 更新颜色ID里面最靠中心的
                    if center_distance < relevant_info_data["center_distance_min_color" + color_index]:
                        relevant_info_data["center_distance_min_color" + color_index] = center_distance
                        relevant_info_id["center_distance_min_color" + color_index] = blob_id
                
                # 打印结果（可选）
                # print("Area Max ID:", relevant_info_id["area_max"])
                # print("Area Min ID:", relevant_info_id["area_min"])
                # print("Leftmost ID:", relevant_info_id["x_min"])
                # print("Rightmost ID:", relevant_info_id["x_max"])
                # print("Topmost ID:", relevant_info_id["y_min"])
                # print("Bottommost ID:", relevant_info_id["y_max"])
                # print("Closest to Center ID:", relevant_info_id["center_distance_min"])
                g_blob_learn_index = blob_learn_index

                g_blob_rect = blob_rect
                g_blob_id = blob_id_list
                g_spatial_attribute = spatial_attribute
                g_relevant_info_id = relevant_info_id
                g_blob_list = blob_list

    img.draw_line(Central_coordinate["x3"],Central_coordinate["y3"], Central_coordinate["x4"], Central_coordinate["y4"], image.Color.from_rgb(255, 255, 255), 2)
    img.draw_line(Central_coordinate["x1"],Central_coordinate["y1"], Central_coordinate["x2"], Central_coordinate["y2"], image.Color.from_rgb(255, 255, 255), 2)

    with display_show_lock:
        show_loading = False
        disp.show(img) # 显示到屏幕

    # handler_socket_message("get_color_space_info\n8\n1")
    
    # 记录循环结束时间，统计循环性能
    # loop_end_time = time.ticks_ms()
    # loop_execution_time = loop_end_time - loop_start_time
    # time_tracker.record_time(loop_execution_time)

    time.sleep_ms(10) # 休眠一些时间来释放一些CPU使用