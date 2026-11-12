#pragma once

namespace ernc::domain {

/// 调用方提供的门禁事实。不读取 ParameterRuntimeStore 或串口。
struct WorkflowGateContext {
    /// 设备会话是否已连接。断开时读/写步骤不能完成。
    bool deviceConnected{false};

    /// 当前活动步关联的公式或映射是否已被负责人确认。
    bool formulaConfirmed{false};

    /// 调用方是否禁止本步写入（例如只读会话）。
    bool writeForbidden{false};
};

}  // namespace ernc::domain
