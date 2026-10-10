/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "panellayout.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>

PanelLayout::PanelLayout(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, &PanelLayout::apply);
    auto *watcher = new QDBusServiceWatcher(QStringLiteral("org.kde.plasmashell"), QDBusConnection::sessionBus(),
                                           QDBusServiceWatcher::WatchForRegistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &PanelLayout::schedule);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
        watchScreen(screen);
        schedule();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &PanelLayout::schedule);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &PanelLayout::schedule);
    for (QScreen *screen : QGuiApplication::screens()) {
        watchScreen(screen);
    }
}

void PanelLayout::watchScreen(QScreen *screen)
{
    connect(screen, &QScreen::geometryChanged, this, &PanelLayout::schedule);
}

void PanelLayout::setPanelId(uint id)
{
    if (m_panelId != id) {
        m_panelId = id;
        Q_EMIT identityChanged();
        schedule();
    }
}

void PanelLayout::setAppletId(uint id)
{
    if (m_appletId != id) {
        m_appletId = id;
        Q_EMIT identityChanged();
        schedule();
    }
}

void PanelLayout::setConfiguration(const QVariantMap &configuration)
{
    if (m_configuration != configuration) {
        m_configuration = configuration;
        Q_EMIT configurationChanged();
        schedule();
    }
}

void PanelLayout::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        Q_EMIT enabledChanged();
        schedule();
    }
}

void PanelLayout::setStatus(bool active, const QString &error)
{
    if (m_active != active || m_error != error) {
        m_active = active;
        m_error = error;
        Q_EMIT statusChanged();
    }
}

void PanelLayout::schedule()
{
    m_pending = true;
    if (!m_inFlight) {
        m_debounce.start();
    }
}

void PanelLayout::apply()
{
    m_pending = false;
    if (!m_enabled || m_panelId == 0 || m_appletId == 0 || m_configuration.isEmpty()) {
        setStatus(false, {});
        return;
    }
    QVariantList outputs;
    for (const QScreen *screen : QGuiApplication::screens()) {
        outputs.append(QVariantMap{{QStringLiteral("name"), screen->name()},
                                   {QStringLiteral("width"), screen->geometry().width()},
                                   {QStringLiteral("height"), screen->geometry().height()}});
    }
    const QScreen *primary = QGuiApplication::primaryScreen();
    const QVariantMap request{{QStringLiteral("panelId"), m_panelId}, {QStringLiteral("appletId"), m_appletId},
                              {QStringLiteral("configuration"), m_configuration}, {QStringLiteral("outputs"), outputs},
                              {QStringLiteral("primaryOutput"), primary ? primary->name() : QString()}};
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"),
                                                        QStringLiteral("org.kde.PlasmaShell"), QStringLiteral("evaluateScript"));
    message.setArguments({scriptFor(request)});
    m_inFlight = true;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            setStatus(false, reply.error().message());
        } else {
            const QJsonDocument response = QJsonDocument::fromJson(reply.value().toUtf8());
            const QJsonObject object = response.object();
            setStatus(object.value(QStringLiteral("ok")).toBool(), object.value(QStringLiteral("error")).toString());
            if (response.isNull()) {
                setStatus(false, tr("Plasma did not return a panel configuration result."));
            }
        }
        m_inFlight = false;
        watcher->deleteLater();
        if (m_pending) {
            m_debounce.start();
        }
    });
}

