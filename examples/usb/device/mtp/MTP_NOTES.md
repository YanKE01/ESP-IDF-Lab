# MTP 交互与返回字段说明

本文档按照主机发起的 MTP 操作，说明主机要完成什么、Command 参数是什么、Data 或 Response 中每个字段代表什么。

## 基础概念

### MTP 基本交互与数据方向

不需要传输额外数据的操作按照 `Command → Response` 完成。

设备向主机返回数据的操作按照 `Command → Data IN → Response` 完成。

主机向设备发送数据的操作按照 `Command → Data OUT → Response` 完成。

USB 和 MTP 中的 `IN`、`OUT` 同样以主机为参照，`Data IN` 表示 `设备 → 主机`，`Data OUT` 表示 `主机 → 设备`。

每个 Object Handle 都代表一个 MTP Object，当前实现中每个文件对应一个 Object 和一个 Handle。

MTP 字符串由一个字符数量字段和 UTF-16 字符串组成，字符数量包括结尾的 `0`。

MTP 数组先返回一个 `uint32_t count`，后面再按顺序返回 `count` 个元素。

本文中的“下载”和“上传”统一以 Windows 等 USB 主机为视角。

“下载”表示主机从 MTP 设备读取文件，数据方向是 `设备 → 主机`。

“上传”表示主机向 MTP 设备写入文件，数据方向是 `主机 → 设备`。

### 设备、Storage 与 Object 的层级

理解 MTP 文件访问时只需要先记住下面的关系。

```text
设备
└── StorageID
    └── ObjectHandle
        ├── ObjectInfo：文件名、大小、格式等元数据
        └── ObjectData：文件的原始内容
```

`StorageID` 标识一个存储器，`ObjectHandle` 标识该存储器中的一个文件 Object。

主机不会直接使用 SPIFFS 路径访问文件，而是先取得 Handle，再使用 Handle 查询信息或传输数据。

### Object、ObjectInfo 与 ObjectHandle 的关系

`Object` 是 MTP 对一个可管理资源的抽象，它可以表示文件、目录或其他媒体对象。

当前项目使用扁平的 SPIFFS 文件系统，因此一个 SPIFFS 文件就是一个 MTP Object，并且当前实现不提供目录 Object。

`ObjectHandle` 是设备分配给 Object 的非零 `uint32_t` 标识符，它类似主机访问 Object 时使用的临时编号。

Handle 不是文件名、文件路径、SPIFFS 文件描述符或 USB Endpoint 编号。

`ObjectInfo` 是 Object 的元数据集合，其中包含存储器、文件格式、文件大小、父 Object 和文件名等信息。

`ObjectData` 是 Object 的实际内容，对于当前项目就是 SPIFFS 文件中的原始字节。

它们之间的关系如下。

```text
ObjectHandle ──定位──> Object
                         ├── ObjectInfo：名称、大小、格式等元数据
                         └── ObjectData：文件内容
```

主机先通过 `GetObjectHandles` 获得 Handle，然后把 Handle 传给不同操作。

```text
GetObjectInfo(Handle)       → 读取 ObjectInfo
GetObject(Handle)           → 读取全部 ObjectData
GetPartialObject(Handle)    → 读取部分 ObjectData
DeleteObject(Handle)        → 删除这个 Object
```

例如 SPIFFS 中存在 `readme.txt` 时，当前实现可能在目录表的第一个位置保存它，并为其分配 Handle `1`。

```text
SPIFFS 文件：readme.txt
        ↓ 映射为
MTP Object：文件 readme.txt
        ↓ 分配
ObjectHandle：1
        ├── GetObjectInfo(1) → Filename=readme.txt, Size=45, Format=TEXT
        └── GetObject(1)     → 返回 readme.txt 的 45 字节文件内容
```

当前实现使用 `s_storage.objects[handle - 1]` 将 Handle 映射到目录表条目，因此 Handle `1` 对应数组下标 `0`。

文件存在期间，`catalog_refresh()` 会根据文件名找到原目录表条目，从而尽量保持其 Handle 不变。

文件删除后对应 Handle 会失效，空出的编号以后可能分配给其他文件，因此主机应该重新调用 `GetObjectHandles`，不能把 Handle 当作永久 ID 保存。

