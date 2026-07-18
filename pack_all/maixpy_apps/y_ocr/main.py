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

def on_user_key(key_id, state):
    '''
        this func called in a single thread
    '''
    print(f"key: {key_id}, state: {state}") # key.c or key.State.KEY_RELEASED

key_obj = key.Key(on_user_key)

from maix import camera, nn, app, touchscreen, pinmap, gpio
import math
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

lFrameResult = []
ts = touchscreen.TouchScreen()
pressed_flag = [False, False]
margin = 12

################
# Model Initial
################
model = "/root/models/pp_ocr.mud"
ocr = nn.PP_OCR(model)
cam_disp = camera.Camera(disp.width(), disp.height(), image.Format.FMT_RGBA8888)
cam = cam_disp.add_channel(ocr.input_width(), ocr.input_height(), ocr.input_format())


scale_x = disp.width() / cam.width()
scale_y = disp.height() / cam.height()


def set_camera_flip(state):
    global cam_flip_init
    cam_disp.vflip(state)
    # cam_disp.hmirror(state)
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


IS_REVERSE_WH = False
if IS_REVERSE_WH:
    RESOLUTION = (disp.height(), disp.width())
else:
    RESOLUTION = (disp.width(), disp.height())

text_offset_x = -4

exit_btn_img = image.load('./assets/images/back_btn.png', image.Format.FMT_RGBA8888)
OCR_btn_img = image.load('./assets/images/OCR.png', image.Format.FMT_RGBA8888)

exit_pos_x = margin
exit_pos_y = margin
exit_pos_w = exit_btn_img.width()
exit_pos_h = exit_btn_img.height()
exit_btn_pos = [exit_pos_x, exit_pos_y, exit_pos_w, exit_pos_h]
exit_btn_pos2 = image.resize_map_pos(ocr.input_width(), ocr.input_height(), disp.width(), disp.height(), image.Fit.FIT_CONTAIN, exit_btn_pos[0], exit_btn_pos[1], exit_btn_pos[2], exit_btn_pos[3])

ocr_label = "OCR"
ocr_pos_w = OCR_btn_img.width()
ocr_pos_h = OCR_btn_img.height()
ocr_pos_x = int(disp.width() /2) - int(ocr_pos_w / 2)
ocr_pos_y = 2* margin
ocr_btn_pos = [ocr_pos_x, ocr_pos_y, ocr_pos_w, ocr_pos_h]
ocr_btn_pos2 = image.resize_map_pos(ocr.input_width(), ocr.input_height(), disp.width(), disp.height(), image.Fit.FIT_CONTAIN, ocr_btn_pos[0], ocr_btn_pos[1], ocr_btn_pos[2], ocr_btn_pos[3])


ocr_text_size = image.string_size(ocr_label)
ocr_text_pos_x = ocr_pos_x + int(ocr_pos_w / 2) - int(ocr_text_size.width() / 2) + text_offset_x
ocr_text_pos_y = ocr_pos_y + int(ocr_pos_h / 2) - int(ocr_text_size.height() / 2)

def is_in_button(x, y, btn_pos):
    return x > btn_pos[0] and x < btn_pos[0] + btn_pos[2] and y > btn_pos[1] and y < btn_pos[1] + btn_pos[3]

