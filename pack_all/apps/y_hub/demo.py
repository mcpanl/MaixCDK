from maix import display, image, nn
from base_page import BasePage
import os
import re

class DemoPage(BasePage):
    model_id = 31
    model_name = "Cat1"
    model_type = 1
    model_path = "/root/y_hub/models/0_31_1/01JWS1388QQTEKTFR6C4BA35E4.mud"
    detector = None
    classifier = None

    detector_results = []
    classifier_result = None

    def __init__(self, name, disp, cam, cam_min):
        super().__init__(name, disp, cam, cam_min)
        self.reset()
        
    def get_label_value_nearest_center(self, label, value_key=None):
        if not self.detector_results or len(self.detector_results) == 0:
            return None

        # 值字段映射
        value_key_map = {
            1: 'cx',
            2: 'cy',
            3: 'w',
            4: 'h',
            5: 'score'
        }

        # 解析 value_key
        if isinstance(value_key, int) or (isinstance(value_key, str) and str(value_key).isdigit()):
            value_key = value_key_map.get(int(value_key), value_key)

        # 过滤出指定 label 的目标
        filtered = [r for r in self.detector_results if r['label'] == label]
        if not filtered:
            return None

        # 计算中心位置
        img_cx, img_cy = self.disp.width() / 2, self.disp.height() / 2

        # 找到最接近中心的该类目标
        target = min(filtered, key=lambda r: (r['cx'] - img_cx) ** 2 + (r['cy'] - img_cy) ** 2)

        # 返回值处理
        if value_key in target:
            if value_key == 'score':
                return int(target['score'] * 100)
            return target[value_key]

        return target  # 如果没有传 value_key，则返回整个目标

    def get_target_by_condition(self, condition, value_key=None):
        if not self.detector_results or len(self.detector_results) == 0:
            return None

        # 数字/字符串数字条件映射
        condition_map = {
            1: 'center_most',
            2: 'most_left',
            3: 'most_right',
            4: 'most_top',
            5: 'most_bottom',
            6: 'largest_box',
            7: 'smallest_box',
            8: 'highest_conf',
            9: 'lowest_conf'
        }

        value_key_map = {
            1: 'cx',
            2: 'cy',
            3: 'w',
            4: 'h',
            5: 'score'
        }


        # 如果是数字或字符串数字，转换为关键词
        if isinstance(condition, int) or (isinstance(condition, str) and condition.isdigit()):
            condition = condition_map.get(int(condition), condition)

        if isinstance(value_key, int) or (isinstance(value_key, str) and str(value_key).isdigit()):
            value_key = value_key_map.get(int(value_key), value_key)

        if condition == 'most_left':
            target = min(self.detector_results, key=lambda r: r['x'])
        elif condition == 'most_right':
            target = max(self.detector_results, key=lambda r: r['x'] + r['w'])
        elif condition == 'most_top':
            target = min(self.detector_results, key=lambda r: r['y'])
        elif condition == 'most_bottom':
            target = max(self.detector_results, key=lambda r: r['y'] + r['h'])
        elif condition == 'center_most':
            img_cx, img_cy = self.disp.width() / 2, self.disp.height() / 2
            target = min(self.detector_results, key=lambda r: (r['cx'] - img_cx) ** 2 + (r['cy'] - img_cy) ** 2)
        elif condition == 'largest_box':
            target = max(self.detector_results, key=lambda r: r['w'] * r['h'])
        elif condition == 'smallest_box':
            target = min(self.detector_results, key=lambda r: r['w'] * r['h'])
        elif condition == 'highest_conf':
            target = max(self.detector_results, key=lambda r: r['score'])
        elif condition == 'lowest_conf':
            target = min(self.detector_results, key=lambda r: r['score'])
        else:
            return None

        if value_key in target:
            if value_key == 'score':
                return int(target['score'] * 100)
            return target[value_key]
        return target  # 如果没有指定字段，就返回整个目标字典

    def show(self):
        print("SHOW")
        self.active = True

    def hide(self):
        print("HIDE")
        self.active = False

    def hanlder_touch_pressed(self, x, y):
        pass

    def loop(self, img, ts_res):
        cam_min_img = self.cam_min.read()

        if self.model_type == 0 and self.classifier:
            res = self.classifier.classify(cam_min_img)

            max_idx, max_prob = res[0]
            msg = f"{self.classifier.labels[max_idx]} {max_prob:5.2f}"
            # print(msg)

            img.draw_image(0,0, cam_min_img.resize(self.disp.width(), self.disp.height()))

            str_size = image.string_size(msg, scale=1.5, thickness=-1)
            text_w, text_h = str_size.width(), str_size.height()

            # 文字居中位置
            text_x = self.disp.width() // 2 - text_w // 2
            text_y = self.disp.height() - text_h - 32

            padding = 8
            rect_x = text_x - padding // 2 - padding - 2
            rect_y = text_y - padding // 2 - padding // 2 - 4
            rect_w = text_w + padding * 2 + padding * 2
            rect_h = text_h + padding * 2 + padding

            # 画背景框
            img.draw_rect(rect_x, rect_y, rect_w, rect_h, color=image.Color.from_rgb(9, 189, 50), thickness=-1)
            # 画文字
            img.draw_string(text_x, text_y, msg, color=image.COLOR_WHITE, scale=1.5, thickness=-1)

            self.classifier_result = {
                'label': self.classifier.labels[max_idx],
                'score': max_prob
            }
            self.detector_results = []
        elif self.model_type == 1 and self.detector:
            results = []
            objs = self.detector.detect(cam_min_img, conf_th = 0.4, iou_th = 0.45)
            img.draw_image(0,0, cam_min_img.resize(self.disp.width(), self.disp.height()))
            scale_x = self.disp.width() / cam_min_img.width()
            scale_y = self.disp.height() / cam_min_img.height()

            
            for obj in objs:
                obj.x = int(obj.x * scale_x)
                obj.y = int(obj.y * scale_y)
                obj.w = int(obj.w * scale_x)
                obj.h = int(obj.h * scale_y)
                # 绘制目标框
                img.draw_rect(obj.x, obj.y, obj.w, obj.h, color=image.Color.from_rgb(9, 189, 50), thickness=4)

                # 文本内容
                msg = f'{self.detector.labels[obj.class_id]}: {obj.score:.2f}'

                # 获取文本尺寸
                str_size = image.string_size(msg, scale=1, thickness=-1)
                text_w, text_h = str_size.width(), str_size.height()

                # 文字左上角外的位置
                text_x = obj.x + 6
                text_y = obj.y - text_h - 8  # 比框上方留一点空隙

                # 背景框位置和尺寸
                padding = 16
                rect_x = text_x - padding // 2
                rect_y = text_y - padding // 2
                rect_w = text_w + padding
                rect_h = text_h + padding

                # 防止背景框超出画面顶端（可选）
                if rect_y < 0:
                    rect_y = 0
                    text_y = rect_y + padding // 2

                # 显示框顶点坐标（在框内侧）
                font_scale = 0.6
                font_thickness = -1

                # 左下角
                lx, ly = obj.x + 2, obj.y + obj.h - text_h - 2
                left_bottom_msg = f'({obj.x},{obj.y + obj.h})'
                # img.draw_string(lx, ly, left_bottom_msg, color=image.COLOR_WHITE, scale=font_scale, thickness=font_thickness)

                # 右上角
                rx, ry = obj.x + obj.w - image.string_size(f'({obj.x + obj.w},{obj.y})', scale=font_scale).width() - 2, obj.y + 2
                right_top_msg = f'({obj.x + obj.w},{obj.y})'
                # img.draw_string(rx, ry, right_top_msg, color=image.COLOR_WHITE, scale=font_scale, thickness=font_thickness)

                # 右下角
                rx2, ry2 = obj.x + obj.w - image.string_size(f'({obj.x + obj.w},{obj.y + obj.h})', scale=font_scale).width() - 2, obj.y + obj.h - text_h - 2
                right_bottom_msg = f'({obj.x + obj.w},{obj.y + obj.h})'
                # img.draw_string(rx2, ry2, right_bottom_msg, color=image.COLOR_WHITE, scale=font_scale, thickness=font_thickness)

                # 画背景框
                img.draw_rect(rect_x, rect_y, rect_w, rect_h, color=image.Color.from_rgb(9, 189, 50), thickness=-1)

                # 画文字
                img.draw_string(text_x, text_y, msg, color=image.COLOR_WHITE, scale=1, thickness=-1)
                result = {
                    'class_id': obj.class_id,
                    'label': self.detector.labels[obj.class_id],
                    'score': obj.score,
                    'x': obj.x,
                    'y': obj.y,
                    'w': obj.w,
                    'h': obj.h,
                    'cx': obj.x + obj.w // 2,  # 中心点 X
                    'cy': obj.y + obj.h // 2   # 中心点 Y
                }
                results.append(result)

            self.detector_results = results
            self.classifier_result = None


        else:
            pass

        return img

    def reset(self):
        if self.model_path:
            match = re.search(r"/(\d+)_(\d+)_(\d+)/", self.model_path)
            _server_type, model_id, model_type = match.groups()

            self.model_id = int(model_id)
            self.model_type = int(model_type)

            print("加载模型", self.model_path, model_id, model_type)
            if os.path.exists(self.model_path):
                if self.detector:
                    del self.detector
                if self.classifier:
                    del self.classifier

                if self.model_type == 0:
                    self.classifier = nn.Classifier(model=self.model_path, dual_buff = False)
                elif self.model_type == 1:
                    self.detector = nn.YOLOv5(model=self.model_path, dual_buff=False)
                else:
                    print("暂不支持的模型类型", model_type)

    def init(self):
        print("INIT")