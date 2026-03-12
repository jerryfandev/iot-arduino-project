# ESP Control Panel (Static)

Web control panel dạng **static HTML + JavaScript thuần**, dùng **Bootstrap 5** để giao diện hiện đại và **responsive** (đẹp trên điện thoại).

## Chạy thử nhanh

### Cách 1: Mở trực tiếp file (nhanh nhất)
- Mở `control-panel/index.html` bằng trình duyệt.

> Lưu ý: một số browser có thể hạn chế `localStorage`/CORS khi mở qua `file://`. Nếu gặp lỗi lặt vặt, dùng Cách 2.

### Cách 2: Chạy static server (khuyên dùng)
Trong thư mục project, chạy:

```bash
python3 -m http.server 8000
```

Rồi mở:
- `http://localhost:8000/control-panel/`

## Hiện tại có gì?
- 1 công tắc + 1 nút **Bật/Tắt đèn**
- JS tách riêng ở `control-panel/assets/js/app.js`
- Chưa gọi API; hàm `sendLampState()` là placeholder để gắn API/WebSocket về ESP sau này

