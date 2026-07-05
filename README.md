# linux-char-driver

[![Build & Test Kernel Module](https://github.com/baonhk/linux-char-driver/actions/workflows/build-check.yml/badge.svg)](https://github.com/YOUR_USERNAME/linux-char-driver/actions/workflows/build-check.yml)

Một Linux Kernel Module (Character Device Driver) hoàn chỉnh, dùng để học và
làm portfolio Embedded Linux. Driver tạo ra `/dev/mydevice`, hỗ trợ
`read()`/`write()`, một bộ lệnh `ioctl()` để điều khiển "LED ảo", và các kỹ
thuật kernel nâng cao: `mutex`, `wait queue`, `poll()`.

> Repo này có CI (GitHub Actions) build module thật trên VM và chạy
> `scripts/test_driver.sh` để insmod/test/rmmod tự động ở mỗi lần push —
> xem badge phía trên và tab **Actions**.

## Mục tiêu học được

- Cấu trúc một Linux Kernel Module (`module_init` / `module_exit`)
- Character device: `alloc_chrdev_region`, `cdev_init`, `cdev_add`
- Tự động tạo device node qua `class_create` + `device_create` (udev)
- `file_operations`: `open`, `release`, `read`, `write`, `unlocked_ioctl`, `poll`
- Trao đổi dữ liệu kernel <-> userspace an toàn: `copy_to_user` / `copy_from_user`
- Kernel logging: `pr_info()`, `pr_err()` (xem bằng `dmesg`)
- Đồng bộ hoá: `mutex` bảo vệ buffer dùng chung
- Blocking I/O: `wait_queue_head_t`, `wait_event_interruptible`, `wake_up_interruptible`
- `poll()`/`select()` để multiplex I/O không cần blocking `read()`
- Viết `Makefile` build module out-of-tree (kbuild)

## Cấu trúc project

```
linux-char-driver/
│
├── driver/
│   ├── hello.c              # Kernel module chính (character driver)
│   ├── mydevice_ioctl.h     # Định nghĩa ioctl dùng chung kernel <-> userspace
│   └── Makefile              # Build module bằng kbuild
│
├── app/
│   └── test.c                # Chương trình userspace: write/read/ioctl/poll
│
├── docs/
│   └── architecture.md       # Sơ đồ kiến trúc (ASCII, xem bên dưới)
│
├── scripts/
│   └── test_driver.sh        # Script test tự động: build → insmod → test → dmesg → rmmod
│
├── .github/workflows/
│   └── build-check.yml       # CI: build module thật + chạy test trên GitHub Actions
│
├── README.md
└── LICENSE
```

## Kiến trúc / Luồng hoạt động

```
        userspace                          kernel space
   ┌────────────────────┐           ┌──────────────────────────┐
   │  app/test.c         │           │  driver/hello.c           │
   │                     │  open()   │                           │
   │  fd = open(          │────────▶│  mydevice_open()           │
   │   "/dev/mydevice")  │           │                           │
   │                     │  write()  │                           │
   │  write(fd, "Hello") │────────▶│  mydevice_write()           │
   │                     │           │   copy_from_user()        │
   │                     │           │   mutex_lock(dev_mutex)   │
   │                     │           │   pr_info() ──▶ dmesg     │
   │                     │           │   wake_up_interruptible() │
   │                     │  read()   │                           │
   │  read(fd, buf, N)   │────────▶│  mydevice_read()            │
   │                     │           │   wait_event_interruptible│
   │                     │           │   copy_to_user()           │
   │                     │  ioctl()  │                           │
   │  ioctl(fd, LED_ON)  │────────▶│  mydevice_ioctl()           │
   │                     │           │   LED_ON/OFF/GET_STATUS   │
   └────────────────────┘           └──────────────────────────┘
```

Luồng theo yêu cầu đề bài:

```
sudo insmod hello.ko
      │
      ▼
/dev/mydevice  (tạo tự động bởi udev qua class_create/device_create)
      │
      ▼
echo "Hello" > /dev/mydevice
      │
      ▼
Driver nhận dữ liệu (mydevice_write, copy_from_user)
      │
      ▼
Kernel xử lý (lưu vào buffer, mutex bảo vệ, wake_up_interruptible)
      │
      ▼
dmesg hiển thị log (pr_info)
```

## Yêu cầu môi trường

- Máy Linux thật hoặc VM (không chạy được trong container không có kernel headers)
- Cài kernel headers khớp với kernel đang chạy:

```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)
```

## Build & chạy

### 1. Build kernel module

```bash
cd driver
make
```

Sinh ra file `hello.ko`.

### 2. Load module

```bash
sudo insmod hello.ko
dmesg | tail        # xem log: "allocated major=... minor=...", "module loaded"
ls -l /dev/mydevice  # device node được udev tạo tự động
```

Hoặc dùng shortcut trong Makefile:

```bash
make load
```

### 3. Thử nghiệm bằng shell

```bash
echo "Hello" | sudo tee /dev/mydevice   # write
sudo cat /dev/mydevice                   # read (sẽ block nếu chưa có dữ liệu)
dmesg | tail                             # xem log kernel
```

### 4. Build & chạy chương trình test C

```bash
cd ../app
gcc -Wall -o test test.c
sudo ./test            # demo write/read + ioctl LED_ON/LED_OFF
sudo ./test --poll      # demo thêm poll()
```

Output mẫu:

```
[app] opened /dev/mydevice (fd=3)
[app] write("Hello from userspace!")
[app] wrote 22 bytes
[app] read back 22 bytes: "Hello from userspace!"
[app] ioctl(MYDEV_IOC_LED_ON)
[app] LED status = 1 (expected 1)
[app] ioctl(MYDEV_IOC_LED_OFF)
[app] LED status = 0 (expected 0)
[app] closed device
```

### 5. Unload module

```bash
cd ../driver
sudo rmmod hello
dmesg | tail       # "module unloaded"
```

### 6. (Khuyến nghị) Chạy test tự động end-to-end

Thay vì làm thủ công từng bước ở trên, dùng script tự động — build, insmod,
chạy app test, chụp log dmesg, rmmod, và báo PASS/FAIL rõ ràng:

```bash
sudo ./scripts/test_driver.sh
```

Log đầy đủ được lưu vào `test_evidence.log` — đây cũng là file bằng chứng
bạn có thể đính kèm khi nộp CV/portfolio.

## Bộ lệnh ioctl

| Lệnh                    | Ý nghĩa                              |
|-------------------------|---------------------------------------|
| `MYDEV_IOC_LED_ON`      | Bật "LED ảo" (`led_status = 1`)       |
| `MYDEV_IOC_LED_OFF`     | Tắt "LED ảo" (`led_status = 0`)       |
| `MYDEV_IOC_GET_STATUS`  | Đọc trạng thái LED hiện tại (int*)    |
| `MYDEV_IOC_RESET_BUF`   | Xoá buffer dữ liệu nội bộ của driver  |

Định nghĩa dùng chung nằm trong `driver/mydevice_ioctl.h`, được include ở cả
kernel module lẫn `app/test.c` để đảm bảo hai bên luôn khớp số lệnh.

## Các kỹ thuật nâng cao đã áp dụng

- **Mutex** (`struct mutex dev_mutex`): bảo vệ buffer `msg_buf`/`msg_len` và
  `led_status` khỏi race condition khi nhiều tiến trình truy cập đồng thời.
- **Wait queue** (`wait_queue_head_t read_wq`): `read()` sẽ block
  (`wait_event_interruptible`) cho đến khi có dữ liệu; `write()` gọi
  `wake_up_interruptible()` để đánh thức các reader đang chờ.
- **poll()**: cho phép userspace dùng `select()`/`poll()`/`epoll()` để chờ
  dữ liệu mà không cần block trực tiếp trên `read()`.

## Gỡ lỗi

- Không thấy `/dev/mydevice`: kiểm tra `dmesg` xem `class_create`/
  `device_create` có lỗi không; kiểm tra `udevadm monitor` khi `insmod`.
- `insmod: ERROR: could not insert module hello.ko: Operation not permitted`:
  kiểm tra Secure Boot / module signing trên máy đó, hoặc dùng VM để test.
- Build lỗi thiếu header: cài đúng `linux-headers-$(uname -r)`.

## License

Xem file [LICENSE](LICENSE) (MIT cho code trong repo; driver dùng
`MODULE_LICENSE("GPL")` theo quy ước bắt buộc của kernel Linux).
