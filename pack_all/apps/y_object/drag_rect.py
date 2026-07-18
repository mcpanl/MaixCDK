from maix import image
from utils import is_in_button

# 屏幕中间可拖拽的方框
class DragRect:
    def __init__(self, screen_width, screen_height):
        self.screen_width = screen_width
        self.screen_height = screen_height

        # 计算矩形的初始位置和大小
        self.rect_width = screen_width // 2
        self.rect_height = screen_height // 2
        self.rect_x = (screen_width - self.rect_width) // 2
        self.rect_y = (screen_height - self.rect_height) // 2

        # 初始化拖动状态
        self.dragging = False
        self.start_x = 0
        self.start_y = 0
        self.start_width = 0
        self.start_height = 0
        self.pressed_count = 0
        self.corner = None
        
    def drag_and_draw(self, img, touch_x, touch_y, pressed):
        if pressed:
            self.pressed_count += 1
            threshold = 50 # 矩形外面能拖拽的范围
            corner_width = self.rect_width // 2 + threshold
            corner_height = self.rect_height // 2 + threshold

            if self.pressed_count == 1:
                # 判断点击的是哪个角
                if is_in_button(touch_x, touch_y, [self.rect_x - threshold, self.rect_y - threshold, corner_width, corner_height]):
                    self.corner = "top-left"
                elif is_in_button(touch_x, touch_y, [self.rect_x + corner_width, self.rect_y - threshold, corner_width, corner_height]):
                    self.corner = "top-right"
                elif is_in_button(touch_x, touch_y, [self.rect_x - threshold, self.rect_y + corner_height, corner_width, corner_height]):
                    self.corner = "bottom-left"
                elif is_in_button(touch_x, touch_y, [self.rect_x + corner_width, self.rect_y + corner_height, corner_width, corner_height]):
                    self.corner = "bottom-right"
                else:
                    self.corner = None
            
            if self.corner:
                if not self.dragging:
                    # 开始拖动初始化数据
                    self.dragging = True
                    self.start_x = touch_x
                    self.start_y = touch_y
                    self.start_width = self.rect_width
                    self.start_height = self.rect_height
                
                # 计算鼠标移动的距离
                dx = touch_x - self.start_x
                dy = touch_y - self.start_y

                new_width = 0
                new_height = 0

                # 根据不同的角计算新的宽度和高度
                if self.corner == "bottom-right":
                    new_width = self.start_width + dx
                    new_height = self.start_height + dy
                elif self.corner == "bottom-left":
                    new_width = self.start_width - dx
                    new_height = self.start_height + dy
                elif self.corner == "top-right":
                    new_width = self.start_width + dx
                    new_height = self.start_height - dy
                elif self.corner == "top-left":
                    new_width = self.start_width - dx
                    new_height = self.start_height - dy

                # 确保宽度和高度不为负数
                new_width = max(10, new_width)
                new_height = max(10, new_height)

                # 计算新的左上角坐标，使矩形保持在屏幕中央
                new_x = (self.screen_width - new_width) // 2
                new_y = (self.screen_height - new_height) // 2

                # 更新矩形的位置和大小
                img.draw_rect(
                    new_x,
                    new_y,
                    new_width,
                    new_height,
                    image.Color.from_rgb(255, 255, 255),
                    thickness=2
                )

                self.rect_x = new_x
                self.rect_y = new_y
                self.rect_width = new_width
                self.rect_height = new_height
        else:
            # 结束拖动
            self.dragging = False
            self.corner = None
            self.pressed_count = 0

        # 绘制初始矩形
        img.draw_rect(
            self.rect_x,
            self.rect_y,
            self.rect_width,
            self.rect_height,
            image.Color.from_rgb(255, 255, 255),
            thickness=2
        )
    
    def on_release(self):
        # 结束拖动
        self.dragging = False
        self.corner = None