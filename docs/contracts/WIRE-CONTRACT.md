# 线协议观察索引

ES-009 的协议索引。这里记录 `CHNB-NEW-6MIC` @ `fbecac9f54d378d212794fd937e950d9d2acd58d` 已跟踪编解码器里能直接看到的帧格式。已批准黄金向量数量是 0。本文件不包含由编解码器计算出来的期望 HEX。

## 状态词

| 状态 | 含义 |
|---|---|
| 旧版观察 | 在上述提交的已跟踪源码中读到的实现 |
| Needs Evidence | 还没有抓包、固件确认或带审核人的人工复核，不能当作已冻结的线字节 |

旧版观察不是黄金向量。`tests/test_br_protocol.py` 用同一套函数编码后再解码，只能说明旧代码前后一致。

## 已看到的规则

| 项 | 旧版观察 | 出处 | 状态 |
|---|---|---|---|
| 外层帧 | 1 字节长度后接 Modbus 帧。长度等于后续字节数，最大 255。不把帧补齐到 256 字节 | `br_protocol/packet_256.py` 的 `build_packet`；`br_protocol/constants.py` 的 `MAX_MODBUS_PAYLOAD` | Needs Evidence |
| 从站地址 | 默认 `0x01` | `br_protocol/constants.py` 的 `DEFAULT_SLAVE_ADDR` | Needs Evidence |
| FC03 | 功能码 `0x03`，读保持寄存器 | `br_protocol/modbus_rtu.py` 的 `ModbusFunction.READ_HOLDING_REGISTERS` | Needs Evidence |
| FC16 | 功能码 `0x10`，写多个寄存器 | 同文件 `WRITE_MULTIPLE_REGISTERS` | Needs Evidence |
| 报文头字节序 | 从站、功能码、起始寄存器和数量使用 `struct.pack(">BBHH")` | `build_read_registers`、`build_write_registers` | Needs Evidence |
| CRC | 初值 `0xFFFF`，对每个字节做右移，最低位为 1 时异或 `0xA001`。结果以 little-endian 两字节附在帧尾 | `calc_crc16`、`append_crc16` | Needs Evidence |
| slot | 逻辑地址到起始寄存器的换算是地址乘 2。读请求的寄存器数量是 slot 数乘 2 | `br_protocol/rnc_codec.py` 的 `float_addr_to_start_register` 与 `encode_request` | Needs Evidence |
| 负载字节序 | `float`、`int32`、`uint32` 各占 4 字节，按 little-endian 放入 FC03 负载和 FC16 数据区 | `unpack_slots`、`build_write_registers` | Needs Evidence |
| 分包上限 | 读默认 60 slot，读最多 62 slot，写最多 61 slot | `DEFAULT_BULK_SLOTS`、`MAX_BULK_SLOTS_READ`、`MAX_BULK_SLOTS_WRITE` | Needs Evidence |

读请求由 `encode_request` 先组装 Modbus 帧，再交给 `build_packet` 加上长度字节。写请求同样经过这两步。FC16 的寄存器数量按每个值占 2 个寄存器计算。

## 还没有冻结的内容

下面这些不在本次写成已批准字节：

- 任一请求或响应的原始 HEX。
- 某个具体浮点、整数在线上的示例字节。
- CRC 对某一帧的计算结果。
- 固件版本、设备型号和审核人。这些字段要等真实样本进入 `tests/fixtures/protocol/golden/README.md` 才填写。

新编码器的输出不能补进黄金目录来充当上面缺失的证据。后续协议测试只使用该目录中已批准的条目作为期望值。
