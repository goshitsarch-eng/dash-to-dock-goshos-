// SPDX-License-Identifier: GPL-2.0-or-later
#include "dockpolicy.h"
#include <algorithm>
#include <cmath>

namespace GoshosDock {
Settings Settings::fromMap(const QVariantMap &map)
{
    Settings s;
    auto boolean = [&map](const char *key, bool fallback) { return map.value(QLatin1String(key), fallback).toBool(); };
    auto number = [&map](const char *key, double fallback, double low, double high) {
        const double value = map.value(QLatin1String(key), fallback).toDouble();
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    s.manualHide = boolean("manualHide", s.manualHide);
    s.dockFixed = boolean("dockFixed", s.dockFixed);
    s.autoHide = boolean("autoHide", s.autoHide);
    s.intelliHide = boolean("intelliHide", s.intelliHide);
    s.intelliHideMode = number("intelliHideMode", 1, 0, 3);
    s.pressureToShow = boolean("pressureToShow", s.pressureToShow);
    s.pressureThreshold = number("pressureThreshold", 100, 1, 1000);
    s.showDelay = qRound(number("showDelay", .25, 0, 60) * 1000);
    s.hideDelay = qRound(number("hideDelay", .2, 0, 60) * 1000);
    s.animationTime = qRound(number("animationTime", .2, 0, 10) * 1000);
    s.hideInFullscreen = boolean("hideInFullscreen", s.hideInFullscreen);
    s.showDockUrgent = boolean("showDockUrgent", s.showDockUrgent);
    s.edge = number("dockPosition", 2, 0, 3);
    return s;
}

void Policy::configure(const Settings &settings)
{
    m_settings = settings;
    cancelEdge();
    m_hideAt = -1;
    m_edgeRevealed = false;
}

bool Policy::canReveal(const Environment &e) const
{
    return !m_settings.manualHide && !m_settings.dockFixed && m_settings.autoHide
        && !e.locked && !(e.fullscreen && m_settings.hideInFullscreen);
}

void Policy::cancelEdge()
{
    m_atEdge = false;
    m_triggered = false;
    m_edgeSince = -1;
    m_pressure.clear();
}

bool Policy::motion(bool atEdge, double outward, double parallel, bool relative,
                    bool buttonsDown, const Environment &e, qint64 now)
{
    if (!atEdge || buttonsDown || !canReveal(e)) {
        cancelEdge();
        return false;
    }
    if (!m_atEdge) {
        m_atEdge = true;
        m_edgeSince = now;
    }
    if (!m_settings.pressureToShow || m_triggered || !relative || outward <= 0 || std::abs(parallel) > outward)
        return false;
    // GNOME PressureBarrier sums capped outward deltas in show-delay's
    // rolling window, while an individual motion above threshold triggers.
    while (!m_pressure.empty() && now - m_pressure.front().first > m_settings.showDelay)
        m_pressure.pop_front();
    m_pressure.emplace_back(now, std::min(outward, 15.0));
    double sum = 0;
    for (const auto &sample : m_pressure)
        sum += sample.second;
    if (outward >= m_settings.pressureThreshold || sum >= m_settings.pressureThreshold) {
        m_triggered = true;
        m_edgeRevealed = true;
        m_revealUntil = now + 250;
        m_visible = true;
        m_hideAt = -1;
        return true;
    }
    return false;
}

bool Policy::update(const Environment &e, qint64 now)
{
    const bool leftRevealBand = m_edgeRevealed && m_environment.revealBand && !e.revealBand;
    if (leftRevealBand)
        m_edgeRevealed = false;
    const bool revealExpired = m_revealUntil >= 0 && now >= m_revealUntil;
    if (revealExpired)
        m_revealUntil = -1;
    if (m_atEdge && !m_settings.pressureToShow && !m_triggered && canReveal(e)
        && now - m_edgeSince >= m_settings.showDelay) {
        m_triggered = true;
        m_edgeRevealed = true;
        m_revealUntil = now + 250;
    }
    bool desired;
    if (e.locked) {
        desired = false;
    } else if (e.overview) {
        desired = true;
    } else if (m_settings.manualHide) {
        desired = false;
    } else if (m_settings.dockFixed) {
        desired = true;
    } else if ((m_settings.intelliHide || m_settings.autoHide)
               && (e.holdVisible || (m_settings.showDockUrgent && e.urgent))) {
        desired = true;
    } else if (m_settings.intelliHide && !e.overlap) {
        desired = true;
    } else if (m_settings.autoHide && (e.hover || (canReveal(e) && ((m_atEdge && m_triggered)
               || (m_edgeRevealed && e.revealBand) || now < m_revealUntil)))) {
        desired = true;
    } else {
        desired = false;
    }
    if (desired) {
        m_visible = true;
        m_hideAt = -1;
    } else if (e.locked || m_settings.manualHide) {
        m_visible = false;
        m_hideAt = -1;
    } else if (m_visible) {
        // Only pointer/temporary reveal departure has the hover hide delay;
        // newly overlapping windows immediately start the hide animation.
        if (m_hideAt < 0) {
            const bool departed = m_environment.hover || m_environment.holdVisible || revealExpired || leftRevealBand;
            m_hideAt = now + (departed ? m_settings.hideDelay : 0);
        }
        if (now >= m_hideAt)
            m_visible = false;
    }
    m_environment = e;
    return m_visible;
}

bool Policy::onEdge(const QPointF &p, const QRectF &output, const QRectF &work, int edge)
{
    // Some compositor work-area APIs describe the whole virtual desktop.
    // A registered dock must never respond at another output's aligned edge.
    const QRectF active = work.intersected(output);
    if (p.x() < output.left() || p.x() >= output.right() || p.y() < output.top() || p.y() >= output.bottom())
        return false;
    if (edge == 0 || edge == 2) {
        return p.x() > active.left() + 1 && p.x() < active.right() - 1
            && std::abs(p.y() - (edge == 0 ? output.top() : output.bottom() - 1)) <= 1;
    }
    return p.y() > active.top() + 1 && p.y() < active.bottom() - 1
        && std::abs(p.x() - (edge == 3 ? output.left() : output.right() - 1)) <= 1;
}

double Policy::outwardDelta(const QPointF &delta, int edge)
{
    switch (edge) {
    case 0: return -delta.y();
    case 1: return delta.x();
    case 2: return delta.y();
    default: return -delta.x();
    }
}
} // namespace GoshosDock
