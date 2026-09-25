// ortho_router.cpp — 正交(可拐弯)、绕开节点的连线路由
//
// 思路：生成若干候选折线路径（直线 / L 形 / 绕障碍边界 / 绕外角），
// 逐一判定是否与任何节点矩形相交，选出“拐弯最少、长度最短”的合法路径。
// 保证：路径只含水平/竖直段，且不穿过任何节点（起止端口所在节点的边界除外）。
//
#include "ortho_router.h"

#include <cmath>
#include <vector>
#include <algorithm>

namespace vz {

namespace {

struct Box { double x, y, w, h; };

using Pt = std::pair<double, double>;

inline double routeLen(const std::vector<Pt>& p) {
    double s = 0;
    for (size_t i = 1; i < p.size(); ++i)
        s += std::fabs(p[i].first - p[i - 1].first) +
             std::fabs(p[i].second - p[i - 1].second);
    return s;
}

} // namespace

std::vector<Pt>
routeOrthogonal(double sx, double sy, double tx, double ty,
                const std::vector<RectD>& obstacles,
                double padding, double /*grid_step*/) {
    if (padding < 0) padding = 0.0;
    if (std::fabs(sx - tx) < 1e-6 && std::fabs(sy - ty) < 1e-6)
        return {{sx, sy}};

    // 其他节点：外扩 padding 保持间距；起止节点：用原始尺寸（端口恰在边界上）
    std::vector<Box> obsI; // 外扩后
    std::vector<Box> obsO; // 原始
    for (const auto& r : obstacles) {
        if (r.w <= 0 || r.h <= 0) continue;
        obsO.push_back({r.x, r.y, r.w, r.h});
        obsI.push_back({r.x - padding, r.y - padding, r.w + 2 * padding, r.h + 2 * padding});
    }
    int srcIdx = -1, tgtIdx = -1;
    for (int i = 0; i < (int)obsI.size(); ++i) {
        const Box& b = obsI[i];
        if (srcIdx < 0 && sx >= b.x && sx <= b.x + b.w && sy >= b.y && sy <= b.y + b.h) srcIdx = i;
        if (tgtIdx < 0 && tx >= b.x && tx <= b.x + b.w && ty >= b.y && ty <= b.y + b.h) tgtIdx = i;
    }

    // 判定一条轴对齐线段是否被某个节点阻挡（严格相交，边界相切允许）
    // 起终点已由上层对齐到节点边界，故此处对所有节点一视同仁地严格判定。
    auto segBlocked = [&](double x1, double y1, double x2, double y2) -> bool {
        const bool vert = (std::fabs(x1 - x2) < 1e-9);
        const double lo = vert ? std::min(y1, y2) : std::min(x1, x2);
        const double hi = vert ? std::max(y1, y2) : std::max(x1, x2);
        const double a = vert ? x1 : y1;
        for (int i = 0; i < (int)obsI.size(); ++i) {
            // 所有节点统一外扩 padding：连线端点由上层推出节点外，
            // 故任何线段都不得贴近/穿过任一节点（含源、目标节点本体）。
            const Box& b = obsI[i];
            if (vert) {
                if (a > b.x && a < b.x + b.w && hi > b.y && lo < b.y + b.h) return true;
            } else {
                if (a > b.y && a < b.y + b.h && hi > b.x && lo < b.x + b.w) return true;
            }
        }
        return false;
    };

    auto validRoute = [&](const std::vector<Pt>& pts) -> bool {
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const Pt& p = pts[i], q = pts[i + 1];
            const bool vert = (std::fabs(p.first - q.first) < 1e-9);
            const bool horz = (std::fabs(p.second - q.second) < 1e-9);
            if (!vert && !horz) return false;          // 非正交
            if (vert && horz) continue;                  // 重合点
            if (segBlocked(p.first, p.second, q.first, q.second)) return false;
        }
        return true;
    };

    // 障碍整体包围盒（用于绕外围的候选通道）
    double gx1 = sx, gx2 = sx, gy1 = sy, gy2 = sy;
    for (const auto& b : obsI) {
        gx1 = std::min(gx1, b.x); gx2 = std::max(gx2, b.x + b.w);
        gy1 = std::min(gy1, b.y); gy2 = std::max(gy2, b.y + b.h);
    }
    const double gap = padding + 20.0;
    const double xR = gx2 + gap, xL = gx1 - gap;
    const double yB = gy2 + gap, yT = gy1 - gap;

