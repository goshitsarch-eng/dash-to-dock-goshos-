// SPDX-License-Identifier: GPL-2.0-or-later
#include <LayerShellQt/Window>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QGuiApplication>
#include <QProcess>
#include <QQuickWindow>
#include <QScreen>
#include <QtTest>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <wayland-client-core.h>
#include <libei.h>
#include <unistd.h>

namespace {
constexpr auto service = "org.gosh.GoshosDock.KWin";
constexpr auto path = "/org/gosh/GoshosDock";
constexpr auto interface = "org.gosh.GoshosDock.KWin1";
uint surfaceId(QWindow *window)
{
    auto *native = window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
    return native && native->surface() ? wl_proxy_get_id(reinterpret_cast<wl_proxy *>(native->surface())) : 0;
}
}

class DockVisibilityTest : public QObject
{
    Q_OBJECT
private:
    std::unique_ptr<QQuickWindow> m_panel;
    std::unique_ptr<QDBusInterface> m_bus;
    QString m_token;
    QVariantMap m_settings;
    ei *m_ei = nullptr;
    ei_device *m_pointer = nullptr;
    ei_device *m_absolute = nullptr;
    int m_eiCookie = -1;
    void dispatchInput()
    {
        ei_dispatch(m_ei);
        while (auto *event = ei_get_event(m_ei)) {
            if (ei_event_get_type(event) == EI_EVENT_SEAT_ADDED)
                ei_seat_bind_capabilities(ei_event_get_seat(event), EI_DEVICE_CAP_POINTER, EI_DEVICE_CAP_POINTER_ABSOLUTE, nullptr);
            if (ei_event_get_type(event) == EI_EVENT_DEVICE_RESUMED) {
                auto *device = ei_event_get_device(event);
                if (ei_device_has_capability(device, EI_DEVICE_CAP_POINTER))
                    m_pointer = ei_device_ref(device);
                if (ei_device_has_capability(device, EI_DEVICE_CAP_POINTER_ABSOLUTE))
                    m_absolute = ei_device_ref(device);
                ei_device_start_emulating(device, 1);
            }
            ei_event_unref(event);
        }
    }
    void absoluteMotion(const QPointF &point)
    {
        ei_device_pointer_motion_absolute(m_absolute, point.x(), point.y());
        ei_device_frame(m_absolute, ei_now(m_ei));
        dispatchInput();
        QTest::qWait(30);
    }
    void relativeMotion(const QPointF &delta)
    {
        ei_device_pointer_motion(m_pointer, delta.x(), delta.y());
        ei_device_frame(m_pointer, ei_now(m_ei));
        dispatchInput();
        QTest::qWait(15);
    }
    QVariantMap query()
    {
        QDBusReply<QVariantMap> result = m_bus->call(QStringLiteral("QueryDock"), m_token);
        return result.value();
    }
    bool update()
    {
        QDBusReply<bool> result = m_bus->call(QStringLiteral("UpdateDock"), m_token, m_settings, false, false);
        return result.isValid() && result.value();
    }
private Q_SLOTS:
    void initTestCase()
    {
        if (!qEnvironmentVariableIsSet("GOSHOS_DISPOSABLE_KWIN_TEST"))
            QSKIP("Set GOSHOS_DISPOSABLE_KWIN_TEST=1 only in an isolated KWin test session");
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("wayland"));
        m_bus = std::make_unique<QDBusInterface>(QString::fromLatin1(service), QString::fromLatin1(path), QString::fromLatin1(interface));
        QVERIFY(m_bus->isValid());
        QDBusReply<bool> overview = m_bus->call(QStringLiteral("OverviewVisible"));
        QVERIFY(overview.isValid());
        if (overview.value()) {
            QDBusReply<bool> closed = m_bus->call(QStringLiteral("HideOverview"));
            QVERIFY(closed.isValid() && closed.value());
        }
        QTRY_VERIFY(!QDBusReply<bool>(m_bus->call(QStringLiteral("OverviewVisible"))).value());
        QDBusReply<bool> alreadyClosed = m_bus->call(QStringLiteral("HideOverview"));
        QVERIFY(alreadyClosed.isValid() && !alreadyClosed.value());
        QDBusReply<bool> available = m_bus->call(QStringLiteral("OverviewAvailable"));
        QVERIFY(available.isValid());
        if (!available.value()) {
            QDBusReply<bool> unsupported = m_bus->call(QStringLiteral("ToggleOverview"));
            QVERIFY(unsupported.isValid() && !unsupported.value());
            QVERIFY(!QDBusReply<bool>(m_bus->call(QStringLiteral("OverviewVisible"))).value());
        }
        m_panel = std::make_unique<QQuickWindow>();
        m_panel->setColor(Qt::darkCyan);
        m_panel->resize(500, 64);
        auto *layer = LayerShellQt::Window::get(m_panel.get());
        layer->setScope(QStringLiteral("dock"));
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors(LayerShellQt::Window::AnchorBottom);
        layer->setExclusiveZone(0);
        layer->setScreen(QGuiApplication::primaryScreen());
        m_panel->show();
        QTest::qWait(500);
        QVERIFY(surfaceId(m_panel.get()));
        m_settings = {{QStringLiteral("dockFixed"), true}, {QStringLiteral("animationTime"), 0.0}};
        QDBusReply<QString> reply = m_bus->call(QStringLiteral("RegisterDock"), surfaceId(m_panel.get()), uint(987654), m_settings);
        QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
        m_token = reply.value();
        QVERIFY(!m_token.isEmpty());
        QDBusReply<QVariantList> alternateShortcuts = m_bus->call(QStringLiteral("NormalizedAlternateShortcuts"));
        QVERIFY2(alternateShortcuts.isValid(), qPrintable(alternateShortcuts.error().message()));
        QCOMPARE(alternateShortcuts.value().size(), 10);
        QTRY_VERIFY(!query().value(QStringLiteral("hidden")).toBool());
    }
    void manualHideAndRestore()
    {
        m_settings[QStringLiteral("manualHide")] = true;
        QVERIFY(update());
        QTRY_VERIFY(query().value(QStringLiteral("hidden")).toBool());
        m_settings[QStringLiteral("manualHide")] = false;
        QVERIFY(update());
        QTRY_VERIFY(!query().value(QStringLiteral("hidden")).toBool());
    }
    void autoHideUsesActualCompositorVisibility()
    {
        m_settings[QStringLiteral("dockFixed")] = false;
        m_settings[QStringLiteral("intelliHide")] = false;
        QVERIFY(update());
        QTRY_VERIFY(query().value(QStringLiteral("hidden")).toBool());
        QDBusReply<bool> held = m_bus->call(QStringLiteral("UpdateDock"), m_token, m_settings, true, false);
        QVERIFY(held.value());
        QTRY_VERIFY(!query().value(QStringLiteral("hidden")).toBool());
        QVERIFY(update());
        QTRY_VERIFY(query().value(QStringLiteral("hidden")).toBool());
    }
    void animatedVisibility()
    {
        m_settings[QStringLiteral("dockFixed")] = true;
        m_settings[QStringLiteral("animationTime")] = .3;
        QVERIFY(update());
        const auto during = query();
        QVERIFY(!during.value(QStringLiteral("hidden")).toBool());
        QVERIFY(during.value(QStringLiteral("opacity")).toDouble() < 1);
        QTRY_COMPARE(query().value(QStringLiteral("opacity")).toDouble(), 1.0);
        m_settings[QStringLiteral("dockFixed")] = false;
        QVERIFY(update());
        QTRY_VERIFY(query().value(QStringLiteral("hidden")).toBool());
        QCOMPARE(query().value(QStringLiteral("opacity")).toDouble(), 1.0);
    }
    void realFullscreenWindowOverlap()
    {
        m_settings[QStringLiteral("intelliHide")] = true;
        m_settings[QStringLiteral("intelliHideMode")] = 2;
        m_settings[QStringLiteral("animationTime")] = 0.;
        QVERIFY(update());
        QQuickWindow client;
        client.setColor(Qt::darkBlue);
        client.setScreen(QGuiApplication::primaryScreen());
        client.showFullScreen();
        client.requestActivate();
        QTRY_VERIFY(query().value(QStringLiteral("fullscreen")).toBool());
        QTRY_VERIFY(query().value(QStringLiteral("overlap")).toBool());
        QTRY_VERIFY(query().value(QStringLiteral("hidden")).toBool());
        client.hide();
        QTRY_VERIFY(!query().value(QStringLiteral("fullscreen")).toBool());
        QTRY_VERIFY(!query().value(QStringLiteral("hidden")).toBool());
    }
    void realPointerPressureAllEdgesAndDwell()
    {
        QDBusInterface eis(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/EIS/RemoteDesktop"),
                           QStringLiteral("org.kde.KWin.EIS.RemoteDesktop"));
        QVERIFY(eis.isValid());
        const QDBusMessage reply = eis.call(QStringLiteral("connectToEIS"), 2);
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        const auto fd = qvariant_cast<QDBusUnixFileDescriptor>(reply.arguments().value(0));
        QVERIFY(fd.isValid());
        m_eiCookie = reply.arguments().value(1).toInt();
        m_ei = ei_new_sender(nullptr);
        ei_configure_name(m_ei, "GoshosDock isolated compositor test");
        QCOMPARE(ei_setup_backend_fd(m_ei, dup(fd.fileDescriptor())), 0);
        QTRY_VERIFY((dispatchInput(), m_pointer && m_absolute));
        const QRect geometry = QGuiApplication::primaryScreen()->geometry();
        const QPointF center = geometry.center();
        bool rightNeighbour = false;
        for (QScreen *screen : QGuiApplication::screens())
            rightNeighbour |= screen->geometry().contains(QPoint(geometry.right() + 1, geometry.center().y()));
        const QPointF points[] = {{center.x(), qreal(geometry.top())}, {qreal(geometry.right()), center.y()},
                                  {center.x(), qreal(geometry.bottom())}, {qreal(geometry.left()), center.y()}};
        const QPointF deltas[] = {{0, -15}, {15, 0}, {0, 15}, {-15, 0}};
        const LayerShellQt::Window::Anchor anchors[] = {LayerShellQt::Window::AnchorTop, LayerShellQt::Window::AnchorRight,
                                                        LayerShellQt::Window::AnchorBottom, LayerShellQt::Window::AnchorLeft};
        m_settings[QStringLiteral("intelliHide")] = false;
        m_settings[QStringLiteral("dockFixed")] = false;
        m_settings[QStringLiteral("animationTime")] = 0.;
        m_settings[QStringLiteral("pressureToShow")] = true;
        m_settings[QStringLiteral("pressureThreshold")] = 100.;
        m_settings[QStringLiteral("showDelay")] = .5;
        m_settings[QStringLiteral("hideDelay")] = .05;
        absoluteMotion(center);
        QDBusReply<QVariantMap> pointerOutput = m_bus->call(QStringLiteral("PointerOutputGeometry"));
        QVERIFY(pointerOutput.isValid());
        QCOMPARE(pointerOutput.value().value(QStringLiteral("x")).toInt(), geometry.x());
        QCOMPARE(pointerOutput.value().value(QStringLiteral("y")).toInt(), geometry.y());
        QCOMPARE(pointerOutput.value().value(QStringLiteral("width")).toInt(), geometry.width());
        QCOMPARE(pointerOutput.value().value(QStringLiteral("height")).toInt(), geometry.height());
        for (int edge = 0; edge < 4; ++edge) {
            absoluteMotion(center);
            auto *layer = LayerShellQt::Window::get(m_panel.get());
            layer->setAnchors(anchors[edge]);
            m_panel->resize(edge % 2 ? QSize(64, 500) : QSize(500, 64));
            m_settings[QStringLiteral("dockPosition")] = edge;
            QVERIFY(update());
            QTRY_VERIFY(query().value(QStringLiteral("hidden")).toBool());
            absoluteMotion(points[edge]);
            QVERIFY2(query().value(QStringLiteral("hidden")).toBool(), "Absolute movement must not simulate pressure");
            relativeMotion(deltas[edge]);
            QVERIFY2(query().value(QStringLiteral("hidden")).toBool(), "Below-threshold motion must remain hidden");
            if (edge == 1 && rightNeighbour)
                QVERIFY2(query().value(QStringLiteral("pointerX")).toDouble() <= geometry.right(), "Internal edge pressure barrier must hold pointer on its own output");
            for (int sample = 0; sample < 7; ++sample)
                relativeMotion(deltas[edge]);
            QTRY_VERIFY2(!query().value(QStringLiteral("hidden")).toBool(), qPrintable(QStringLiteral("Pressure did not reveal edge %1").arg(edge)));
            if (edge == 1 && rightNeighbour) {
                relativeMotion({200, 0});
                QTRY_VERIFY2(query().value(QStringLiteral("pointerX")).toDouble() > geometry.right(), "Revealed dock must release internal monitor crossing");
            }
        }
        absoluteMotion(center);
        LayerShellQt::Window::get(m_panel.get())->setAnchors(LayerShellQt::Window::AnchorBottom);
        m_panel->resize(500, 64);
        m_settings[QStringLiteral("dockPosition")] = 2;
        m_settings[QStringLiteral("pressureToShow")] = false;
        m_settings[QStringLiteral("showDelay")] = .2;
        QVERIFY(update());
        QTRY_VERIFY(query().value(QStringLiteral("hidden")).toBool());
        if (rightNeighbour) {
            // An aligned edge on a different output must never activate us.
            m_settings[QStringLiteral("pressureToShow")] = true;
            QVERIFY(update());
            absoluteMotion({geometry.right() + 100., qreal(geometry.bottom())});
            pointerOutput = m_bus->call(QStringLiteral("PointerOutputGeometry"));
            QVERIFY(pointerOutput.isValid());
            QVERIFY(pointerOutput.value().value(QStringLiteral("x")).toInt() > geometry.right());
            relativeMotion({0, 200});
            QVERIFY(query().value(QStringLiteral("hidden")).toBool());
            absoluteMotion(center);
            m_settings[QStringLiteral("pressureToShow")] = false;
            QVERIFY(update());
        }
        absoluteMotion(points[2]);
        QVERIFY(query().value(QStringLiteral("hidden")).toBool());
        QTRY_VERIFY(!query().value(QStringLiteral("hidden")).toBool());
        absoluteMotion(center);
        m_settings[QStringLiteral("pressureToShow")] = true;
        m_settings[QStringLiteral("hideInFullscreen")] = true;
        QVERIFY(update());
        QQuickWindow fullscreen;
        fullscreen.setColor(Qt::darkMagenta);
        fullscreen.setScreen(QGuiApplication::primaryScreen());
        fullscreen.showFullScreen();
        fullscreen.requestActivate();
        QTRY_VERIFY(query().value(QStringLiteral("fullscreen")).toBool());
        QTRY_VERIFY(query().value(QStringLiteral("hidden")).toBool());
        absoluteMotion(points[2]);
        relativeMotion({0, 200});
        QVERIFY2(query().value(QStringLiteral("hidden")).toBool(), "Fullscreen suppression must reject edge pressure");
        m_settings[QStringLiteral("hideInFullscreen")] = false;
        QVERIFY(update());
        absoluteMotion(center);
        absoluteMotion(points[2]);
        relativeMotion({0, 200});
        QTRY_VERIFY2(!query().value(QStringLiteral("hidden")).toBool(), "Fullscreen opt-in must permit edge pressure");
        fullscreen.hide();
        absoluteMotion(center);
        m_settings[QStringLiteral("hideInFullscreen")] = true;
        QVERIFY(update());
    }
    void nativeOverviewRevealsManualHiddenDock()
    {
        QDBusInterface effects(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QStringLiteral("org.kde.kwin.Effects"));
        const QDBusReply<bool> supported = effects.call(QStringLiteral("isEffectSupported"), QStringLiteral("overview"));
        QVERIFY2(supported.isValid() && supported.value(), "Native Overview needs a supported OpenGL compositor; run other cases explicitly in QPainter sessions");
        QSignalSpy changes(m_bus.get(), SIGNAL(OverviewChanged(bool)));
        QVERIFY(changes.isValid());
        m_settings[QStringLiteral("manualHide")] = true;
        QVERIFY(update());
        QTRY_VERIFY(query().value(QStringLiteral("hidden")).toBool());
        QDBusReply<bool> opened = m_bus->call(QStringLiteral("ToggleOverview"));
        QVERIFY(opened.isValid() && opened.value());
        QTRY_VERIFY(query().value(QStringLiteral("overview")).toBool());
        QTRY_VERIFY(!query().value(QStringLiteral("hidden")).toBool());
        QTRY_VERIFY(changes.count() >= 1);
        QCOMPARE(changes.first().first().toBool(), true);
        QDBusReply<bool> closed = m_bus->call(QStringLiteral("HideOverview"));
        QVERIFY(closed.isValid() && closed.value());
        QTRY_VERIFY(!query().value(QStringLiteral("overview")).toBool());
        QTRY_VERIFY(query().value(QStringLiteral("hidden")).toBool());
        QTRY_VERIFY(changes.count() >= 2);
        QCOMPARE(changes.last().first().toBool(), false);
        closed = m_bus->call(QStringLiteral("HideOverview"));
        QVERIFY(closed.isValid() && !closed.value());
        QTest::qWait(100);
        QVERIFY(!query().value(QStringLiteral("overview")).toBool());
        opened = m_bus->call(QStringLiteral("ToggleOverview"));
        QVERIFY(opened.isValid() && opened.value());
        QTRY_VERIFY(query().value(QStringLiteral("overview")).toBool());
        QDBusReply<bool> toggledClosed = m_bus->call(QStringLiteral("ToggleOverview"));
        QVERIFY(toggledClosed.isValid() && toggledClosed.value());
        QTRY_VERIFY(!query().value(QStringLiteral("overview")).toBool());
        m_settings[QStringLiteral("manualHide")] = false;
        QVERIFY(update());
    }
    void foreignProcessCannotClaimSurfaceOrToken()
    {
        QProcess other;
        other.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--foreign-probe"), QString::number(surfaceId(m_panel.get())), m_token});
        QVERIFY(other.waitForFinished(5000));
        QCOMPARE(other.exitStatus(), QProcess::NormalExit);
        QCOMPARE(other.exitCode(), 0);
    }
    void normalWindowCannotRegister()
    {
        QQuickWindow normal;
        normal.resize(200, 200);
        normal.show();
        QTest::qWait(100);
        QDBusReply<QString> reply = m_bus->call(QStringLiteral("RegisterDock"), surfaceId(&normal), uint(222), m_settings);
        QVERIFY(reply.isValid());
        QVERIFY(reply.value().isEmpty());
    }
    void cleanupTestCase()
    {
        if (m_pointer)
            ei_device_unref(m_pointer);
        if (m_absolute)
            ei_device_unref(m_absolute);
        if (m_ei)
            ei_unref(m_ei);
        if (m_eiCookie >= 0) {
            QDBusInterface eis(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/EIS/RemoteDesktop"),
                               QStringLiteral("org.kde.KWin.EIS.RemoteDesktop"));
            eis.call(QStringLiteral("disconnect"), m_eiCookie);
        }
        if (!m_bus || m_token.isEmpty())
            return;
        QDBusReply<bool> result = m_bus->call(QStringLiteral("UnregisterDock"), m_token);
        QVERIFY(result.value());
        QVERIFY(query().isEmpty());
        m_panel.reset();
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (app.arguments().value(1) == QStringLiteral("--foreign-probe")) {
        QDBusInterface bus(QString::fromLatin1(service), QString::fromLatin1(path), QString::fromLatin1(interface));
        QDBusReply<QString> registered = bus.call(QStringLiteral("RegisterDock"), app.arguments().value(2).toUInt(), uint(111), QVariantMap());
        QDBusReply<QVariantMap> query = bus.call(QStringLiteral("QueryDock"), app.arguments().value(3));
        QDBusReply<bool> changed = bus.call(QStringLiteral("UpdateDock"), app.arguments().value(3), QVariantMap(), true, true);
        QDBusReply<bool> removed = bus.call(QStringLiteral("UnregisterDock"), app.arguments().value(3));
        QDBusReply<QVariantMap> pointerOutput = bus.call(QStringLiteral("PointerOutputGeometry"));
        QDBusReply<QVariantList> alternateShortcuts = bus.call(QStringLiteral("NormalizedAlternateShortcuts"));
        return registered.isValid() && registered.value().isEmpty() && query.isValid() && query.value().isEmpty()
            && changed.isValid() && !changed.value() && removed.isValid() && !removed.value()
            && pointerOutput.isValid() && pointerOutput.value().isEmpty()
            && alternateShortcuts.isValid() && alternateShortcuts.value().isEmpty() ? 0 : 1;
    }
    DockVisibilityTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_dockvisibility.moc"
