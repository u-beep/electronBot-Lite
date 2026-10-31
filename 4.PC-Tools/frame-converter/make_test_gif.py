# 生成测试 GIF：检验 ebconvert 的局部帧矩形 / 透明色 / disposal 合成
# - 画布 160x120，4 色全局表（0=黑 1=红 2=绿 3=透明）
# - 第 0 帧：全画布，绿色象限(80x60) + 红方块(40,40)，disposal=1 保留
# - 第 1..5 帧：40x40 局部帧，红方块右移，左上角 4x4 透明孔，disposal=2 还原背景
import struct

W, H = 160, 120
GCT = bytes((0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 0))

class BitW:
    def __init__(s):
        s.buf = 0; s.n = 0; s.b = bytearray()
    def put(s, code, bits=9):
        s.buf |= code << s.n; s.n += bits
        while s.n >= 8:
            s.b.append(s.buf & 0xFF); s.buf >>= 8; s.n -= 8
    def flush(s):
        if s.n:
            s.b.append(s.buf & 0xFF)
        s.buf = 0; s.n = 0
        r = bytearray(); data = bytes(s.b)
        for i in range(0, len(data), 255):
            c = data[i:i + 255]
            r.append(len(c)); r += c
        r.append(0)
        return r

def lzw(idx):
    # 纯字面量编码：码表 9 位最多 512 项，CLEAR 周期必须远小于
    # (512 - 258) = 254 个码，否则解码器切 10 位后失步
    bw = BitW(); bw.put(256)
    for cnt, p in enumerate(idx, 1):
        bw.put(p)
        if cnt % 250 == 0:
            bw.put(256)
    bw.put(257)
    return bw.flush()

out = bytearray()
out += b'GIF89a'
out += struct.pack('<HHBBB', W, H, 0xF1, 0, 0)   # GCT flag=1, size field 1 -> 4 entries
out += GCT

for i in range(6):
    if i == 0:
        x0, y0, fw, fh = 0, 0, W, H
        disp = 1
        idx = [0] * (W * H)
        for y in range(60):
            for x in range(80):
                idx[y * W + x] = 2                       # 绿象限
        for y in range(40, 80):
            for x in range(10, 50):
                idx[y * W + x] = 1                       # 红方块起点
    else:
        x0 = 10 + i * 18
        x0, y0, fw, fh = x0, 40, 40, 40
        disp = 2
        idx = [1] * (fw * fh)                            # 纯红局部帧
        for y in range(4):                               # 左上 4x4 透明孔
            for x in range(4):
                idx[y * fw + x] = 3
    out += b'\x21\xF9\x04' + struct.pack('<BHB', (disp << 2) | 1, 5, 3) + b'\x00'
    out += b'\x2C' + struct.pack('<HHHH', x0, y0, fw, fh) + b'\x00'
    out += bytes((8,)) + lzw(idx)
out += b'\x3B'

open('test_anim.gif', 'wb').write(out)
print('test_anim.gif ok: 6 frames,', len(out), 'bytes')
