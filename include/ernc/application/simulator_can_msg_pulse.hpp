#pragma once

#include "ernc/domain/firmware_layout.hpp"

namespace ernc::simulator {
class SlaveImage;
}

namespace ernc::application {

/// @brief 把固件 Can_Msg float[72] 整段写成 0 或 1（按 float32 线编码）。
/// @param image 从站镜像；成功时被原地修改。
/// @param layout 用于 locate Can_Msg。
/// @param high true 写 1.0f，false 写 0.0f。
/// @return 布局可解析且镜像长度足够时为 true。
bool writeCanMsgMirrorLevel(ernc::simulator::SlaveImage& image, const ernc::domain::FirmwareLayout& layout, bool high);

}  // namespace ernc::application
