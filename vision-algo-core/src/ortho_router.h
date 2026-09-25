// ortho_router.h — 正交(可拐弯)路径规划
//
// 给定起点、终点以及一组矩形障碍物（节点），规划一条只含水平/竖直段的
// 折线，使路径不穿过任何障碍物（外扩 padding 后）。用于节点编辑器连线。
//
#pragma once
#include <vector>
#include <utility>

namespace vz {

struct RectD {
    double x, y, w, h;   // 左上角 + 宽高
};

// 返回有序点列（含起点与终点）。失败时返回回退的 L 形路径，不会为空。
//   padding   : 障碍物外扩距离（走线与节点保持的间距）
//   grid_step : 搜索网格步长（像素），内部会按包围盒自适应放大以限制规模
std::vector<std::pair<double, double>>
routeOrthogonal(double sx, double sy, double tx, double ty,
                const std::vector<RectD>& obstacles,
                double padding, double grid_step);

} // namespace vz
