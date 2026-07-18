from maix import image
from base_page import BasePage
import json
import os
import time
import datetime

class CollectPage(BasePage):
    detector = None

    url = "https://maixhub.com/api/v1/dataset/device/upload/image"
    token = "138806b58c6f432e9afe7b312e1d7125"
    data_id = None
    server_type = 0

    take_button_image = None
    take_button_position = [0,0,1,1]

    take_photo_flag = False

    take_photo_last_image = None
    take_photo_last_image_at = time.time()
    take_photo_dir = '/root/y_hub/upload'
    take_photo_file_list = []
    take_photo_upload_list = []

    def __init__(self, name, disp, cam, cam_min):
        super().__init__(name, disp, cam, cam_min)
        self.take_button_image = image.load("./images/TakeButton.png", format=image.Format.FMT_BGRA8888)
        self.take_button_position = [
            self.disp.width() - self.take_button_image.width() - 24,
            self.disp.height() // 2 - self.take_button_image.height() // 2,
            self.take_button_image.width(),
            self.take_button_image.height()
        ]
        if not os.path.exists(self.take_photo_dir):
            os.makedirs(self.take_photo_dir)

    def show(self):
        print("SHOW")
        self.active = True

    def hide(self):
        print("HIDE")
        self.active = False

    def hanlder_touch_pressed(self, x, y):
        print(f"x={x}, y={y}")
        if self.is_in_button(x, y, self.take_button_position):
            print("拍照？")
            self.take_photo_flag = True

    def handler_user_key(self):
        print("实体按键触发拍照")
        self.take_photo_flag = True

    def loop(self, img, ts_res):
        current_year = datetime.datetime.now().year
        if current_year == 1970:
            self.show_toast(4)

        current_time = time.time()
        cam_img = self.cam.read()
        cam_min_img = self.cam_min.read()

        if time.time() - self.take_photo_last_image_at > 0.2:
            self.take_photo_last_image = None

        if self.take_photo_last_image:
            img.draw_image(self.disp.width() // 2 - self.cam_min.width() // 2, self.disp.height() // 2 - self.cam_min.height() // 2, self.take_photo_last_image)
        else:
            img.draw_image(0,0,cam_img)

            img.draw_image(
                self.take_button_position[0],
                self.take_button_position[1],
                self.take_button_image
            )

            if self.take_photo_flag == True:
                tmp_file_name = f"tmp_{current_time}.jpeg"
                tmp_file_full_path = f"{self.take_photo_dir}/{tmp_file_name}"
                cam_min_img.save(tmp_file_full_path)
                self.take_photo_file_list.append(tmp_file_full_path)
                self.take_photo_last_image = cam_min_img
                self.take_photo_flag = False
                self.take_photo_last_image_at = time.time()

        if len(self.take_photo_upload_list) >= len(self.take_photo_upload_list) + len(self.take_photo_file_list):
            text_color = image.COLOR_GREEN
        else:
            text_color = image.COLOR_RED
        img.draw_string(12, 420, f"Uploaded: {len(self.take_photo_upload_list)}/{len(self.take_photo_upload_list) + len(self.take_photo_file_list)}", scale = 2, color = text_color, thickness=-1)

        return img

    def reset(self):
        self.take_photo_last_image = None
        self.take_photo_file_list = []
        self.take_photo_upload_list = []

    def init(self):
        print("INIT")
    
