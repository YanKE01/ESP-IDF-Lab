# Windows / Linux 主机 OS 识别

[English / 英文版](./README.md)

本例演示一个 USB 设备如何在不询问主机的前提下,纯靠观察主机的枚举行为,判断 host 是 Windows 还是 Linux/macOS。

## 原理

USB 协议里没有"请告诉我你的操作系统"这种请求,所以设备只能从枚举行为的差异反推主机 OS。

最可靠、且不受缓存影响的指纹,是主机会不会去配置一个"没有驱动可绑"的 vendor 接口。

对一个裸 vendor 设备(接口类 0xFF、无驱动、无 WCID):Linux/macOS 在枚举时总会发 `SET_CONFIGURATION`,哪怕没有驱动绑定,而 Windows 找不到驱动就拒绝配置,把设备晾在 configuration 0。

于是设备这样判定:收到 `SET_CONFIGURATION`(mount)即为 Linux/macOS,而读了配置描述符却在宽限期内一直收不到 `SET_CONFIGURATION` 即为 Windows。

教科书上的 Windows 信号是 `GET_DESCRIPTOR(String, 0xEE)`("MSFT100" 探测),但 Windows 会把结果缓存进注册表(`...\Control\usbflags\<VID><PID><REV>` 的 `osvc`),之后重复插拔同一 VID/PID 就不再请求,所以本例特意不用它,只依赖不受缓存影响的 `SET_CONFIGURATION` 指纹。

## 判定流程

```
上电启动 -> 一个中性 vendor 接口枚举
  - host 读取我们的配置描述符(枚举已开始)
      - 收到 SET_CONFIGURATION(mount)          -> LINUX / macOS / 其他
      - 2000ms 内未收到 SET_CONFIGURATION         -> WINDOWS
  - 出结论 -> 写入 RTC RAM -> esp_restart()
软复位启动 -> 从 RTC RAM 读回结论,跳过探测
```

## 用 RTC RAM + 复位原因把结论保留到重启之后

真实设备都是探测一次,然后重启,让真正的 OS 专属 USB 协议栈从干净状态起来,所以结论必须能挺过这次重启。

本例把结论存进一个 `RTC_NOINIT_ATTR` 变量,它能挺过软复位,但断电会被清掉。

启动时只有当 `esp_reset_reason()` 是 `ESP_RST_SW`(我们探测后自己发起的 `esp_restart()`)才信任这个值,其它任何复位原因(包括 RTC RAM 里是垃圾值的真实上电)都强制重新探测,因此不需要额外的魔数校验。

选 RTC RAM 而不是 NVS 是有意为之:USB 总线供电的板子在你拔下来挪到另一台主机时会断电,从而清掉结论、自动重新探测,既避免跨机器的陈旧误判,又不磨损 flash。

在软复位那次启动里,本例只是把缓存的结论打印出来;Stage-2 的固件则会据此让 Windows 起 MSC、Linux 起 CDC-ACM。

## 如何区分"Windows 超时"和"根本没插 USB"

单纯一个超时无法区分"Windows 在场但拒绝配置我们"和"压根没插任何东西",因为这两种情况在总线上都表现为一片沉默。

关键在于:宽限计时器不是挂在开机时间上,而是挂在"主机读取我们配置描述符的那一刻"(代码里的 `usb_descriptors_config_requested()`)。

配置描述符只有在一个真实主机真正开始枚举设备时才会被读取,所以它的到来就是"主机在场并在和我们通信"的确凿证据。

如果开发板没插、用的是只供电的线、或接到了 UART 桥那个口而不是原生 USB 口,配置描述符永远不会被请求,计时器永远不会启动,设备会一直静默等待,而不会误报 Windows。

只读设备描述符也不足以启动计时器,所以一个探测一下就立刻放弃的主机同样不会触发误判的 Windows 结论。

一句话总结:Windows = 主机读了我们的配置描述符、随后在宽限期内保持沉默,而无主机 = 配置描述符从未被读取,二者因此永不混淆。

## 用法

为带原生 USB-OTG 的芯片编译烧录(esp32s3 / esp32s2 / esp32p4):

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

把开发板的原生 USB 口(GPIO19 D-、GPIO20 D+)接到被测主机,不要接 UART 桥那个口。

判定结果会打印在 UART 串口上。

## 注意事项与边界

Windows 的判定是基于超时的负向判断,因此固定有约 2 秒延迟;可调 `CONFIG_WAIT_GRACE_MS`,但调得太短会在慢速 Windows 上误判。

探针必须保持"无驱动可绑",所以不要加 WCID / MS OS 2.0 描述符;一旦给这个 VID/PID 装了 WinUSB/Zadig 驱动,Windows 就会去配置它,从而被误判成 Linux。

嵌入式 Linux、Android、各类 BSD 同样会配置设备,所以它们都归入 "LINUX / macOS / 其他"。

出结论后设备会重启一次,之后就信任 RTC 里缓存的结果,所以要重新探测必须给板子断电上电(清掉 RTC RAM),而不是按 RST 或只拔插数据口。
