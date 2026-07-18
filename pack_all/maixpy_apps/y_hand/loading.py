from maix import image

# 绘制 Loading 序列帧图片
class Loading:
    def __init__(self, disp):
        self.img_total = 79
        self.current_loaindg_index = 0
        self.disp = disp
        self.loading_img_list = [
            image.load(f"./assets/images/loading/{i:05d}.png") 
            for i in range(1, self.img_total + 1)  # 从00001.png到00079.png
        ]
    
    def draw(self):
        img = image.Image(self.disp.width(), self.disp.height())

        img.draw_rect(0, 0, self.disp.width(), self.disp.height(), image.Color.from_rgb(0, 0, 0), thickness=-1)
        img.draw_image(self.disp.width() // 2 - self.loading_img_list[0].width() // 2, 120, self.loading_img_list[self.current_loaindg_index])

        loading_text = "Loading..."
        loading_text_scale = 1.4

        loading_text_w, string_size_h = image.string_size(loading_text, scale=loading_text_scale)

        img.draw_string(self.disp.width() // 2 - loading_text_w // 2, 300, loading_text, image.Color.from_rgb(255, 255, 255), scale=loading_text_scale)

        if self.current_loaindg_index >= self.img_total - 1:
            self.current_loaindg_index = 0
        else:
            self.current_loaindg_index += 1
        
        return img
    