QString PanelLayout::scriptFor(const QVariantMap &request)
{
    // Values enter JavaScript exclusively as JSON, never interpolated source.
    const QString encoded = QString::fromUtf8(QJsonDocument::fromVariant(request).toJson(QJsonDocument::Compact));
    return QStringLiteral("(function(request) {\n") + QString::fromUtf8(R"JS(
    const plugin = "org.gosh.goshosdock";
    const configuration = request.configuration;
    function dock(panel) {
        if (!panel) return null;
        const widgets = panel.widgets();
        return widgets.length === 1 && widgets[0].type === plugin ? widgets[0] : null;
    }
    function marker(panel, name, fallback) {
        panel.currentConfigGroup = ["GoshosDockLayout"];
        return panel.readConfig(name, fallback);
    }
    function setMarker(panel, name, value) {
        panel.currentConfigGroup = ["GoshosDockLayout"];
        if (Array.from(panel.configKeys).indexOf(name) === -1 || panel.readConfig(name, value) !== value) panel.writeConfig(name, value);
    }
    function sync(widget) {
        widget.currentConfigGroup = ["General"];
        const existingKeys = Array.from(widget.configKeys);
        let changed = false;
        Object.keys(configuration).forEach(function(key) {
            if (!/^[A-Za-z][A-Za-z0-9]*$/.test(key)) return;
            const value = configuration[key];
            if (existingKeys.indexOf(key) === -1 || JSON.stringify(widget.readConfig(key, value)) !== JSON.stringify(value)) {
                widget.writeConfig(key, value);
                changed = true;
            }
        });
        if (changed) widget.reloadConfig();
    }
    const source = panelById(request.panelId);
    const sourceWidget = dock(source);
    if (!sourceWidget || sourceWidget.id !== request.appletId) {
        print(JSON.stringify({ok:false,error:"Dock layout requires a panel containing only this dock."}));
        return;
    }
    let ownerId = Number(marker(source, "Owner", source.id));
    const groupId = String(marker(source, "Group", source.id + ":" + sourceWidget.id));
    let owner = panelById(ownerId);
    if (!dock(owner) || (owner.id !== source.id && String(marker(owner, "Group", "")) !== groupId)) {
        owner = source;
        ownerId = source.id;
    }
    const group = panels().filter(function(panel) {
        return dock(panel) && (panel.id === source.id || panel.id === ownerId || String(marker(panel, "Group", "")) === groupId);
    });
    const outputs = request.outputs.map(function(output) {
        return {name:output.name,width:output.width,height:output.height,screen:screenForConnector(output.name)};
    }).filter(function(output) { return output.screen >= 0; });
    if (!outputs.length) {
        print(JSON.stringify({ok:false,error:"No connected Plasma screen is available."}));
        return;
    }
    group.forEach(function(panel) {
        setMarker(panel, "Owner", ownerId);
        setMarker(panel, "Group", groupId);
    });
    setMarker(owner, "CreatedByDock", false);
    const requested = configuration.preferredOutput === "primary" ? request.primaryOutput : configuration.preferredOutput;
    const primary = outputs.find(function(output) { return output.name === request.primaryOutput; }) || outputs[0];
    const selected = outputs.find(function(output) { return output.name === requested; }) || primary;
    const targets = configuration.allOutputs ? [selected].concat(outputs.filter(function(output) { return output.name !== selected.name; })) : [selected];
    const used = [];
    targets.forEach(function(output, index) {
        let panel = index === 0 ? owner : group.find(function(candidate) {
            return candidate.id !== ownerId && used.indexOf(candidate.id) === -1 && candidate.screen === output.screen;
        });
        if (!panel) {
            panel = new Panel();
            panel.addWidget(plugin);
            setMarker(panel, "Owner", ownerId);
            setMarker(panel, "Group", groupId);
            setMarker(panel, "CreatedByDock", true);
            panel.floating = true;
            group.push(panel);
        }
        const widget = dock(panel);
        if (!widget) return;
        used.push(panel.id);
        panel.screen = output.screen;
        const edge = Math.max(0, Math.min(3, Number(configuration.dockPosition) || 0));
        panel.location = ["top", "right", "bottom", "left"][edge];
        panel.alignment = "center";
        panel.offset = 0;
        panel.lengthMode = "custom";
        const fraction = Math.max(0.1, Math.min(1, Number(configuration.intendedLengthFraction) || 0.9));
        const length = Math.round((edge === 1 || edge === 3 ? output.height : output.width) * fraction);
        const thickness = Math.max(28, Math.min(140, (Number(configuration.iconSize) || 48) + 12));
        panel.height = thickness;
        // In Plasma Custom mode content length is bounded by these two values.
        // Fit mode ignores maximumLength and cannot enforce the user's fraction.
        panel.minimumLength = configuration.extendDock ? length : Math.min(thickness, length);
        panel.maximumLength = length;
        sync(widget);
    });
    group.forEach(function(panel) {
        // Only remove copies this helper created, still dedicated to this dock.
        if (used.indexOf(panel.id) === -1 && panel.id !== ownerId && dock(panel) && marker(panel, "CreatedByDock", false) === true) panel.remove();
    });
    print(JSON.stringify({ok:true,error:"",owner:ownerId,panels:used}));
)JS") + QStringLiteral("\n})(") + encoded + QStringLiteral(");");
}

#include "moc_panellayout.cpp"