上传时顺序正好相反，`SendObjectInfo` 先根据文件元数据创建 Object 并分配新 Handle，随后 `SendObject` 把文件内容写入这个 Object。

### Session 是什么

MTP Session 是主机在已经建立的 USB 连接之上开启的一段逻辑工作周期。

Session 不是 USB 物理连接、Endpoint、线程或单次文件传输。

可以把 USB 连接理解为主机和设备之间的道路，把 Session 理解为主机进入设备文件管理服务后建立的一次工作上下文。

USB 枚举完成后设备已经连接，但此时 MTP Session 仍然可以处于关闭状态。

主机通常先在 Session 外调用 `GetDeviceInfo` 查询能力，然后调用 `OpenSession(SessionID)` 开始文件操作。

```text
USB 已连接
    │
    ├── GetDeviceInfo
    │
    ├── OpenSession(SessionID = 1)
    │       ├── GetStorageIDs
    │       ├── GetObjectHandles
    │       ├── GetObject / SendObject / DeleteObject
    │       └── 其他 MTP 操作
    │
    └── CloseSession
            USB 仍然可以保持连接
```

`SessionID` 按协议应是主机选择的非零编号，用来标识本次工作周期，设备在 Session 打开期间保存这个编号。

SessionID 不标识某个文件，文件由 ObjectHandle 标识。

SessionID 也不标识某一条命令，同一 Session 中的每条 Command 使用各自的 TransactionID。

```text
SessionID：标识一整段 MTP 工作周期
TransactionID：标识工作周期中的某一次 Command/Data/Response 交互
ObjectHandle：标识被操作的某一个文件 Object
```

当前实现一次只允许打开一个 Session，再次调用 `OpenSession` 会返回 `MTP_RESP_SESSION_ALREADY_OPEN`。

调用 `CloseSession` 时，设备会中止正在进行的文件传输、丢弃尚未完成的上传状态、清除 SessionID，并回到 Session Closed 状态。

关闭 Session 不等于拔掉 USB，主机仍可在同一 USB 连接上重新调用 `OpenSession` 开启新的 Session。

USB 断开时 `tud_umount_cb()` 会调用 `mtp_storage_reset()`，因此当前 Session 和未完成的传输状态也会被清除。

## 完整交互流程

### 设备连接与存储器发现

```text
GetDeviceInfo              → mtp_get_device_info()
                            ← 设备能力、支持的操作、事件、属性和格式

OpenSession(SessionID)     → mtp_open_close_session()
                            ← MTP_RESP_OK

GetStorageIDs              → mtp_get_storage_ids()
                            ← count + StorageID 数组

GetStorageInfo(StorageID)  → mtp_get_storage_info()
                            ← 存储类型、容量、剩余空间和显示名称
```

完成这一步后，主机知道设备有哪些存储器以及每个存储器的基本信息。

### 枚举文件

```text
GetNumObjects(StorageID, Format, Parent)
    → mtp_get_num_objects()
    ← 符合条件的 Object 数量

GetObjectHandles(StorageID, Format, Parent)
    → mtp_get_object_handles()
    ← count + ObjectHandle 数组

GetObjectInfo(ObjectHandle)
    → mtp_get_object_info()
    ← 文件名、大小、格式、父对象等 ObjectInfo
```

`GetNumObjects` 只返回数量，因此它是可选查询。

`GetObjectHandles` 返回真正用于后续操作的 Handle 数组。

主机通常会对数组中的每个 Handle 调用一次 `GetObjectInfo`，从而得到资源管理器需要显示的文件名和大小。

假设设备中有两个文件，交互结果可能如下。

```text
GetObjectHandles(STORAGE_ID, 0, 0)
← count = 2, handles = [1, 2]

GetObjectInfo(1)
← Filename = readme.txt, Size = 45, Format = TEXT

GetObjectInfo(2)
← Filename = photo.jpg, Size = 102400, Format = EXIF_JPEG
```

这里的 Handle `1` 和 Handle `2` 就是后续读取、分段读取或删除文件时使用的标识。

