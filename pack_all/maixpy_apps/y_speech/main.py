from maix import display, image, app, time, touchscreen, key, pinmap, gpio
from utils import set_local_storage, get_local_storage
import select, asyncio, threading, socket
from delete_popup import DeletePopup
from asr_on_line_client import AsrOnlineClient

speech_init_flag = False

disp = display.Display()

disp_image = image.Image(disp.width(), disp.height(), image.Format.FMT_RGBA8888)

disp_image.draw_string(24,24,'Loading...', image.COLOR_WHITE, scale=4.8, thickness=3)

disp.show(disp_image)

import os

os.system("python /mk/m_build_core/m_build_speech_service.py &") # 启动中文语音识别服务

delete_popup = DeletePopup(disp) # 删除确认弹窗
show_delete_popup = False
tmp_delete = None
save_path = "/mk/speech_recognition.pkl"
ts = touchscreen.TouchScreen()
current_tab = 0
touch_end = False

font_size = 20
image.load_font("sourcehansans", "/maixapp/share/font/SourceHanSansCN-Regular.otf", size = font_size)
print("fonts:", image.fonts())
image.set_default_font("sourcehansans")

def get_scale(px):
    return px / font_size

sound_icon = image.load('./assets/images/sound.png', image.Format.FMT_RGBA8888)
check_icon = image.load('./assets/images/check.png', image.Format.FMT_RGBA8888)
list_icon = image.load('./assets/images/list.png', image.Format.FMT_RGBA8888)
edit_icon = image.load('./assets/images/edit.png', image.Format.FMT_RGBA8888)
back_icon = image.load('./assets/images/back.png', image.Format.FMT_RGBA8888)
del_icon = image.load('./assets/images/del.png', image.Format.FMT_RGBA8888)
chinese_btn_active_img = image.load('./assets/images/chinese_btn_active.png', image.Format.FMT_RGBA8888)
chinese_btn_img = image.load('./assets/images/chinese_btn.png', image.Format.FMT_RGBA8888)
chinese_btn_img_xywh = [98, 18, chinese_btn_img.width(), chinese_btn_img.height()]
english_btn_active_img = image.load('./assets/images/english_btn_active.png', image.Format.FMT_RGBA8888)
english_btn_img = image.load('./assets/images/english_btn.png', image.Format.FMT_RGBA8888)
english_btn_img_xywh = [248, 18, english_btn_img.width(), english_btn_img.height()]
english_btn = image.load('./assets/images/english_btn.png', image.Format.FMT_RGBA8888)
keywords_img = image.load('./assets/images/keywords.png', image.Format.FMT_RGBA8888)

icon_center = [round(disp.width() / 2) - round(sound_icon.width() / 2), round(disp.height() / 2) - round(sound_icon.height() / 2) - 32]
check_icon_xywh = [round(disp.width() / 2) - round(sound_icon.width() / 2), round(disp.height() / 2) - round(sound_icon.height() / 2) - 76, check_icon.width(), check_icon.height()]

list_xywh = [round(disp.width() - list_icon.width() - 4), round(disp.height() - list_icon.height() - 6), list_icon.width(), list_icon.height()]
edit_xywh = [round(disp.width() - edit_icon.width() - 4), 6, edit_icon.width(), edit_icon.height()]
back_xywh = [6, 6, back_icon.width(), back_icon.height()]

