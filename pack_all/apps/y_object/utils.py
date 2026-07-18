from maix import image

# 触摸是否在按钮的范围内
def is_in_button(x, y, btn_pos):
    return x > btn_pos[0] and x < btn_pos[0] + btn_pos[2] and y > btn_pos[1] and y < btn_pos[1] + btn_pos[3]

def show_page_loading(disp):
    # 显示 loading 加载
    img = image.Image(disp.width(), disp.height(), image.Format.FMT_RGBA8888)
    loading_text = 'Loading...'
    loading_text_scale = 2
    loading_w, loading_h = image.string_size(loading_text, scale=loading_text_scale)
    img.draw_string(disp.width() // 2 - loading_w // 2, disp.height() // 2 - loading_h // 2, loading_text, scale=loading_text_scale)
    disp.show(img)