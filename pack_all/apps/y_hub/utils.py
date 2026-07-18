import subprocess
from maix import time, image

# 触摸是否在按钮的范围内
def is_in_button(x, y, btn_pos):
    return x > btn_pos[0] and x < btn_pos[0] + btn_pos[2] and y > btn_pos[1] and y < btn_pos[1] + btn_pos[3]

def truncate_string_by_width(text: str, text_scale: int = 1, max_width: int = 200) -> str:
    """
    根据显示宽度截断字符串
    """
    if not text:
        return text
    
    # 估算每个字符的显示宽度
    def get_char_width(char):
        return image.string_size(char, scale=text_scale).width()
    
    # 计算当前文本的总宽度
    current_width = sum(get_char_width(char) for char in text)
    
    if current_width <= max_width:
        return text
    
    # 逐个字符添加，直到超过最大宽度
    result = ""
    width_count = 0
    
    for char in text:
        char_width = get_char_width(char)
        if width_count + char_width <= max_width:
            result += char
            width_count += char_width
        else:
            break
    
    # 如果文本被截断了，添加省略号
    if width_count < current_width:
        return result + "..."
    else:
        return result

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

def is_network_available(retries=3, timeout=1):
    """
    检测网络连接是否可用（兼顾国内外DNS）
    
    参数:
        retries (int): 单次检测的重试次数
        timeout (int): 每次ping的超时时间（秒）
    
    返回:
        bool: 网络可用返回True，否则返回False
    """
    # 国外DNS（Google）+ 国内DNS（阿里云、腾讯云）
    hosts = ['8.8.8.8', '223.5.5.5', '119.29.29.29']
    
    for host in hosts:
        for _ in range(retries):
            # 执行ping命令，-c 1表示发送1个包，-W设置超时
            result = subprocess.call(
                ['ping', '-c', '1', '-W', str(timeout), host],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            if result == 0:
                return True
            time.sleep_ms(10)  # 重试间隔，避免频繁请求
    
    return False