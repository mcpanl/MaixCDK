import pickle, os

# 触摸是否在按钮的范围内
def is_in_button(x, y, btn_pos):
    return x > btn_pos[0] and x < btn_pos[0] + btn_pos[2] and y > btn_pos[1] and y < btn_pos[1] + btn_pos[3]

# 保存数据到本地
def set_local_storage(save_path, data):
    # 获取保存路径的目录部分
    directory = os.path.dirname(save_path)

    # 如果目录不存在，则创建目录
    if not os.path.exists(directory):
        os.makedirs(directory)
    
    with open(save_path, "wb") as f:
        pickle.dump(data, f)

# 获取本地的数据
def get_local_storage(save_path):
    try:
        if os.path.exists(save_path):
            with open(save_path, 'rb') as f:
                return pickle.load(f)
        else:
            return None
    except Exception as ex:
        return None