### 主机下载文件（设备 → 主机）或删除文件

这里的“下载”是指 Windows 主机从 ESP32 MTP 设备取得文件，并保存到主机本地。

从 ESP32 设备的角度看，同一个过程也可以称为设备向主机发送文件。

```text
主机 → 设备：GetObject(ObjectHandle)
设备端处理：mtp_get_object()
设备 → 主机：Data IN，整个文件的原始字节

主机 → 设备：GetPartialObject(ObjectHandle, Offset, MaxBytes)
设备端处理：mtp_get_partial_object()
设备 → 主机：Data IN，指定范围的文件原始字节
设备 → 主机：Response Param1 = ActualBytes，表示实际返回的字节数

主机 → 设备：DeleteObject(ObjectHandle)
设备端处理：mtp_delete_object()
设备 → 主机：MTP_RESP_OK
```

`GetObjectInfo` 返回元数据，`GetObject` 和 `GetPartialObject` 返回真正的文件内容。

### 主机上传文件（主机 → 设备）

这里的“上传”是指 Windows 主机把本地文件发送给 ESP32 MTP 设备，并由设备写入 SPIFFS。

从 ESP32 设备的角度看，同一个过程也可以称为设备接收主机发送的文件。

上传必须由 `SendObjectInfo` 和 `SendObject` 两个连续操作完成。

```text
主机 → 设备：SendObjectInfo(StorageID, Parent)
主机 → 设备：Data OUT，ObjectInfo 中的文件名、大小、格式等元数据
设备端处理：mtp_send_object_info()
设备 → 主机：Response，StorageID + ParentObject + 新 ObjectHandle

主机 → 设备：SendObject
主机 → 设备：Data OUT，文件的全部原始字节
设备端处理：mtp_send_object()
设备 → 主机：MTP_RESP_OK

设备 → 主机：ObjectAdded Event + 新 ObjectHandle
设备端发送：tud_mtp_response_complete_cb()
```

`SendObjectInfo` 只传元数据并取得新 Handle，`SendObject` 才传真正的文件内容。

上传完成后的 `ObjectAdded` Event 使用同一个 Handle 通知主机新增文件已经可用。

### 关闭会话

```text
CloseSession → mtp_open_close_session()
             ← MTP_RESP_OK
```

完整主线可以压缩成下面一行。

```text
GetDeviceInfo → OpenSession → GetStorageIDs → GetStorageInfo → GetObjectHandles → GetObjectInfo → GetObject/SendObject/DeleteObject → CloseSession
```

## API 参考

### 设备能力与 Session

#### GetDeviceInfo — `mtp_get_device_info()`

主机目的：查询设备身份和设备支持的 MTP 操作、事件、属性与文件格式。

Command 参数：无。

Data IN 字段按下面的顺序返回。

| 字段 | 含义 |
| --- | --- |
| `StandardVersion` | 设备遵循的标准版本。 |
| `MTPVendorExtensionID` | MTP 厂商扩展标识。 |
| `MTPVersion` | MTP 协议版本。 |
| `MTPExtensions` | 设备声明的 MTP 扩展字符串。 |
| `FunctionalMode` | 设备当前的功能模式。 |
| `SupportedOperations` | 设备支持的 MTP Operation Code 数组。 |
| `SupportedEvents` | 设备支持的 MTP Event Code 数组。 |
| `SupportedDeviceProperties` | 设备支持的 Device Property Code 数组。 |
| `CaptureFormats` | 设备声明支持接收或创建的 Object Format Code 数组。 |
| `PlaybackFormats` | 设备声明支持读取或提供的 Object Format Code 数组。 |
| `Manufacturer` | 厂商字符串，当前为 `TinyUSB`。 |
| `Model` | 型号字符串，当前为 `ESP SPIFFS MTP`。 |
| `DeviceVersion` | 设备版本字符串，当前为 `1.0`。 |
| `SerialNumber` | MTP 设备序列号字符串，当前为 `123456`。 |

下面四次调用依次追加 `Manufacturer`、`Model`、`DeviceVersion` 和 `SerialNumber`。

