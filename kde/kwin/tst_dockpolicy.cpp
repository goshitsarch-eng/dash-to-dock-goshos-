// SPDX-License-Identifier: GPL-2.0-or-later
#include "dockpolicy.h"
#include <QtTest>

using namespace GoshosDock;
class DockPolicyTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void sourceDefaults()
    {
        const auto s = Settings::fromMap({});
        QCOMPARE(s.showDelay, 250);
        QCOMPARE(s.hideDelay, 200);
        QCOMPARE(s.animationTime, 200);
        QCOMPARE(s.pressureThreshold, 100);
        QVERIFY(s.intelliHide && s.autoHide && s.pressureToShow && s.hideInFullscreen);
    }
    void manualOverridesFixedAndUrgent()
    {
        Settings s;
        s.manualHide = s.dockFixed = true;
        Policy p;
        p.configure(s);
        Environment e;
        e.holdVisible = e.urgent = e.hover = true;
        QVERIFY(!p.update(e, 0));
        QVERIFY(!p.canReveal(e));
    }
    void fixedResistsOverlap()
    {
        Settings s;
        s.dockFixed = true;
        Policy p;
        p.configure(s);
        Environment e;
        e.overlap = e.fullscreen = true;
        QVERIFY(p.update(e, 0));
        e.locked = true;
        QVERIFY(!p.update(e, 1));
    }
    void overviewRevealsManualHiddenButNeverOverLock()
    {
        Settings s;
        s.manualHide = true;
        Policy p;
        p.configure(s);
        Environment e;
        QVERIFY(!p.update(e, 0));
        e.overview = true;
        QVERIFY(p.update(e, 1));
        e.locked = true;
        QVERIFY(!p.update(e, 2));
    }
    void intelligentVisibility()
    {
        Policy p;
        Environment e;
        QVERIFY(p.update(e, 0));
        e.overlap = true;
        QVERIFY(!p.update(e, 1));
        e.overlap = false;
        QVERIFY(p.update(e, 2));
    }
    void hoverDepartureDelayAndCancellation()
    {
        Settings s;
        s.intelliHide = false;
        Policy p;
        p.configure(s);
        Environment e;
        e.hover = true;
        QVERIFY(p.update(e, 0));
        e.hover = false;
        QVERIFY(p.update(e, 10));
        QVERIFY(p.update(e, 209));
        e.hover = true;
        QVERIFY(p.update(e, 210));
        e.hover = false;
        QVERIFY(p.update(e, 220));
        QVERIFY(!p.update(e, 420));
    }
    void dwellDelay()
    {
        Settings s;
        s.intelliHide = false;
        s.pressureToShow = false;
        Policy p;
        p.configure(s);
        Environment e;
        QVERIFY(!p.update(e, 0));
        QVERIFY(!p.motion(true, 0, 0, false, false, e, 100));
        QVERIFY(!p.update(e, 349));
        QVERIFY(p.update(e, 350));
    }
    void cancelledDwellDoesNotReveal()
    {
        Settings s;
        s.intelliHide = false;
        s.pressureToShow = false;
        Policy p;
        p.configure(s);
        Environment e;
        p.update(e, 0);
        p.motion(true, 0, 0, false, false, e, 100);
        p.cancelEdge();
        QVERIFY(!p.update(e, 400));
    }
    void pressureUsesRawOutwardAndRollingWindow()
    {
        Settings s;
        s.intelliHide = false;
        Policy p;
        p.configure(s);
        Environment e;
        p.update(e, 0);
        for (int i = 0; i < 20; ++i) {
            QVERIFY(!p.motion(true, 20, 0, false, false, e, i)); // absolute
            QVERIFY(!p.motion(true, 20, 21, true, false, e, i)); // parallel
        }
        QVERIFY(!p.motion(true, 90, 0, true, false, e, 20)); // capped at15
        QVERIFY(!p.motion(true, 90, 0, true, false, e, 300)); // old sample expired
        for (int i = 0; i < 5; ++i)
            QVERIFY(!p.motion(true, 15, 0, true, false, e, 310 + i));
        QVERIFY(p.motion(true, 15, 0, true, false, e, 320));
        QVERIFY(p.update(e, 320));
    }
    void fullscreenAndButtonsSuppressReveal()
    {
        Settings s;
        s.intelliHide = false;
        Policy p;
        p.configure(s);
        Environment e;
        e.fullscreen = true;
        QVERIFY(!p.motion(true, 200, 0, true, false, e, 0));
        s.hideInFullscreen = false;
        p.configure(s);
        QVERIFY(!p.motion(true, 200, 0, true, true, e, 1));
        QVERIFY(p.motion(true, 200, 0, true, false, e, 2));
    }
    void pressureRevealRetainsFloatingEdgeBand()
    {
        Settings s;
        s.intelliHide = false;
        Policy p;
        p.configure(s);
        Environment e;
        e.revealBand = true;
        p.update(e, 0);
        QVERIFY(p.motion(true, 200, 0, true, false, e, 1));
        QVERIFY(p.update(e, 1));
        p.motion(false, 0, 0, false, false, e, 20);
        QVERIFY(p.update(e, 1000)); // Floating gap, outside the icon strip.
        e.revealBand = false;
        QVERIFY(p.update(e, 1001));
        QVERIFY(!p.update(e, 1201));
    }
    void urgentAndPopupHold()
    {
        Policy p;
        Environment e;
        e.overlap = true;
        e.urgent = true;
        QVERIFY(p.update(e, 0));
        e.urgent = false;
        QVERIFY(!p.update(e, 1));
        e.holdVisible = true;
        QVERIFY(p.update(e, 2));
        Settings s;
        s.showDockUrgent = false;
        p.configure(s);
        e.holdVisible = false;
        e.urgent = true;
        p.update(e, 3);
        QVERIFY(!p.update(e, 203));
    }
    void neitherModeHides()
    {
        Settings s;
        s.intelliHide = s.autoHide = false;
        Policy p;
        p.configure(s);
        QVERIFY(!p.update({}, 0));
        Environment e;
        e.urgent = e.holdVisible = true;
        QVERIFY(!p.update(e, 1));
    }
    void allEdgesMixedOriginsAndCorners()
    {
        const QRectF output(-1920, -200, 1920, 1080);
        QVERIFY(Policy::onEdge({-1000, -200}, output, output, 0));
        QVERIFY(Policy::onEdge({-1, 200}, output, output, 1));
        QVERIFY(Policy::onEdge({-1000, 879}, output, output, 2));
        QVERIFY(Policy::onEdge({-1920, 200}, output, output, 3));
        QVERIFY(!Policy::onEdge(output.topLeft(), output, output, 0));
        QVERIFY(!Policy::onEdge({-1900, -198}, output, output, 0));
        QVERIFY(!Policy::onEdge({2500, 1079}, {0, 0, 1920, 1080}, {0, 0, 3840, 1080}, 2));
        QVERIFY(!Policy::onEdge({1920, 500}, {0, 0, 1920, 1080}, {0, 0, 3840, 1080}, 1));
        QVERIFY(Policy::onEdge({1000, 1079}, {0, 0, 1920, 1080}, {0, 0, 3840, 1080}, 2));
        QCOMPARE(Policy::outwardDelta({-3, -7}, 0), 7);
        QCOMPARE(Policy::outwardDelta({3, 7}, 1), 3);
        QCOMPARE(Policy::outwardDelta({3, 7}, 2), 7);
        QCOMPARE(Policy::outwardDelta({-3, -7}, 3), 3);
    }
    void invalidConfigurationIsBounded()
    {
        const auto s = Settings::fromMap({{QStringLiteral("pressureThreshold"), -2},
                                        {QStringLiteral("hideDelay"), 1e30},
                                        {QStringLiteral("showDelay"), std::numeric_limits<double>::quiet_NaN()}});
        QCOMPARE(s.pressureThreshold, 1);
        QCOMPARE(s.hideDelay, 60000);
        QCOMPARE(s.showDelay, 250);
    }
};
QTEST_GUILESS_MAIN(DockPolicyTest)
#include "tst_dockpolicy.moc"