    std::vector<std::vector<Pt>> cands;

    // 1) 直线
    if (std::fabs(sx - tx) < 1e-6 || std::fabs(sy - ty) < 1e-6)
        cands.push_back({{sx, sy}, {tx, ty}});

    // 2) L 形（两种拐角）
    cands.push_back({{sx, sy}, {tx, sy}, {tx, ty}});
    cands.push_back({{sx, sy}, {sx, ty}, {tx, ty}});

    // 3) 绕单边（3 段）
    for (double x : {xR, xL})
        cands.push_back({{sx, sy}, {x, sy}, {x, ty}, {tx, ty}});
    for (double y : {yB, yT})
        cands.push_back({{sx, sy}, {sx, y}, {tx, y}, {tx, ty}});

    // 4) 绕外角（4 段）—— 对应“右出→下绕→左进”这类外围走线
    for (double x : {xR, xL})
        for (double y : {yB, yT})
            cands.push_back({{sx, sy}, {x, sy}, {x, y}, {tx, y}, {tx, ty}});

    // 4b) 绕单个障碍节点（更短的局部绕行，避免动不动绕全局大圈）
    for (const auto& b : obsI) {
        double ox1 = b.x - gap, ox2 = b.x + b.w + gap;
        double oy1 = b.y - gap, oy2 = b.y + b.h + gap;
        for (double x : {ox1, ox2}) {
            cands.push_back({{sx, sy}, {x, sy}, {x, ty}, {tx, ty}});
            for (double y : {oy1, oy2})
                cands.push_back({{sx, sy}, {x, sy}, {x, y}, {tx, y}, {tx, ty}});
        }
        for (double y : {oy1, oy2})
            cands.push_back({{sx, sy}, {sx, y}, {tx, y}, {tx, ty}});
    }

    // 5) 兜底：沿障碍包围盒外围绕一整圈（一定不穿节点）
    cands.push_back({{sx, sy}, {sx, yT}, {xR, yT}, {xR, yB}, {tx, yB}, {tx, ty}});

    // 推断起止端口朝向：让连线顺着端口方向出线/进线（右端口向右出等）
    auto sideOut = [&](int idx, double px, double py) -> int {
        // 返回 1=R,2=L,3=U,4=D,0=未知
        if (idx < 0) return 0;
        const Box& b = obsO[idx];
        if (px >= b.x + b.w - 1.0) return 1;
        if (px <= b.x + 1.0) return 2;
        if (py <= b.y + 1.0) return 3;
        if (py >= b.y + b.h - 1.0) return 4;
        return 0;
    };
    const int sOut = sideOut(srcIdx, sx, sy);
    const int tIn = [&](){ int o = sideOut(tgtIdx, tx, ty);
        return o == 1 ? 2 : o == 2 ? 1 : o == 3 ? 4 : o == 4 ? 3 : 0; }(); // 进线方向=端口朝外的反向
    auto segDir = [](double ax, double ay, double bx, double by) -> int {
        if (std::fabs(bx - ax) > std::fabs(by - ay)) return (bx > ax) ? 1 : 2;
        if (std::fabs(by - ay) > 1e-9) return (by > ay) ? 4 : 3;
        return 0;
    };

    // 选合法、拐弯最少、最短；优先顺着端口朝向
    std::vector<Pt> best;
    double bestScore = 1e30;
    for (auto& c : cands) {
        if (!validRoute(c) || c.size() < 2) continue;
        double score = (double)c.size() * 1e6 + routeLen(c);
        if (sOut && segDir(c[0].first, c[0].second, c[1].first, c[1].second) == sOut) score -= 40;
        size_t m = c.size();
        if (tIn && segDir(c[m - 2].first, c[m - 2].second, c[m - 1].first, c[m - 1].second) == tIn) score -= 40;
        if (score < bestScore) { bestScore = score; best = c; }
    }

    if (best.empty())
        best = {{sx, sy}, {xR, sy}, {xR, yB}, {tx, yB}, {tx, ty}};
    return best;
}

} // namespace vz