```c
mtp_container_add_cstring(container, DEVICE_INFO_MANUFACTURER);
mtp_container_add_cstring(container, DEVICE_INFO_MODEL);
mtp_container_add_cstring(container, DEVICE_INFO_VERSION);
mtp_container_add_cstring(container, DEVICE_INFO_SERIAL);
```

Response：`MTP_RESP_OK`，没有额外 Response 参数。

#### OpenSession — `mtp_open_close_session()`

主机目的：打开一个 MTP Session，后续文件操作在这个 Session 中进行。

| Command 参数 | 含义 |
| --- | --- |
| `Param1: SessionID` | 主机分配的 Session 标识。 |

Response：成功时返回 `MTP_RESP_OK`，已经存在 Session 时返回 `MTP_RESP_SESSION_ALREADY_OPEN`。

#### CloseSession — `mtp_open_close_session()`

主机目的：关闭当前 MTP Session。

Command 参数：无。

Response：成功时返回 `MTP_RESP_OK`，没有打开 Session 时返回 `MTP_RESP_SESSION_NOT_OPEN`。

### 存储器操作

#### GetStorageIDs — `mtp_get_storage_ids()`

主机目的：查询设备提供了哪些逻辑存储器。

Command 参数：无。

下面的调用返回一个 `uint32_t` 数组。

```c
mtp_container_add_auint32(&cb_data->io_container, TU_ARRAY_SIZE(storage_ids), storage_ids);
```

| Data IN 字段 | 含义 |
| --- | --- |
| `count` | Storage ID 的数量，当前为 `1`。 |
| `StorageID[0]` | 当前唯一的 Storage ID，值为 `0x00010001`。 |

Response：`MTP_RESP_OK`，没有额外 Response 参数。

#### GetStorageInfo — `mtp_get_storage_info()`

主机目的：查询指定存储器的类型、访问能力、容量和名称。

| Command 参数 | 含义 |
| --- | --- |
| `Param1: StorageID` | 需要查询的存储器 ID。 |

下面的调用一次性追加整个 StorageInfo Dataset。

```c
mtp_container_add_raw(&cb_data->io_container, &storage_info, sizeof(storage_info));
```

| Data IN 字段 | 含义 |
| --- | --- |
| `StorageType` | 存储器是固定或可移除，以及只读或可写，当前为固定可读写。 |
| `FilesystemType` | 主机看到的逻辑对象组织方式，当前为没有目录层级的 `GENERIC_FLAT`。 |
| `AccessCapability` | 主机对对象的访问权限，当前为读写。 |
| `MaxCapacity` | 存储器总容量，单位为字节。 |
| `FreeSpaceInBytes` | 存储器剩余容量，单位为字节。 |
| `FreeSpaceInObjects` | 还能创建多少个 MTP Object。 |
| `StorageDescription` | 主机显示的存储名称，当前为 `Internal Storage`。 |
| `VolumeIdentifier` | 存储卷标识字符串，当前为 `ESP32`。 |

Response：Storage ID 有效时返回 `MTP_RESP_OK`，无效时返回 `MTP_RESP_INVALID_STORAGE_ID`。

### 设备属性

#### GetDevicePropDesc — `mtp_get_device_property()`

主机目的：查询某个设备属性的数据类型、读写权限、默认值和当前值。

| Command 参数 | 含义 |
| --- | --- |
| `Param1: DevicePropertyCode` | 需要查询的设备属性，当前只支持 `DEVICE_FRIENDLY_NAME`。 |

下面的调用先追加 Device Property Description Header。

```c
mtp_container_add_raw(container, &descriptor, sizeof(descriptor));
```

| Data IN 字段 | 含义 |
| --- | --- |
| `DevicePropertyCode` | 当前描述的是哪个设备属性。 |
| `DataType` | 属性值的数据类型，当前为 MTP 字符串。 |
| `GetSet` | 属性是只读还是可修改，当前为只读。 |

下面两个字符串必须依次返回 Factory Default Value 和 Current Value。

```c
mtp_container_add_cstring(container, DEVICE_FRIENDLY_NAME);
mtp_container_add_cstring(container, DEVICE_FRIENDLY_NAME);
```