def on_clicked_ocr():
    pass

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

    if cmds[0] == "get_ocr_info":
        position_type = cmds[1]
        value_type = cmds[2]
        if position_type == "1":
            result = get_result_by_other(mode="center", results=lFrameResult)
            if result:
                if value_type == "1":
                    value = f"get_ocr_info\nfull\n{result.x}"
                    send_socket_message(value)
                elif value_type == "2":
                    value = f"get_ocr_info\nfull\n{result.y}"
                    send_socket_message(value)
                elif value_type == "3":
                    value = f"get_ocr_info\nfull\n{result.w}"
                    send_socket_message(value)
                elif value_type == "4":
                    value = f"get_ocr_info\nfull\n{result.h}"
                    send_socket_message(value)
            else:
                value = f"get_ocr_info\nempty"
                send_socket_message(value)

        elif position_type == "2":
            result = get_result_by_other(mode="leftmost", results=lFrameResult)
            if result:
                if value_type == "1":
                    value = f"get_ocr_info\nfull\n{result.x}"
                    send_socket_message(value)
                elif value_type == "2":
                    value = f"get_ocr_info\nfull\n{result.y}"
                    send_socket_message(value)
                elif value_type == "3":
                    value = f"get_ocr_info\nfull\n{result.w}"
                    send_socket_message(value)
                elif value_type == "4":
                    value = f"get_ocr_info\nfull\n{result.h}"
                    send_socket_message(value)
            else:
                value = f"get_ocr_info\nempty"
                send_socket_message(value)

        elif position_type == "3":
            result = get_result_by_other(mode="rightmost", results=lFrameResult)
            if result:
                if value_type == "1":
                    value = f"get_ocr_info\nfull\n{result.x}"
                    send_socket_message(value)
                elif value_type == "2":
                    value = f"get_ocr_info\nfull\n{result.y}"
                    send_socket_message(value)
                elif value_type == "3":
                    value = f"get_ocr_info\nfull\n{result.w}"
                    send_socket_message(value)
                elif value_type == "4":
                    value = f"get_ocr_info\nfull\n{result.h}"
                    send_socket_message(value)
            else:
                value = f"get_ocr_info\nempty"
                send_socket_message(value)
        
        elif position_type == "4":
            result = get_result_by_other(mode="topmost", results=lFrameResult)
            if result:
                if value_type == "1":
                    value = f"get_ocr_info\nfull\n{result.x}"
                    send_socket_message(value)
                elif value_type == "2":
                    value = f"get_ocr_info\nfull\n{result.y}"
                    send_socket_message(value)
                elif value_type == "3":
                    value = f"get_ocr_info\nfull\n{result.w}"
                    send_socket_message(value)
                elif value_type == "4":
                    value = f"get_ocr_info\nfull\n{result.h}"
                    send_socket_message(value)
            else:
                value = f"get_ocr_info\nempty"
                send_socket_message(value)
        
        elif position_type == "5":
            result = get_result_by_other(mode="bottommost", results=lFrameResult)
            if result:
                if value_type == "1":
                    value = f"get_ocr_info\nfull\n{result.x}"
                    send_socket_message(value)
                elif value_type == "2":
                    value = f"get_ocr_info\nfull\n{result.y}"
                    send_socket_message(value)
                elif value_type == "3":
                    value = f"get_ocr_info\nfull\n{result.w}"
                    send_socket_message(value)
                elif value_type == "4":
                    value = f"get_ocr_info\nfull\n{result.h}"
                    send_socket_message(value)
            else:
                value = f"get_ocr_info\nempty"
                send_socket_message(value)

        elif position_type == "6":
            result = get_result_by_other(mode="area_max", results=lFrameResult)
            if result:
                if value_type == "1":
                    value = f"get_ocr_info\nfull\n{result.x}"
                    send_socket_message(value)
                elif value_type == "2":
                    value = f"get_ocr_info\nfull\n{result.y}"
                    send_socket_message(value)
                elif value_type == "3":
                    value = f"get_ocr_info\nfull\n{result.w}"
                    send_socket_message(value)
                elif value_type == "4":
                    value = f"get_ocr_info\nfull\n{result.h}"
                    send_socket_message(value)
            else:
                value = f"get_ocr_info\nempty"
                send_socket_message(value)

        elif position_type == "7":
            result = get_result_by_other(mode="area_min", results=lFrameResult)
            if result:
                if value_type == "1":
                    value = f"get_ocr_info\nfull\n{result.x}"
                    send_socket_message(value)
                elif value_type == "2":
                    value = f"get_ocr_info\nfull\n{result.y}"
                    send_socket_message(value)
                elif value_type == "3":
                    value = f"get_ocr_info\nfull\n{result.w}"
                    send_socket_message(value)
                elif value_type == "4":
                    value = f"get_ocr_info\nfull\n{result.h}"
                    send_socket_message(value)
            else:
                value = f"get_ocr_info\nempty"
                send_socket_message(value)

    elif cmds[0] == "get_ocr_text":
        position_type = cmds[1]
        if position_type == "1":
            result = get_result_by_other(mode="center", results=lFrameResult)
            if result:
                value = f"get_ocr_text\nfull\n{result.content}"
                send_socket_message(value)
            else:
                value = f"get_ocr_text\nempty"
                send_socket_message(value)

        elif position_type == "2":
            result = get_result_by_other(mode="leftmost", results=lFrameResult)
            if result:
                value = f"get_ocr_text\nfull\n{result.content}"
                send_socket_message(value)
            else:
                value = f"get_ocr_text\nempty"
                send_socket_message(value)

        elif position_type == "3":
            result = get_result_by_other(mode="rightmost", results=lFrameResult)
            if result:
                value = f"get_ocr_text\nfull\n{result.content}"
                send_socket_message(value)
            else:
                value = f"get_ocr_text\nempty"
                send_socket_message(value)
        
        elif position_type == "4":
            result = get_result_by_other(mode="topmost", results=lFrameResult)
            if result:
                value = f"get_ocr_text\nfull\n{result.content}"
                send_socket_message(value)
            else:
                value = f"get_ocr_text\nempty"
                send_socket_message(value)
        
        elif position_type == "5":
            result = get_result_by_other(mode="bottommost", results=lFrameResult)
            if result:
                value = f"get_ocr_text\nfull\n{result.content}"
                send_socket_message(value)
            else:
                value = f"get_ocr_text\nempty"
                send_socket_message(value)
        elif position_type == "6":
            result = get_result_by_other(mode="area_max", results=lFrameResult)
            if result:
                value = f"get_ocr_text\nfull\n{result.content}"
                send_socket_message(value)
            else:
                value = f"get_ocr_text\nempty"
                send_socket_message(value)

        elif position_type == "7":
            result = get_result_by_other(mode="area_min", results=lFrameResult)
            if result:
                value = f"get_ocr_text\nfull\n{result.content}"
                send_socket_message(value)
            else:
                value = f"get_ocr_text\nempty"
                send_socket_message(value)

    elif cmds[0] == "get_ocr_info_by_text":
        text_content = cmds[1]
        value_type = cmds[2]
        text = get_result_by_content(text_content)
        if text:
            if value_type == "1":
                value = f"get_ocr_info_by_text\nfull\n{text.x}"
                send_socket_message(value)

            elif value_type == "2":
                value = f"get_ocr_info_by_text\nfull\n{text.y}"
                send_socket_message(value)

            elif value_type == "3":
                value = f"get_ocr_info_by_text\nfull\n{text.w}"
                send_socket_message(value)

            elif value_type == "4":
                value = f"get_ocr_info_by_text\nfull\n{text.h}"
                send_socket_message(value)

        else:
            value = f"get_ocr_info_by_text\nempty"
            send_socket_message(value)

        
