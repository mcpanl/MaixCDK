from maix import image
from base_page import BasePage
import json
import datetime

class DeployPage(BasePage):
    detector = None

    url = None
    token = None
    server_type = 0
    
    model_full_name = None
    model_id = None
    model_name = None
    model_type = None
    download_url = None
    download_status = None
    download_progress = 0
    error_msg = ''

    def __init__(self, name, disp, cam, cam_min):
        super().__init__(name, disp, cam, cam_min)

    def show(self):
        print("部署页面，SHOW")
        self.active = True

    def hide(self):
        print("部署页面，HIDE")
        self.active = False

    def hanlder_touch_pressed(self, x, y):
        print(f"x={x}, y={y}")

    def loop(self, img, ts_res):
        text_scale = 1.3

        if self.download_status == "WaitMeatInfo":
            text = f'Get Model Info...'
            text_w, text_h = image.string_size(text, scale=text_scale)
            img.draw_string(self.disp.width() // 2 - text_w // 2, 240, text, scale=text_scale, thickness=-1)
        elif self.download_status == "WaitZip":
            text = f'Extracting files'
            text_w, text_h = image.string_size(text, scale=text_scale)
            img.draw_string(self.disp.width() // 2 - text_w // 2, 240, text, scale=text_scale, thickness=-1)
        elif self.download_status == 'Error':
            text = self.error_msg
            text_w, text_h = image.string_size(text, scale=text_scale)
            img.draw_string(self.disp.width() // 2 - text_w // 2, 240, text, scale=text_scale, thickness=-1)
        elif self.download_status == "Downloading":
            img.draw_string(226, 210, f'Downloading...', scale=1.2, thickness=-1)
            w = self.disp.width() // 2
            h = 6
            img.draw_rect(self.disp.width() // 2 - w // 2, self.disp.height() // 2 - h // 2 + 24, w, h, image.Color.from_rgb(255, 255, 255), thickness=-1)
            img.draw_rect(self.disp.width() // 2 - w // 2, self.disp.height() // 2 - h // 2 + 24, int(w * (self.download_progress / 100)), h, image.Color.from_rgb(67, 186, 74), thickness=-1)

        return img


    def init(self):
        print("INIT")