| Data IN 字段 | 含义 |
| --- | --- |
| `FactoryDefaultValue` | 属性的出厂默认值，当前为 `ESP SPIFFS MTP`。 |
| `CurrentValue` | 属性的当前值，当前为 `ESP SPIFFS MTP`。 |

最后追加的 `0` 是 Form Flag。

```c
mtp_container_add_uint8(container, 0);
```

| Form Flag | 含义 |
| --- | --- |
| `0` | 属性没有额外约束。 |
| `1` | 属性值受 Range 约束。 |
| `2` | 属性值只能来自 Enumeration。 |

Response：属性受支持时返回 `MTP_RESP_OK`，否则返回 `MTP_RESP_DEVICE_PROP_NOT_SUPPORTED`。

#### GetDevicePropValue — `mtp_get_device_property()`

主机目的：只查询某个设备属性的当前值。

| Command 参数 | 含义 |
| --- | --- |
| `Param1: DevicePropertyCode` | 需要查询的设备属性，当前只支持 `DEVICE_FRIENDLY_NAME`。 |

下面的调用只追加 Current Value，不需要返回属性描述和 Factory Default Value。

```c
mtp_container_add_cstring(container, DEVICE_FRIENDLY_NAME);
```

| Data IN 字段 | 含义 |
| --- | --- |
| `CurrentValue` | 属性当前值，当前为 `ESP SPIFFS MTP`。 |

Response：属性受支持时返回 `MTP_RESP_OK`，否则返回 `MTP_RESP_DEVICE_PROP_NOT_SUPPORTED`。

### Object 枚举与读取

#### GetNumObjects — `mtp_get_num_objects()`

主机目的：统计符合指定存储器、格式和父对象条件的 Object 数量。

| Command 参数 | 含义 |
| --- | --- |
| `Param1: StorageID` | 需要查询的存储器，`0xFFFFFFFF` 表示所有存储器。 |
| `Param2: ObjectFormatCode` | 需要统计的对象格式，`0` 表示所有格式。 |
| `Param3: AssociationOH` | 父对象或目录 Handle，`0` 表示不按父对象过滤，`0xFFFFFFFF` 表示根目录。 |

下面的调用把数量追加为 Response 的第一个参数。

```c
mtp_container_add_uint32(&cb_data->io_container, count);
```

| Response 参数 | 含义 |
| --- | --- |
| `Param1: NumObjects` | 符合条件的 Object 数量。 |

这个操作没有 Data 阶段。

#### GetObjectHandles — `mtp_get_object_handles()`

主机目的：取得符合指定条件的全部 Object Handle，以便继续查询或操作每个 Object。

| Command 参数 | 含义 |
| --- | --- |
| `Param1: StorageID` | 需要查询的存储器，`0xFFFFFFFF` 表示所有存储器。 |
| `Param2: ObjectFormatCode` | 需要查询的对象格式，`0` 表示所有格式。 |
| `Param3: AssociationOH` | 父对象或目录 Handle，`0` 表示不按父对象过滤，`0xFFFFFFFF` 表示根目录。 |

下面的调用返回一个 `uint32_t` Handle 数组。

```c
mtp_container_add_auint32(&cb_data->io_container, count, handles);
```

| Data IN 字段 | 含义 |
| --- | --- |
| `count` | 返回的 Object Handle 数量。 |
| `handles[0..count-1]` | 每个符合条件的 Object Handle。 |

Response：成功时返回 `MTP_RESP_OK`，没有额外 Response 参数。

#### GetObjectInfo — `mtp_get_object_info()`

主机目的：根据 Object Handle 查询文件名、大小、格式、父对象和媒体信息。

| Command 参数 | 含义 |
| --- | --- |
| `Param1: ObjectHandle` | 需要查询的 Object Handle。 |

下面的调用先追加固定长度的 ObjectInfo Header。

```c
mtp_container_add_raw(&cb_data->io_container, &object_info, sizeof(object_info));
```