# def calculate_xywh(points):
#     x_coords = [points[i] for i in range(0, len(points), 2)]
#     y_coords = [points[i+1] for i in range(0, len(points), 2)]
    
#     x_min = min(x_coords)
#     y_min = min(y_coords)
#     x_max = max(x_coords)
#     y_max = max(y_coords)
    
#     width = x_max - x_min
#     height = y_max - y_min
    
#     return (int(width / 2) + x_min, int(height / 2) + y_min, width, height)

def calculate_xywh(points, cam_width, cam_height, disp_width, disp_height):
    # 提取x和y坐标
    x_coords = [points[i] for i in range(0, len(points), 2)]
    y_coords = [points[i+1] for i in range(0, len(points), 2)]
    
    # 计算边界框的最小和最大坐标
    x_min = min(x_coords)
    y_min = min(y_coords)
    x_max = max(x_coords)
    y_max = max(y_coords)
    
    # 计算边界框的宽度和高度
    width = x_max - x_min
    height = y_max - y_min
    
    # 计算中心点坐标
    center_x = int(width / 2) + x_min
    center_y = int(height / 2) + y_min
    
    return (center_x, center_y, width, height)

class Text:
    def __init__(self, x, y, w, h, conf, content) -> None:
        self.x = x
        self.y = y
        self.w = w
        self.h = h
        self.conf = conf
        self.content = content

