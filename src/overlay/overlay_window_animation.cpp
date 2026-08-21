#include "overlay_window_internal.hpp"

#include <cmath>

namespace kf2::overlay::detail {

bool same_rect(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}
RECT visibility_pose(const RECT& bounds, float scale, LONG outward) {
    const LONG center_x = (bounds.left + bounds.right) / 2;
    const LONG center_y = (bounds.top + bounds.bottom) / 2;
    const LONG half_width = static_cast<LONG>(std::lround(
        (bounds.right - bounds.left) * 0.5F * scale));
    const LONG half_height = static_cast<LONG>(std::lround(
        (bounds.bottom - bounds.top) * 0.5F * scale));
    RECT posed{center_x - half_width, center_y - half_height,
               center_x + half_width, center_y + half_height};
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST), &monitor)) {
        const LONG monitor_x = (monitor.rcWork.left + monitor.rcWork.right) / 2;
        const LONG monitor_y = (monitor.rcWork.top + monitor.rcWork.bottom) / 2;
        OffsetRect(&posed, center_x < monitor_x ? -outward : outward,
                   center_y < monitor_y ? -outward : outward);
    }
    return posed;
}

}  // namespace kf2::overlay::detail