// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QPointF>
#include <QRectF>
#include <QVariantMap>
#include <deque>

namespace GoshosDock {

struct Settings {
    bool manualHide = false;
    bool dockFixed = false;
    bool autoHide = true;
    bool intelliHide = true;
    int intelliHideMode = 1;
    bool pressureToShow = true;
    double pressureThreshold = 100;
    int showDelay = 250;
    int hideDelay = 200;
    int animationTime = 200;
    bool hideInFullscreen = true;
    bool showDockUrgent = true;
    int edge = 2; // top, right, bottom, left
    static Settings fromMap(const QVariantMap &map);
};

struct Environment {
    bool overlap = false;
    bool fullscreen = false;
    bool hover = false;
    bool holdVisible = false;
    bool urgent = false;
    bool locked = false;
    bool overview = false;
    bool revealBand = false;
};

/** Pure per-dock state machine. All times are monotonic milliseconds. */
class Policy {
public:
    void configure(const Settings &settings);
    bool update(const Environment &environment, qint64 now);
    // Return true if this motion triggers edge reveal. Parallel motion and
    // absolute/warped input never contribute pressure.
    bool motion(bool atEdge, double outward, double parallel, bool relative,
                bool buttonsDown, const Environment &environment, qint64 now);
    void cancelEdge();
    bool visible() const { return m_visible; }
    bool canReveal(const Environment &environment) const;
    const Settings &settings() const { return m_settings; }
    static bool onEdge(const QPointF &point, const QRectF &output, const QRectF &workArea, int edge);
    static double outwardDelta(const QPointF &delta, int edge);

private:
    Settings m_settings;
    Environment m_environment;
    bool m_visible = true;
    bool m_atEdge = false;
    bool m_triggered = false;
    bool m_edgeRevealed = false;
    qint64 m_edgeSince = -1;
    qint64 m_revealUntil = -1;
    qint64 m_hideAt = -1;
    std::deque<std::pair<qint64, double>> m_pressure;
};

} // namespace GoshosDock
