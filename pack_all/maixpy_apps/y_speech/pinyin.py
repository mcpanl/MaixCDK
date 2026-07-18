class PinyinLookup:
    def __init__(self, filename):
        self.filename = filename
        self.index = []
        self.data_start = 0
        self._load_index()

    def _load_index(self):
        with open(self.filename, 'rb') as f:
            num_entries = int.from_bytes(f.read(4), 'little')
            index_size = num_entries * 8  # 每个索引条目8字节
            index_bytes = f.read(index_size)
            self.data_start = 4 + index_size  # 文件头4字节 + 索引区

            # 解析索引到列表
            self.index = [(int.from_bytes(index_bytes[i:i + 4], 'little'),
                           int.from_bytes(index_bytes[i + 4:i + 8], 'little'))
                          for i in range(0, len(index_bytes), 8)]

    def get_pinyin(self, character):
        cp = ord(character)
        # 二分查找
        left, right = 0, len(self.index) - 1
        while left <= right:
            mid = (left + right) // 2
            mid_cp, offset = self.index[mid]
            if mid_cp == cp:
                return self._read_pinyin_data(offset)
            elif mid_cp < cp:
                left = mid + 1
            else:
                right = mid - 1
        return None

    def _read_pinyin_data(self, offset):
        with open(self.filename, 'rb') as f:
            f.seek(self.data_start + offset)
            length = int.from_bytes(f.read(2), 'little')
            return f.read(length).decode('utf-8')
