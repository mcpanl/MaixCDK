from maix import image

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