class FrameResult:
    def __init__(self, time_stamp, texts: list[Text]) -> None:
        self.time_stamp = time_stamp
        self.texts = texts

def scale_points(coords):
    scaled_coords = []
    for i in range(0, len(coords), 2):
        x = round(coords[i] * scale_x)
        y = round(coords[i+1] * scale_y)
        scaled_coords.extend([x, y])
    return scaled_coords


def ocr_mode1(img, save_length: int):
    objs = ocr.detect(img)
    time_stamp = int(time.time())
    lResult = []
    for obj in objs:
        points = scale_points(obj.box.to_list())
        xywh = calculate_xywh(points, cam.width(), cam.height(), disp.width(), disp.height())
        text = Text(
            x=xywh[0],
            y=xywh[1],
            w=xywh[2],
            h=xywh[3],
            conf=None,
            content=None,
        )
        img.draw_keypoints(points, image.COLOR_RED, 4, -1, 1)
        print(text.x, text.y)
        lResult.append(text)

    frame_result = FrameResult(
        time_stamp=time_stamp,
        texts=lResult
    )
    lFrameResult.append(frame_result)

    if len(lFrameResult) > save_length:
        del lFrameResult[0]

    

def ocr_mode2(img, disp_img, save_length: int):
    objs = ocr.detect(img)
    time_stamp = int(time.time())
    lResult = []

    for obj in objs:
        print(obj.box.y1, obj.box.y2, obj.box.y3, obj.box.y4)
        points = scale_points(obj.box.to_list())
        xywh = calculate_xywh(points, cam.width(), cam.height(), disp.width(), disp.height())
        text = Text(
            x=xywh[0],
            y=xywh[1],
            w=xywh[2],
            h=xywh[3],
            conf=None,
            content=obj.char_str(),
        )
        str_w, str_h = image.string_size(obj.char_str(), scale=1.5)
        disp_img.draw_rect(round(obj.box.x4 * scale_x), round(obj.box.y1 * scale_y) - str_h - 8 - 6, str_w + 16, str_h + 16, image.Color.from_rgb(88,182,124), thickness=-1)
        disp_img.draw_string(round(obj.box.x4 * scale_x) + 8, round(obj.box.y1 * scale_y) - str_h - 6, obj.char_str(), image.COLOR_WHITE, scale=1.5)
        disp_img.draw_keypoints(points, image.Color.from_rgb(88,182,124), 4, -1, 2)
        lResult.append(text)
        print(text.x, text.y)

    frame_result = FrameResult(
        time_stamp=time_stamp,
        texts=lResult
    )
    lFrameResult.append(frame_result)

    if len(lFrameResult) > save_length:
        del lFrameResult[0]

def get_result_by_frame(frame_no):
    return lFrameResult[-(frame_no + 1)]

def get_result_by_content(text_content):
    frame_result = get_result_by_frame(0)
    matching_texts = []
    for text in frame_result.texts:
        if text.content == text_content:
            matching_texts.append(text)
    
    if not matching_texts:
        return None
    
    # 如果只有一个匹配的文字，直接返回
    if len(matching_texts) == 1:
        return matching_texts[0]
    
    # 如果有多个匹配的文字，返回最靠近屏幕中心的
    center_x = RESOLUTION[0] / 2
    center_y = RESOLUTION[1] / 2
    
    closest_text = None
    min_distance = float('inf')
    
    for text in matching_texts:
        distance = math.sqrt((text.x - center_x) ** 2 + (text.y - center_y) ** 2)
        if distance < min_distance:
            min_distance = distance
            closest_text = text
    
    return closest_text

