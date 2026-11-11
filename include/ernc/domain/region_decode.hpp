#pragma once

#include "ernc/domain/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ernc::domain {

/// 一个区域字段解出的浮点。对象拥有路径、维度和数值。
struct DecodedField {
    /// 字段的完整固件路径。
    std::string path;

    /// 各级长度，与目录里的 shape 相同。空表示这一个 float 没有数组维度。
    /// 这里不给轴命名。
    std::vector<std::uint32_t> shape;

    /// 按字节出现顺序排列的 float。个数等于 slot 数。
    std::vector<float> values;
};

/// 一块区域解开后的全部字段。对象拥有这些字段。
struct DecodedRegion {
    /// 父区域路径。ERNC_TF 或 check_data。
    std::string path;

    /// 按目录中的子字段顺序排列。失败的解码不会留下半份。
    std::vector<DecodedField> fields;
};

}  // namespace ernc::domain