speech_recognition_english_xywh = [chinese_btn_img_xywh[0] + chinese_btn_img_xywh[2], chinese_btn_img_xywh[1] + chinese_btn_img_xywh[3] // 2, chinese_btn_img.width(), chinese_btn_img.height()]

SERVER_ADDR = "117.50.34.249"
SERVER_PORT = 6006

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
    global rec_display, back, mode, rect_list, switching_mode, rect_id
    
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
                    mode = 1
                    rect_list = []
                    rect_id = 0
                    switching_mode = True
                key_down_count += 1
            elif key_swith_left.value() == 0:
                # print(f'key_swith_left:{key_down_count}')
                if key_down_count == 2:
                    print('d-1')
                    mode = 0
                    rect_list = []
                    rect_id = 0
                    switching_mode = True
                key_down_count += 1
            else:
                key_down_count = 0
        time.sleep_ms(10)

key_thread = threading.Thread(target=key_scan)
key_thread.daemon = True
key_thread.start()

def on_user_key(key_id, state):
    '''
        this func called in a single thread
    '''
    print(f"key: {key_id}, state: {state}") # key.c or key.State.KEY_RELEASED
    if state == 1:
        handler_add_keyword()

key_obj = key.Key(on_user_key)

# Unix 域套接字路径
socket_path = "/tmp/my_socket"

client_socket = None
client_socket_status = False

socket_rx_string_buffer = ''

def send_socket_message(message):
    try:
        message_packet = f"@#{message}#@"
        print('[发送成功]\n', message_packet)
        if client_socket:
            client_socket.sendall(message_packet.encode())
    except Exception as e:
        print(e)


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

last_main_text_buffer = ''
main_text_buffer = ''

first_text_at = time.time()

def search_last_keyword():
    # 清理文本，去除空格和标点符号
    full_text = main_text_buffer.replace(" ", "").replace(",", " ").replace(".", " ")
    
    # 获取关键词列表
    keyword_list = get_keyword_list()
    
    # 初始化变量，用于记录最右边的匹配关键词
    last_matched_keyword = None
    last_matched_index = -1
    
    # 遍历关键词列表
    for keyword in keyword_list:
        # 获取关键词的单词
        word = keyword['word']
        
        # 检查关键词是否在 full_text 中
        index = full_text.rfind(word)
        
        # 如果找到并且位置比之前记录的位置更靠右，则更新记录
        if index != -1 and index > last_matched_index:
            last_matched_keyword = keyword
            last_matched_index = index
    
    # 返回最右边的匹配关键词
    return last_matched_keyword

def get_speech_id_by_keyword(keyword):
    # 获取关键词列表
    keyword_list = get_keyword_list()
    
    # 遍历关键词列表，查找匹配的关键词并返回对应的 ID
    for item in keyword_list:
        if item["word"].lower() == keyword.lower():
            send_socket_message(f'get_speech_id_by_keyword\nfull\n{item["id"]}')
            return
    
    send_socket_message(f'get_speech_id_by_keyword\nempty\n')

def get_speech_keyword_by_id(id):
    # 获取关键词列表
    keyword_list = get_keyword_list()
    
    # 遍历关键词列表，查找匹配的 ID 并返回对应的关键词
    for item in keyword_list:
        if str(item["id"]) == id:
            send_socket_message(f'get_speech_keyword_by_id\nfull\n{item["word"]}')
            return

    send_socket_message(f'get_speech_keyword_by_id\nempty\n')
    

def get_speech_keyword_count():
    count = len(get_keyword_list())
    send_socket_message(f'get_speech_keyword_count\nfull\n{count}')

def get_speech_result():
    full_text = main_text_buffer.replace(" ", "").replace(",", " ").replace(".", " ")
    if full_text.endswith(" "):
        full_text = full_text[:-1]

    if len(full_text) > 0:
        send_socket_message(f'get_speech_result\nfull\n{full_text}')
    else:
        send_socket_message(f'get_speech_result\nempty\n')

def get_speech_keyword_is_trigger():
    res = search_last_keyword()

    print("LastKeyword", res)

    if res:
        send_socket_message(f'get_speech_keyword_is_trigger\nfull\n1')
    else:
        send_socket_message(f'get_speech_keyword_is_trigger\nfull\n0')


def set_speech_keyword(keyword):
    handler_add_keyword(keyword)

def handler_socket_message(message):
    global speech_init_flag
    global main_text_buffer, last_main_text_buffer, first_text_at
    cmds = message.split("\n")

    if cmds[0] == 'global_set_speech_text':
        speech_init_flag = True
        main_text_buffer = cmds[1][-35:]
        if last_main_text_buffer == '' and main_text_buffer != '':
            first_text_at = time.time()
        last_main_text_buffer = main_text_buffer
    elif cmds[0] == 'get_speech_id_by_keyword':
        get_speech_id_by_keyword(cmds[1])
    elif cmds[0] == 'get_speech_keyword_by_id':
        get_speech_keyword_by_id(cmds[1])
    elif cmds[0] == 'get_speech_keyword_count':
        get_speech_keyword_count()
    elif cmds[0] == 'get_speech_result':
        get_speech_result()
    elif cmds[0] == 'get_speech_keyword_is_trigger':
        get_speech_keyword_is_trigger()
    elif cmds[0] == 'set_speech_keyword':
        set_speech_keyword(cmds[1])

def get_text_buffer():
    all_text = main_text_buffer.replace(" ", "").replace(",", " ").replace(".", "\u3000")
    if all_text.endswith("\u3000") or all_text.endswith(" "):
        all_text = all_text[:-1]
        
    if len(all_text) > 0:
        return all_text
    else:
        return ''

show_mode = 'default'

def switch_default():
    global show_mode
    time.sleep_ms(1500)
    show_mode = 'default'

keyword_list = []
local_storage_data = get_local_storage(save_path)

if isinstance(local_storage_data, list) and len(local_storage_data):
    keyword_list = local_storage_data

def get_keywords():
    words = [item['word'] for item in keyword_list]
    return words

def get_keyword_list():
    return keyword_list

def clear_keywords():
    global keyword_list
    keyword_list = []

def delete_keyword(id):
    for item in keyword_list:
        if item["id"] == id:
            keyword_list.remove(item)
            set_local_storage(save_path, keyword_list)
            break


def handler_add_keyword(keyword = None):
    global show_mode
    if len(main_text_buffer) > 0 or keyword is not None:
        if keyword == None:
            word_str = main_text_buffer.replace(" ", "").replace(",", ".")
            word_list = word_str.split('.')
            # 去除空白字符串的成员
            word_list = [s for s in word_list if s]
            
            # 获取最后一个元素
            last_word = word_list[-1]

            print("要添加的关键词",  word_str, word_list)

            if len(last_word) == 0:
                return
        else:
            last_word = keyword
        
        # 检查是否已经存在相同的 word
        existing_word = next((item for item in keyword_list if item["word"] == last_word), None)
        
        if not existing_word:
            # 找到最小的可用 id
            used_ids = {item["id"] for item in keyword_list}
            min_id = 1
            while min_id in used_ids:
                min_id += 1
            
            # 将新元素添加到 keyword_list
            keyword_list.append({"id": min_id, "word": last_word})
            show_mode = 'add_success'
            threading.Thread(target=switch_default).start()
            set_local_storage(save_path, keyword_list)

    print(keyword_list)

def handler_open_list():
    global show_mode
    show_mode = 'list_dialog'

def is_in_button(x, y, btn_pos):
    return x > btn_pos[0] and x < btn_pos[0] + btn_pos[2] and y > btn_pos[1] and y < btn_pos[1] + btn_pos[3]

pressed_flag = [False, False, False]
del_lock = False

def hadnler_touch(x, y, pressed):
    global pressed_flag, show_mode, del_lock, show_delete_popup, tmp_delete
    
    if pressed:

        if show_delete_popup:
            # 如果点击删除弹窗的取消按钮或关闭按钮
            if delete_popup.is_touch_cancel(x, y, pressed) or delete_popup.is_touch_close(x, y, pressed):
                print("[取消或关闭]")
                show_delete_popup = False

            # 如果点击删除弹窗的确认按钮
            elif delete_popup.is_touch_confirm(x, y, pressed):
                print("[确认]")
                delete_keyword(tmp_delete)
                show_delete_popup = False

            return

        del_flag = False
        if show_mode == 'list_dialog' and not del_lock:
            current_y = 106

            str_size = image.string_size('关键词', scale=2.08)
            current_y += 24 + str_size.height()

            for word in keyword_list:
                str_size = image.string_size(word['word'], scale=2.08)

                del_xywh = [540, current_y - 6, del_icon.width(), del_icon.height()]

                # print(x, y, del_xywh)

                if is_in_button(x, y, del_xywh):
                    print("删除", word)
                    show_delete_popup = True
                    tmp_delete = word["id"]
                    # delete_keyword(word["id"])
                    del_flag = True
                    del_lock = True

                current_y += 32 + str_size.height()

        if del_flag:
            return

        if is_in_button(x, y, back_xywh):
            pressed_flag[0] = True
        elif is_in_button(x, y, edit_xywh):
            pressed_flag[1] = True
        elif is_in_button(x,y, list_xywh):
            pressed_flag[2] = True
        else: # cancel
            pressed_flag = [False, False, False]
    else:
        if show_delete_popup:
            return
        
        del_lock = False
        if pressed_flag[0]:
            print("exit btn click")
            pressed_flag[0] = False
            if show_mode == 'list_dialog':
                show_mode = 'default'
            else:
                app.set_exit_flag(True)
            
        if pressed_flag[1]:
            print("edit btn click")
            pressed_flag[1] = False
            if show_mode == 'list_dialog':
                pass
            else:
                handler_add_keyword()
            
        if pressed_flag[2]:
            print("list btn click")
            pressed_flag[2] = False
            if not show_mode == 'list_dialog':
                handler_open_list()

last_tick = time.time()
last_keep = time.time()

def draw_word():
    char_block_size = 64

    # 计算字符串的尺寸
    str_size = char_block_size * len(get_text_buffer())
    max_width = disp.width() - char_block_size  # 两边各留24像素的边距

    # 计算起始x坐标
    if str_size <= max_width:
        # 字符宽度小于屏幕宽度，居中显示
        x = round((disp.width() - str_size) / 2)
    else:
        # 字符宽度大于屏幕宽度，确保最右边显示到距离屏幕最右边24像素的位置
        x = disp.width() - str_size - 24

    # 计算起始y坐标
    y = disp.height() - 94 - char_block_size  # 距离底部94像素

    # 逐字符绘制
    current_x = x
    i = 0

    tmp_main_text = get_text_buffer()
    
    while i < len(tmp_main_text):
        # 检查当前字符是否属于某个关键词
        is_keyword = False
        keyword_length = 1  # 默认字符长度为1
        for keyword in keywords:
            if tmp_main_text[i:i+len(keyword)] == keyword:
                is_keyword = True
                keyword_length = len(keyword)  # 更新为关键词的长度
                break
        

        def _draw_string(current_x, i, text):
            # 设置字符颜色
            color = image.Color.from_rgb(255, 60, 60) if is_keyword else image.COLOR_WHITE
            # 绘制单个字符
            y_offset = 0
            if tmp_main_text[i] == '一':
                y_offset = 22
            elif tmp_main_text[i] == '二':
                y_offset = 4

            disp_image.draw_string(current_x, y + y_offset, text, color=color, scale=3.2)
        
        # 绘制字符或关键词
        if is_keyword:
            # 绘制整个关键词
            for tk in tmp_main_text[i:i+keyword_length]:
                _draw_string(current_x, i, tk)
                # 更新x坐标
                char_width = char_block_size
                current_x += char_width
                # 移动到下一个字符
                i += 1
        else:
            _draw_string(current_x, i, tmp_main_text[i])
            # 更新x坐标
            char_width = char_block_size
            current_x += char_width
            # 移动到下一个字符
            i += 1

def draw_word_english():
    keywords = get_keywords() # 关键词列表
    char_block_size = 64

    # 计算字符串的尺寸
    str_size = image.string_size(get_text_buffer(), 3.2).width()
    max_width = disp.width() - char_block_size  # 两边各留24像素的边距

    # 计算起始x坐标
    if str_size <= max_width:
        # 字符宽度小于屏幕宽度，居中显示
        x = round((disp.width() - str_size) // 2)
    else:
        # 字符宽度大于屏幕宽度，确保最右边显示到距离屏幕最右边24像素的位置
        x = disp.width() - str_size - 24

    # 计算起始y坐标
    y = disp.height() - 94 - char_block_size  # 距离底部24像素

    # 逐字符绘制
    current_x = x
    i = 0

    tmp_main_text = get_text_buffer()
    
    while i < len(tmp_main_text):
        # 检查当前字符是否属于某个关键词
        is_keyword = False
        keyword_length = 1  # 默认字符长度为1
        for keyword in keywords:
            if tmp_main_text[i:i+len(keyword)] == keyword:
                is_keyword = True
                keyword_length = len(keyword)  # 更新为关键词的长度
                break
        
        # 设置字符颜色
        color = image.Color.from_rgb(255, 60, 60) if is_keyword else image.COLOR_WHITE
        
        # 绘制字符或关键词
        if is_keyword:
            # 绘制整个关键词
            for tk in tmp_main_text[i:i+keyword_length]:
                y_offset = int(char_block_size - image.string_size(tk, 3.2).height())
                disp_image.draw_string(current_x, y + y_offset, tk, color=color, scale=3.2)
                # 更新x坐标
                char_width = image.string_size(tk, 3.2).width()
                current_x += char_width
                # 跳过关键词的其他字符
                i += 1
        else:
            # 绘制单个字符
            y_offset = int(char_block_size - image.string_size(tmp_main_text[i], 3.2).height())
            disp_image.draw_string(current_x, y + y_offset, tmp_main_text[i], color=color, scale=3.2)
            # 更新x坐标
            char_width = image.string_size(tmp_main_text[i], 3.2).width()
            current_x += char_width
            # 移动到下一个字符
            i += 1

async def english_tts_async_task():
    try:
        client = AsrOnlineClient(SERVER_ADDR, SERVER_PORT)
        asr_task = asyncio.create_task(client.run())

        await asr_task
    except ConnectionRefusedError:
        print('Check the server ip and port is correct!')

def english_tts_thread_work():
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    loop.run_until_complete(english_tts_async_task())
    loop.close()

    # asyncio.run(english_tts_task())

english_tts_thread = threading.Thread(target=english_tts_thread_work)
# english_tts_thread.daemon = True
english_tts_thread.start()

def main():
    global touch_end, last_keep, last_tick, first_text_at, current_tab, keywords, disp_image

    while not app.need_exit():
        if int(time.time()) - last_keep > 2:
            send_socket_message('global_keep_speech\n')
            last_keep = time.time()

        if int(time.time()) - last_tick > 5:
            send_socket_message('PASS\n')
            last_tick = time.time()

        if int(time.time()) - first_text_at > 0.5:
            if main_text_buffer != '':
                print(f"=== 超过{int(time.time()) - first_text_at }秒，强制清空！")
                send_socket_message('global_clear_speech_text\n')
                first_text_at = time.time()

        
        x, y, pressed = ts.read()
        hadnler_touch(x, y, pressed)

        # 防误触设计，模拟用户按压屏幕松开后才触发
        if pressed:
            touch_end = True
        elif touch_end:
            touch_end = False

            if is_in_button(x, y, chinese_btn_img_xywh):
                current_tab = 0
            if is_in_button(x, y, english_btn_img_xywh):
                current_tab = 1

        # 关键词列表
        keywords = get_keywords()

        # 初始化显示图像
        disp_image = image.Image(disp.width(), disp.height(), image.Format.FMT_RGBA8888)

        if show_mode == 'default':
            # disp_image.draw_string(150, 24, '暂不支持和童芯派交互', scale=1, color=image.COLOR_RED)
            disp_image.draw_image(icon_center[0], icon_center[1], sound_icon)
            draw_word()
        elif show_mode == 'add_success':
            disp_image.draw_image(check_icon_xywh[0], check_icon_xywh[1], check_icon)
            draw_word()
        elif show_mode == 'list_dialog':
            disp_image.draw_image(back_xywh[0], back_xywh[1], back_icon)
            disp_image.draw_image(86, 18, keywords_img)

            current_y = 106

            disp_image.draw_string(24, current_y + 4, 'ID', scale=2.08)
            str_size = image.string_size('关键词', scale=2.08)
            disp_image.draw_string(96, current_y, 'Keyword', scale=2.08)

            current_y += 24 + str_size.height()

            for word in keyword_list:
                # 超过屏幕大小需要跳过不然会报错
                if current_y > disp.height():
                    continue

                text = word['word']
                    
                if len(text) > 8:
                    text = text[:8] + '...'
                    
                disp_image.draw_string(24, current_y + 4, str(word['id']), scale=2.08)
                str_size = image.string_size(text, scale=2.08)
                disp_image.draw_string(96, current_y, text, scale=2.08)
                disp_image.draw_image(540, current_y - 6, del_icon)
                current_y += 24 + str_size.height()
        
        if show_mode in ['default']:
            disp_image.draw_image(edit_xywh[0], edit_xywh[1], edit_icon)
            disp_image.draw_image(back_xywh[0], back_xywh[1], back_icon)
            disp_image.draw_image(list_xywh[0], list_xywh[1], list_icon)

            if current_tab == 0:
                disp_image.draw_image(chinese_btn_img_xywh[0], chinese_btn_img_xywh[1], chinese_btn_active_img)
            else:
                disp_image.draw_image(chinese_btn_img_xywh[0], chinese_btn_img_xywh[1], chinese_btn_img)
            
            if current_tab == 1:
                disp_image.draw_image(english_btn_img_xywh[0], english_btn_img_xywh[1], english_btn_active_img)
            else:
                disp_image.draw_image(english_btn_img_xywh[0], english_btn_img_xywh[1], english_btn_img)
        
        if not speech_init_flag:
            disp_image = image.Image(disp.width(), disp.height(), image.Format.FMT_RGBA8888)
            disp_image.draw_string(0, 200, 'Connecting to server', color=image.COLOR_YELLOW, scale=3.2)

        if show_delete_popup:
            delete_popup.draw(disp_image)

        disp.show(disp_image)

main_thread = threading.Thread(target=main)
main_thread.start()