| ObjectInfo Header 字段 | 含义 |
| --- | --- |
| `StorageID` | Object 所属的存储器 ID。 |
| `ObjectFormat` | Object 的格式，例如 TEXT、JPEG、PNG 或 MP3。 |
| `ProtectionStatus` | Object 是否受到只读保护，当前为不保护。 |
| `ObjectCompressedSize` | Object 内容大小，单位为字节，对于普通文件就是文件大小。 |
| `ThumbFormat` | 缩略图格式，当前为 `UNDEFINED`。 |
| `ThumbCompressedSize` | 缩略图数据大小，当前为 `0`。 |
| `ThumbPixWidth` | 缩略图宽度，当前为 `0`。 |
| `ThumbPixHeight` | 缩略图高度，当前为 `0`。 |
| `ImagePixWidth` | 图像宽度，当前没有解析媒体信息，所以为 `0`。 |
| `ImagePixHeight` | 图像高度，当前没有解析媒体信息，所以为 `0`。 |
| `ImageBitDepth` | 图像位深，当前没有解析媒体信息，所以为 `0`。 |
| `ParentObject` | 父目录 Object Handle，当前文件都位于根目录，所以为 `0`。 |
| `AssociationType` | Object 是否是目录等 Association Object，当前为 `UNDEFINED`。 |
| `AssociationDesc` | Association 的附加描述，当前为 `0`。 |
| `SequenceNumber` | Object 序号，当前未使用，所以为 `0`。 |

ObjectInfo Header 后面必须继续按固定顺序返回四个 MTP 字符串。

```c
mtp_container_add_string(&cb_data->io_container, filename);
mtp_container_add_cstring(&cb_data->io_container, "");
mtp_container_add_cstring(&cb_data->io_container, "");
mtp_container_add_cstring(&cb_data->io_container, "");
```

| 字符串顺序 | 含义 |
| --- | --- |
| 第一个字符串 | `Filename`，Object 的文件名。 |
| 第二个字符串 | `DateCreated`，创建时间，当前返回空字符串。 |
| 第三个字符串 | `DateModified`，修改时间，当前返回空字符串。 |
| 第四个字符串 | `Keywords`，关键字，当前返回空字符串。 |

Response：Handle 有效时返回 `MTP_RESP_OK`，无效时返回 `MTP_RESP_INVALID_OBJECT_HANDLE`。

#### GetObject — `mtp_get_object()`

主机目的：根据 Object Handle 下载整个文件内容。

| Command 参数 | 含义 |
| --- | --- |
| `Param1: ObjectHandle` | 需要下载的 Object Handle。 |

Data IN Payload：从文件偏移 `0` 开始的全部原始文件字节。

Data IN 不包含文件名、文件大小或格式，这些元数据需要先通过 `GetObjectInfo` 查询。

Response：传输完成时返回 `MTP_RESP_OK`，Handle 无效时返回 `MTP_RESP_INVALID_OBJECT_HANDLE`。

#### GetPartialObject — `mtp_get_partial_object()`

主机目的：从指定文件偏移开始读取最多指定数量的文件字节。

| Command 参数 | 含义 |
| --- | --- |
| `Param1: ObjectHandle` | 需要读取的 Object Handle。 |
| `Param2: Offset` | 开始读取的字节偏移。 |
| `Param3: MaxBytes` | 主机希望读取的最大字节数。 |

Data IN Payload：从 `Offset` 开始的原始文件字节，长度不会超过 `MaxBytes`，也不会超过文件末尾。

数据阶段完成后，下面的调用追加实际发送长度作为 Response 参数。

```c
mtp_container_add_uint32(response, cb_data->total_xferred_bytes - sizeof(mtp_container_header_t));
```

| Response 参数 | 含义 |
| --- | --- |
| `Param1: ActualBytes` | 本次实际返回的文件数据字节数，不包括 MTP Container Header。 |

### Object 删除与上传

#### DeleteObject — `mtp_delete_object()`

主机目的：删除指定 Object Handle 对应的文件。

| Command 参数 | 含义 |
| --- | --- |
| `Param1: ObjectHandle` | 需要删除的 Object Handle。 |
| `Param2: ObjectFormatCode` | 可选的格式过滤参数，当前实现没有使用这个参数。 |

这个操作没有 Data 阶段，也没有成功 Response 参数。

