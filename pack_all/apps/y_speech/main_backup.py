from maix import app, nn, time, display, touchscreen, image
import time as _time
from pinyin import PinyinLookup
import threading

disp = display.Display()

disp_image = image.Image(disp.width(), disp.height())

disp_image.draw_string(24,24,'Loading...', image.COLOR_WHITE, scale=3, thickness=3)

disp.show(disp_image)

image.load_font("sourcehansans", "/maixapp/share/font/SourceHanSansCN-Regular.otf", size = 32)
print("fonts:", image.fonts())
image.set_default_font("sourcehansans")

sound_icon = image.load('./static/images/sound.png')
check_icon = image.load('./static/images/check.png')

icon_center = [round(disp.width() / 2) - round(sound_icon.width() / 2), round(disp.height() / 2) - round(sound_icon.height() / 2)]

main_text_update_at = int(time.time())
main_text = ''
main_text_last = ''
main_text_buffer = ''

speech = nn.Speech("/root/models/am_7332_192_int8.mud")
speech.init(nn.SpeechDevice.DEVICE_MIC)

def get_text_buffer():
    all_text = main_text_buffer + main_text
    if all_text.endswith("\u3000") or all_text.endswith(" "):
        all_text = all_text[:-1]
        
    if len(all_text) > 0:
        return all_text
    else:
        return '···'

def reset_text_buffer():
    global main_text_buffer, main_text
    print("RESET")
    # main_text_buffer = main_text_buffer + main_text
    main_text_buffer = ''
    main_text = ''
    speech.clear()

def callback(data: tuple[str, str], len: int):
    global main_text, main_text_last, main_text_update_at
    text, pinyin = data
    if text:
        main_text = text.replace(" ", "").replace(",", " ").replace(".", "\u3000")
    else:
        main_text = ''

    if main_text != main_text_last:
        main_text_update_at = int(time.time())

    main_text_last = main_text
    
    # print(data)

print('============')
start_time = _time.perf_counter()
lookup = PinyinLookup('./static/pinyin.bin')
print(lookup.get_pinyin('小'))  # 输出: nǐ
print(lookup.get_pinyin('爱'))  # 输出: hǎo,hào
print(lookup.get_pinyin('同'))
print(lookup.get_pinyin('学'))
end_time = _time.perf_counter()
print('============')
elapsed_time = end_time - start_time
print(f"函数执行耗时: {elapsed_time:.6f} 秒")

kwd = [{
    'id': 1,
    'word': '小爱同学',
    'pinyin': [['xiao']]
}]

lmS_path = "/root/models/lmS/"

speech.lvcsr(lmS_path + "lg_6m.sfst", lmS_path + "lg_6m.sym", \
             lmS_path + "phones.bin", lmS_path + "words_utf.bin", \
             callback)

def delete_loop():
    global main_text_buffer
    while not app.need_exit():
        # 关键词列表
        keywords = ["你好", "感谢", "使用"]

        # 初始化显示图像
        disp_image = image.Image(disp.width(), disp.height())

        # 绘制图标
        disp_image.draw_image(icon_center[0], icon_center[1], sound_icon)

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
        y = disp.height() - 24 - char_block_size  # 距离底部24像素

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
            color = image.COLOR_GREEN if is_keyword else image.COLOR_WHITE
            
            # 绘制字符或关键词
            if is_keyword:
                # 绘制整个关键词
                for tk in tmp_main_text[i:i+keyword_length]:
                    disp_image.draw_string(current_x, y, tk, color=color, scale=2)
                    # 更新x坐标
                    char_width = char_block_size
                    current_x += char_width
                    # 跳过关键词的其他字符
                    i += 1
            else:
                # 绘制单个字符
                disp_image.draw_string(current_x, y, tmp_main_text[i], color=color, scale=2)
                # 更新x坐标
                char_width = char_block_size
                current_x += char_width
                # 移动到下一个字符
                i += 1

        # 显示图像
        disp.show(disp_image)
        if int(time.time()) - main_text_update_at > 6:
            if len(main_text) > 0:
                reset_text_buffer()
        main_text_buffer = main_text_buffer[1:]
        time.sleep_ms(100)
        print(time.time())

loop_thread = threading.Thread(target=delete_loop)
loop_thread.daemon = True
loop_thread.start()

while not app.need_exit():
    # print(time.time(), main_text_update_at)

    frames = speech.run(1)
    
    if frames < 1:
        print("run out\n")
        break

    time.sleep_ms(100)
