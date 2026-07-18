from io import SEEK_CUR
from maix import image
from base_page import BasePage
import os
import re
from utils import truncate_string_by_width

class ModelListPage(BasePage):
    model_dir = '/root/y_hub/models'
    model_list = []

    edit_button_image = None

    def __init__(self, name, disp, cam, cam_min):
        super().__init__(name, disp, cam, cam_min)
        self.scroll_offset = 0
        self.scroll_y = 0
        self.last_touch_y = None
        self.is_touching = False
        self.scroll_min = -40  # 内容不满一屏也可以拖动
        self.scroll_max = 40     # 最高不能往下滚超出
        self.scroll_velocity = 0
        self.scroll_animating = False
        self.list_image = None
        self.inertia_velocity = 0
        self.inertia_active = False
        self.edit_button_image = image.load("./images/EditButton.png", format=image.Format.FMT_BGRA8888)
        self.footer_image = image.load("./images/Footer.png", format=image.Format.FMT_BGRA8888)
        self.right_image = image.load("./images/Right.png", format=image.Format.FMT_BGRA8888)
        self.title_my_models_image = image.load("./images/TitleMyModels.png", format=image.Format.FMT_BGRA8888)

        self.prev_page_btn_img = image.load('./assets/images/prev_page_btn.png', image.Format.FMT_RGBA8888)
        self.prev_page_btn_right_margin = 45
        self.prev_page_btn_xywh = [disp.width() // 2 - self.prev_page_btn_img.width() - self.prev_page_btn_right_margin, disp.height() - self.prev_page_btn_img.height(), self.prev_page_btn_img.width(), self.prev_page_btn_img.height()]

        self.next_page_btn_img = image.load('./assets/images/next_page_btn.png', image.Format.FMT_RGBA8888)
        self.next_page_btn_left_margin = 45
        self.next_page_btn_xywh = [disp.width() // 2 + self.next_page_btn_left_margin, disp.height() - self.next_page_btn_img.height(), self.next_page_btn_img.width(), self.next_page_btn_img.height()]

        print('next_page_btn_xywh', self.next_page_btn_xywh)

        self.current_page = 1 # 当前页码
        self.total_page = 0 # 总页数
        self.pagination_num = 4 # 每页 x 条数据
        self.pagination_list = [] # 分页列表数据
        self.data_initialized = False  # 标记数据是否已初始化
        self.scan_model_folders()

    def show(self):
        print("List SHOW")
        self.inertia_velocity = 0
        self.inertia_active = False
        self.is_touching = False
        
        # 只有在数据未初始化时才重新扫描
        if not self.data_initialized:
            self.scan_model_folders()
            self.data_initialized = True
        
        # 确保当前页码在有效范围内
        self._update_pagination()
        if self.current_page > self.total_page and self.total_page > 0:
            self.current_page = self.total_page
        elif self.total_page == 0:
            self.current_page = 1
        
        self.active = True

    def hide(self):
        print("List HIDE")
        self.active = False

    def _update_pagination(self):
        """更新分页数据"""
        self.pagination_list = [self.model_list[i:i+self.pagination_num] for i in range(0, len(self.model_list), self.pagination_num)]
        self.total_page = len(self.pagination_list) if self.pagination_list else 1

    def hanlder_touch_pressed(self, touch_x, touch_y):
        # 如果点击的是上一页
        if self.is_in_button(touch_x, touch_y, self.prev_page_btn_xywh) and self.current_page > 1:
            self.current_page -= 1

        # 如果点击的是下一页
        elif self.is_in_button(touch_x, touch_y, self.next_page_btn_xywh) and self.current_page < self.total_page:
            self.current_page += 1
        
        else:
            # 如果点击的是编辑按钮
            y = 90
            x4 = 520
            y += 58

            if self.pagination_list and self.current_page <= len(self.pagination_list):
                for index, item in enumerate(self.pagination_list[self.current_page - 1]):
                    tmp_position = [x4, y - 8 + 20 // 2 - 4, self.edit_button_image.width(), self.edit_button_image.height()]

                    if self.is_in_button(touch_x, touch_y, tmp_position):
                        print("Clicked", item["model_id"])
                        self.emit_event('on_model_list_edit', item)

                    y += 70

    def loop(self, img, ts_res):
        img.draw_rect(0, 0, self.disp.width(), 130, image.Color.from_bgra(0,0,0,1), thickness=-1)
        img.draw_image(self.disp.width() // 2 - self.title_my_models_image.width() // 2, 26, self.title_my_models_image)
        img.draw_string(24, 90, "ID", scale=1.5)
        img.draw_string(120, 90, "Name / Type", scale=1.5)

        y = 90
        x1 = 24
        x2 = 120
        x4 = 520
        y += 58

        # 移除重复的分页计算，使用已计算好的pagination_list
        if len(self.pagination_list) == 0:
            return img

        # 确保当前页码在有效范围内
        if self.current_page > len(self.pagination_list):
            self.current_page = len(self.pagination_list) if len(self.pagination_list) > 0 else 1
        elif self.current_page < 1:
            self.current_page = 1

        # 确保当前页有数据
        if self.current_page > len(self.pagination_list) or len(self.pagination_list[self.current_page - 1]) == 0:
            return img

        for index, item in enumerate(self.pagination_list[self.current_page - 1]):
            img.draw_string(x1, y + 4 + 20 // 2, str((self.current_page - 1) * 4 + (index + 1)), scale=1.5)

            text = item["model_name"]
            text = truncate_string_by_width(text)

            img.draw_string(x2, y, text, scale=1.5, wrap=False)
            img.draw_string(x2, y + 38, "Classification" if item["model_type"] == 0 else "Detection", scale=1)
            img.draw_image(x4, y - 8 + 20 // 2 - 4, self.edit_button_image)

            y += 70
        
        if self.current_page > 1:
            img.draw_image(self.prev_page_btn_xywh[0], self.prev_page_btn_xywh[1], self.prev_page_btn_img)
        
        pagination_num_text = f"{self.current_page}/{self.total_page}"
        pagination_num_text_w, pagination_num_text_h = image.string_size(pagination_num_text, scale=1.575)
        img.draw_string(self.disp.width() // 2 - pagination_num_text_w // 2, self.disp.height() - pagination_num_text_h - 20, pagination_num_text, scale=1.575)

        if self.current_page < self.total_page:
            img.draw_image(self.next_page_btn_xywh[0], self.next_page_btn_xywh[1], self.next_page_btn_img)

        return img

    def init(self):
        print("List INIT")

    def scan_model_folders(self):
        self.model_list = []  # 清空之前的数据
        if not os.path.exists(self.model_dir):
            print(f"模型目录不存在: {self.model_dir}")
            return

        for item in os.listdir(self.model_dir):
            full_path = os.path.join(self.model_dir, item)
            if os.path.isdir(full_path):
                mud_files = [f for f in os.listdir(full_path) if f.endswith('.mud')]
                mud_name = None

                if len(mud_files) > 0:
                    mud_name = mud_files[0]
                else:
                    continue

                model_info = {
                    "dir_path": full_path,
                    "dir_name": item,
                    "model_path": f"{full_path}/{mud_name}",
                    "server_type": 0,
                    "model_id": 0,
                    "model_type": 0,
                    "model_name": ""
                }
                model_name_path = os.path.join(full_path, "model_name.txt")
                match = re.search(r"/(\d+)_(\d+)_(\d+)/", model_info["model_path"])
                server_type, model_id, model_type = match.groups()

                model_info["server_type"] = int(server_type)
                model_info["model_id"] = int(model_id)
                model_info["model_type"] = int(model_type)

                if os.path.isfile(model_name_path):
                    try:
                        with open(model_name_path, "r") as f:
                            model_info["model_name"] = f.read().strip()
                    except Exception as e:
                        print(f"读取 {model_name_path} 失败: {e}")
                self.model_list.append(model_info)

        # 根据模型ID倒序排列
        self.model_list.sort(key=lambda x: x["model_id"], reverse=True)
        # self.model_list = self.model_list * 3
        print("扫描完成:", self.model_list)
        
        # 扫描完成后更新分页数据
        self._update_pagination()