def get_result_by_other(mode, results):
    if mode == "area_max":
        areas = []
        for result in results:
            frame_result = result.texts
            for result_indiv in frame_result:
                area = result_indiv.w * result_indiv.h
                area_dict = {"text": result_indiv, "area": area}
                areas.append(area_dict)

        if areas:
            max_item = max(areas, key=lambda x: x['area'])
            return max_item["text"]
        else:
            return None
    
    elif mode == "area_min":
        areas = []
        for result in results:
            frame_result = result.texts
            for result_indvi in frame_result:
                area = result_indvi.w * result_indvi.h
                area_dict = {"text": result_indvi, "area": area}
                areas.append(area_dict)

        if areas:
            min_item = min(areas, key=lambda x: x['area']) 
            return min_item["text"]
        else:
            return None
    
    
    elif mode == "leftmost":
        orientations = []
        for result in results:
            frame_result = result.texts
            for result_indvi in frame_result:
                area = result_indvi.x
                area_dict = {"text": result_indvi, "x": area}
                orientations.append(area_dict)
        if orientations:
            min_item = min(orientations, key=lambda x: x['x'])
            return min_item["text"]
        else:
            return None
    
    elif mode == "rightmost":
        orientations = []
        for result in results:
            frame_result = result.texts
            for result_indvi in frame_result:
                area = result_indvi.x
                area_dict = {"text": result_indvi, "x": area}
                orientations.append(area_dict)
        if orientations:
            max_item = max(orientations, key=lambda x: x['x'])
            return max_item["text"]
        else:
            return None
    
    elif mode == "topmost":
        orientations = []
        for result in results:
            frame_result = result.texts
            for result_indvi in frame_result:
                area = result_indvi.y
                area_dict = {"text": result_indvi, "y": area}
                orientations.append(area_dict)
        if orientations:
            min_item = min(orientations, key=lambda x: x['y'])
            return min_item["text"]
        else:
            return None
    
    elif mode == "bottommost":
        orientations = []
        for result in results:
            frame_result = result.texts
            for result_indvi in frame_result:
                area = result_indvi.y
                area_dict = {"text": result_indvi, "y": area}
                orientations.append(area_dict)
        if orientations:
            max_item = max(orientations, key=lambda x: x['y'])
            return max_item["text"]
        else:
            return None
    
    elif mode == "center":
        orientations = []
        center_x = RESOLUTION[0] / 2
        center_y = RESOLUTION[1] / 2 
        for result in results:
            frame_result = result.texts
            for result_indvi in frame_result:
                x = result_indvi.x
                y = result_indvi.y
                distance = math.sqrt((x - center_x) ** 2 + (y - center_y) ** 2)
                area_dict = {"text": result_indvi, "distance": distance}
                orientations.append(area_dict)

        if orientations:
            closest_item = min(orientations, key=lambda x: x['distance'])
            return closest_item["text"]
        else:
            return None
    
    
    elif mode == "highest_confidence":
        return None
    
    elif mode == "lowest_confidence":
        return None

def main(disp):
    global show_loading, last_x, last_y, last_pressed, pressed_already

    last_tick = time.time()
    arrowLeft = image.load("./assets/images/ArrowLeft_32_32.png", format=image.Format.FMT_RGBA8888)
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
                on_clicked_ocr()

        disp_img = cam_disp.read()
        img = cam.read()
        ocr_mode2(img, disp_img, 5)
        # img_display = image.Image(disp.width(), disp.height(), image.Format.FMT_RGBA8888)
        # img_display.draw_image(0, 0, img.resize(disp.width(), disp.height()))

        # disp_img.draw_rect(exit_btn_pos[0], exit_btn_pos[1], exit_btn_pos[2], exit_btn_pos[3],  image.Color(0,0,0,0.2, image.Format.FMT_RGBA8888), -1)
        disp_img.draw_image(exit_btn_pos[0], exit_btn_pos[1], exit_btn_img)
        # disp_img.draw_rect(ocr_btn_pos[0], ocr_btn_pos[1], ocr_btn_pos[2], ocr_btn_pos[3],  image.Color(0,0,0,0.8,image.Format.FMT_RGBA8888), -1)
        disp_img.draw_image(ocr_btn_pos[0], ocr_btn_pos[1], OCR_btn_img)
        
        with display_show_lock:
            show_loading = False
            disp.show(disp_img) # 显示到屏幕
        
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
        time.sleep(0.1)

if client_socket:
    client_socket.close()