Response：删除成功时返回 `MTP_RESP_OK`，Handle 无效时返回 `MTP_RESP_INVALID_OBJECT_HANDLE`，无法删除时返回 `MTP_RESP_OBJECT_WRITE_PROTECTED`。

#### SendObjectInfo — `mtp_send_object_info()`

主机目的：上传文件内容之前，先把新 Object 的元数据发送给设备并取得设备分配的 Handle。

| Command 参数 | 含义 |
| --- | --- |
| `Param1: StorageID` | 新 Object 要保存到的存储器，`0xFFFFFFFF` 表示由设备选择。 |
| `Param2: ParentObject` | 新 Object 的父目录 Handle，`0` 或 `0xFFFFFFFF` 表示根目录。 |

Data OUT：主机发送一个 ObjectInfo Dataset，固定头字段与 `GetObjectInfo` 中的 ObjectInfo Header 相同。

当前上传流程主要使用下面的字段。

| Data OUT 字段 | 含义 |
| --- | --- |
| `StorageID` | 新 Object 要保存到的存储器。 |
| `ObjectFormat` | 新 Object 的文件格式。 |
| `ObjectCompressedSize` | 主机随后会通过 `SendObject` 发送的文件内容总字节数。 |
| `ParentObject` | 新 Object 的父目录 Handle。 |
| `AssociationType` | 是否是目录等 Association Object，当前不接受目录 Object。 |
| `Filename` | 新 Object 的文件名，位于固定 ObjectInfo Header 后面的第一个 MTP 字符串。 |
| `DateCreated` | 创建时间，当前实现不使用。 |
| `DateModified` | 修改时间，当前实现不使用。 |
| `Keywords` | 关键字，当前实现不使用。 |

Data OUT 接收并校验成功后，Response 按下面的顺序追加三个参数。

```c
mtp_container_add_uint32(response, STORAGE_ID);
mtp_container_add_uint32(response, upload_info.parent);
mtp_container_add_uint32(response, upload_info.handle);
```

| Response 参数 | 含义 |
| --- | --- |
| `Param1: StorageID` | 设备最终选择的存储器 ID。 |
| `Param2: ParentObject` | 设备最终选择的父目录 Handle，当前为根目录。 |
| `Param3: ObjectHandle` | 设备为新 Object 分配的 Handle，后续 `SendObject` 与事件会使用它。 |

#### SendObject — `mtp_send_object()`

主机目的：在 `SendObjectInfo` 成功后发送新 Object 的实际文件内容。

Command 参数：无。

Data OUT Payload：文件的全部原始字节，预期总长度来自前一个 `SendObjectInfo` 的 `ObjectCompressedSize`。

Response：文件完整写入时返回 `MTP_RESP_OK`，没有有效 ObjectInfo 时返回 `MTP_RESP_NO_VALID_OBJECTINFO`，空间不足时返回 `MTP_RESP_STORE_FULL`。

### Event 与设备状态请求

#### ObjectAdded Event — `tud_mtp_response_complete_cb()`

设备目的：文件上传成功后，主动通知主机出现了一个新 Object。

| Event 字段 | 含义 |
| --- | --- |
| `Code` | 事件码，当前为 `MTP_EVENT_OBJECT_ADDED`。 |
| `SessionID` | 新 Object 所属交互使用的 Session ID。 |
| `TransactionID` | 触发新 Object 的 MTP Transaction ID。 |
| `Param1: ObjectHandle` | 新增 Object 的 Handle。 |
| `Param2` | 当前未使用，为 `0`。 |
| `Param3` | 当前未使用，为 `0`。 |

主机收到 `ObjectAdded` 后，可以使用 Event 中的 Handle 继续发送 `GetObjectInfo`。

#### GetDeviceStatus — `tud_mtp_request_get_device_status_cb()`

主机目的：通过 MTP Class Control Request 查询设备当前状态。

设备当前返回两个连续的 `uint16_t` 字段。

```c
const uint16_t status[] = { 4, MTP_RESP_OK };
```

| 返回字段 | 含义 |
| --- | --- |
| `Length` | 状态数据总长度，当前为 `4` 字节。 |
| `ResponseCode` | 设备当前状态，`MTP_RESP_OK` 表示可以继续处理命令。 |
