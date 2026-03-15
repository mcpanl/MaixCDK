import cv2

# 读取图片
img = cv2.imread("cat1.jpg")

# bbox坐标
x1, y1 = 22, 149
x2, y2 = 718, 689

# 画框 (BGR颜色)
cv2.rectangle(img, (x1, y1), (x2, y2), (0,255,0), 3)

# 写类别和置信度
cv2.putText(img, "cat 0.725", (x1, y1-10),
            cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0,255,0), 2)

# 保存图片
cv2.imwrite("cat1_out.jpg", img)

print("saved cat1_out.jpg")

