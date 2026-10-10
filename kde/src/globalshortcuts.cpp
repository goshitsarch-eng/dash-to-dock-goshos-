/*
    SPDX-FileCopyrightText: 2026 Gosho's Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "globalshortcuts.h"

#include <KGlobalAccel>
#include <KLocalizedString>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QAction>
#include <QCoreApplication>
#include <QCursor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>
#include <QKeySequence>
#include <QPointer>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <array>
#include <utility>

namespace
{
class ShortcutRegistry : public QObject
{
    Q_OBJECT
public:
    explicit ShortcutRegistry(QObject *parent)
        : QObject(parent)
    {
        setObjectName(QStringLiteral("goshosdock-shared-shortcuts"));
        // KGlobalAccel emits this immediately before QAction::trigger(), and
        // again on key release. Route Plasma's existing task shortcut without
        // altering its saved/default keys or relying on ShellCorona panel order.
        connect(KGlobalAccel::self(), &KGlobalAccel::globalShortcutActiveChanged,
                this, &ShortcutRegistry::nativeShortcutActiveChanged);
        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/component/plasmashell"),
            QStringLiteral("org.kde.kglobalaccel.Component"), QStringLiteral("globalShortcutPressed"),
            this, SLOT(nativeShortcutPressed(QString,QString,qlonglong)));
        QDBusConnection::sessionBus().connect(QStringLiteral("org.gosh.GoshosDock.KWin"), QStringLiteral("/org/gosh/GoshosDock"),
            QStringLiteral("org.gosh.GoshosDock.KWin1"), QStringLiteral("AlternateShortcutsChanged"),
            this, SLOT(refreshAlternateShortcuts()));
        m_alternateRetry.setInterval(1000);
        connect(&m_alternateRetry, &QTimer::timeout, this, &ShortcutRegistry::refreshAlternateShortcuts);
        connect(KGlobalAccel::self(), &KGlobalAccel::globalShortcutChanged, this, [this](QAction *action, const QKeySequence &key) {
            if (!m_registering && m_actions.contains(action)) {
                const int index = ownTaskIndex(action->objectName());
                if (index >= 0) {
                    // A user's explicit empty/custom mapping disables the
                    // default bridge, including when the empty value is also
                    // what the original native-key conflict produced.
                    setNativeActivationEnabled(index, key == defaultActivationKey(index));
                }
            }
            QTimer::singleShot(0, this, &ShortcutRegistry::updateErrors);
        });
    }

    ~ShortcutRegistry() override
    {
        restoreNativeActions();
    }

    void addProxy(GlobalShortcuts *proxy)
    {
        m_proxies.append(proxy);
        updateActions();
    }

    bool removeProxy(GlobalShortcuts *proxy)
    {
        m_proxies.removeAll(proxy);
        updateActions();
        return m_proxies.isEmpty();
    }

    bool nativeActivationEnabled(int index) const
    {
        return index >= 0 && index < 10 && m_nativeBridgeAllowed[index];
    }

    void updateActions()
    {
        ++m_generation;
        const bool enabled = std::any_of(m_proxies.cbegin(), m_proxies.cend(), [](const auto &proxy) {
            return proxy && proxy->isEnabled();
        });
        if (!enabled) {
            m_alternateRetry.stop();
            restoreNativeActions();
            // KGlobalAccel marks destroyed actions inactive and retains the user's
            // bindings. removeAllShortcuts() would erase those saved preferences.
            qDeleteAll(m_actions);
            m_actions.clear();
            updateErrors();
            return;
        }
        if (!m_actions.isEmpty()) {
            updateErrors();
            return;
        }

        m_registering = true;
        for (int index = 0; index < 10; ++index) {
            const Qt::Key key = index == 9 ? Qt::Key_0 : static_cast<Qt::Key>(Qt::Key_1 + index);
            loadNativeActivationPreference(index);
            // Existing Plasma assignments use the scoped native bridge below.
            // Free keys (including Meta+0) and user overrides bind directly.
            auto *activate = addAction(QStringLiteral("goshosdock-activate-%1").arg(index + 1),
                                       i18n("Activate dock application %1", index + 1),
                                       QKeySequence(QKeyCombination(Qt::MetaModifier, key)));
            connect(activate, &QAction::triggered, this, [this, index] {
                dispatch(index, false, false);
            });

            auto *launch = addAction(QStringLiteral("goshosdock-launch-%1").arg(index + 1),
                                     i18n("Launch dock application %1", index + 1),
                                     QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::MetaModifier, key)));
            connect(launch, &QAction::triggered, this, [this, index] {
                dispatch(index, true, false);
            });

            auto *shift = addAction(QStringLiteral("goshosdock-alternate-%1").arg(index + 1),
                                    i18n("Alternate action for dock application %1", index + 1),
                                    QKeySequence(QKeyCombination(Qt::ShiftModifier | Qt::MetaModifier, key)));
            connect(shift, &QAction::triggered, this, [this, index] {
                dispatch(index, false, true);
            });
        }

        auto *overlay = addAction(QStringLiteral("goshosdock-show-overlay"),
                                  i18n("Show dock and application numbers"),
                                  QKeySequence(QKeyCombination(Qt::MetaModifier, Qt::Key_Q)));
        connect(overlay, &QAction::triggered, this, [this] {
            dispatch(-1, false, false);
        });
        m_registering = false;
        updateErrors();
        refreshAlternateShortcuts();
    }

private:
    static QKeySequence defaultActivationKey(int index)
    {
        const Qt::Key key = index == 9 ? Qt::Key_0 : static_cast<Qt::Key>(Qt::Key_1 + index);
        return QKeySequence(QKeyCombination(Qt::MetaModifier, key));
    }

    static int ownTaskIndex(const QString &name)
    {
        const QString prefix = QStringLiteral("goshosdock-activate-");
        if (!name.startsWith(prefix)) return -1;
        bool valid = false;
        const int number = name.sliced(prefix.size()).toInt(&valid);
        return valid && number >= 1 && number <= 10 ? number - 1 : -1;
    }

    void setNativeActivationEnabled(int index, bool enabled, bool loading = false)
    {
        ++m_generation;
        m_nativeBridgeAllowed[index] = enabled;
        if (!enabled) {
            for (auto held = m_heldNative.begin(); held != m_heldNative.end();) {
                if (held->action && nativeTaskIndex(QStringLiteral("plasmashell"), held->action->objectName()) == index) {
                    held->action->blockSignals(held->previouslyBlocked);
                    held = m_heldNative.erase(held);
                } else ++held;
            }
        }
        KConfigGroup state(KSharedConfig::openConfig(QStringLiteral("goshosdock-shortcutsrc")), QStringLiteral("NativeDefaults"));
        const auto owners = KGlobalAccel::globalShortcutsByKey(defaultActivationKey(index));
        const bool nativeConflict = std::any_of(owners.cbegin(), owners.cend(), [](const auto &owner) {
            return nativeTaskIndex(owner.componentUniqueName(), owner.uniqueName()) >= 0;
        });
        // During Plasma startup its native actions may not have registered
        // yet. A previously recorded collision remains the reason our saved
        // key is empty; losing that evidence here disables it next restart.
        // Explicit user changes still clear it, including an explicit "none".
        const bool knownConflict = loading && state.readEntry(QString::number(index + 1), false);
        state.writeEntry(QString::number(index + 1), enabled && (nativeConflict || knownConflict));
        state.sync();
    }

    void loadNativeActivationPreference(int index)
    {
        const auto config = KSharedConfig::openConfig(QStringLiteral("kglobalshortcutsrc"), KConfig::NoGlobals);
        config->reparseConfiguration();
        const KConfigGroup shortcuts(config, QStringLiteral("org.gosh.goshosdock"));
        const QString action = QStringLiteral("goshosdock-activate-%1").arg(index + 1);
        const QStringList entry = shortcuts.readEntry(action, QStringList());
        if (!shortcuts.hasKey(action)) {
            setNativeActivationEnabled(index, true, true);
            return;
        }
        // KGlobalAccel's config syntax uses a literal "none" sentinel and
        // tab-separated portable sequences, not QKeySequence::listFromString.
        // Qt parses "none" as a non-empty unknown key, which otherwise loses
        // the saved native-collision marker on every shell restart.
        QList<QKeySequence> keys;
        if (!entry.isEmpty() && entry.first() != QLatin1String("none")) {
            for (const auto &key : entry.first().split(QLatin1Char('\t'), Qt::SkipEmptyParts))
                keys.append(QKeySequence::fromString(key, QKeySequence::PortableText));
        }
        const bool empty = std::all_of(keys.cbegin(), keys.cend(), [](const auto &key) { return key.isEmpty(); });
        const KConfigGroup state(KSharedConfig::openConfig(QStringLiteral("goshosdock-shortcutsrc")), QStringLiteral("NativeDefaults"));
        const bool previousConflict = empty && state.readEntry(QString::number(index + 1), false);
        setNativeActivationEnabled(index, keys.contains(defaultActivationKey(index)) || previousConflict, true);
    }

    static int nativeTaskIndex(const QString &component, const QString &name)
    {
        if (component != QLatin1String("plasmashell")) return -1;
        const QString prefix = QStringLiteral("activate task manager entry ");
        if (!name.startsWith(prefix)) return -1;
        bool valid = false;
        const int number = name.sliced(prefix.size()).toInt(&valid);
        return valid && number >= 1 && number <= 10 && name == prefix + QString::number(number) ? number - 1 : -1;
    }

    void restoreNativeActions()
    {
        for (const auto &held : std::as_const(m_heldNative)) {
            if (held.action) held.action->blockSignals(held.previouslyBlocked);
        }
        m_heldNative.clear();
    }

    void nativeShortcutActiveChanged(QAction *action, bool active)
    {
        if (!action) return;
        const auto held = std::find_if(m_heldNative.begin(), m_heldNative.end(), [action](const auto &entry) { return entry.action == action; });
        if (!active) {
            if (held != m_heldNative.end()) {
                action->blockSignals(held->previouslyBlocked);
                m_heldNative.erase(held);
            }
            return;
        }
        // Ignore repeat presses while another modifier/key is also held.
        if (held != m_heldNative.end()) return;
        const QString component = action->property("componentName").isValid()
            ? action->property("componentName").toString() : QCoreApplication::applicationName();
        const int index = nativeTaskIndex(component, action->objectName());
        const bool enabled = std::any_of(m_proxies.cbegin(), m_proxies.cend(), [](const auto &proxy) { return proxy && proxy->isEnabled(); });
        if (!enabled || !nativeActivationEnabled(index) || action->signalsBlocked()) return;
        m_heldNative.append({action, action->blockSignals(true)});
    }

private Q_SLOTS:
    void nativeShortcutPressed(const QString &component, const QString &name, qlonglong)
    {
        const int index = nativeTaskIndex(component, name);
        if (!nativeActivationEnabled(index)) return;
        dispatch(index, false, false, true);
    }

private:
    void dispatch(int index, bool launch, bool shifted, bool native = false)
    {
        // Wayland clients cannot observe the global pointer while another
        // client's surface is under it. Ask the compositor instead of using
        // Qt's last pointer position delivered to this applet process.
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.gosh.GoshosDock.KWin"),
            QStringLiteral("/org/gosh/GoshosDock"), QStringLiteral("org.gosh.GoshosDock.KWin1"),
            QStringLiteral("PointerOutputGeometry"));
        const quint64 generation = m_generation;
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 1000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation, index, launch, shifted, native] {
            const QDBusPendingReply<QVariantMap> reply = *watcher;
            watcher->deleteLater();
            if (generation != m_generation || (native && !nativeActivationEnabled(index))) return;
            QRect output;
            if (!reply.isError()) {
                const auto geometry = reply.value();
                output = QRect(geometry.value(QStringLiteral("x")).toInt(), geometry.value(QStringLiteral("y")).toInt(),
                    geometry.value(QStringLiteral("width")).toInt(), geometry.value(QStringLiteral("height")).toInt());
            }
            if (auto *proxy = target(output)) {
                const auto receivers = m_proxies;
                const bool broadcast = proxy->allOutputs();
                if (index >= 0) Q_EMIT proxy->taskRequested(index, launch, shifted);
                // Source shortcuts reveal numbers even beyond the app count.
                // Automatically replicated docks share that overlay/reveal.
                for (const auto &receiver : receivers) {
                    if (receiver && receiver->isEnabled()
                        && (receiver == proxy || (broadcast && receiver->allOutputs()))) {
                        Q_EMIT receiver->overlayRequested();
                    }
                }
            }
        });
    }


    void updateErrors()
    {
        QStringList conflicts;
        for (const auto *action : std::as_const(m_actions)) {
            const auto activeKeys = KGlobalAccel::self()->shortcut(action);
            if (std::any_of(activeKeys.cbegin(), activeKeys.cend(), [](const auto &key) { return !key.isEmpty(); })) continue;
            for (const auto &key : KGlobalAccel::self()->defaultShortcut(action)) {
                if (key.isEmpty()) continue;
                for (const auto &owner : KGlobalAccel::globalShortcutsByKey(key)) {
                    if (owner.componentUniqueName() == QLatin1String("org.gosh.goshosdock")) continue;
                    if (action->objectName().startsWith(QLatin1String("goshosdock-activate-"))
                        && nativeTaskIndex(owner.componentUniqueName(), owner.uniqueName()) >= 0) continue;
                    conflicts.append(i18n("%1 is already assigned to %2 (%3).", key.toString(QKeySequence::NativeText), owner.friendlyName(), owner.componentFriendlyName()));
                }
            }
        }
        conflicts.removeDuplicates();
        const QString error = conflicts.join(QLatin1Char('\n'));
        for (const auto &proxy : std::as_const(m_proxies)) {
            if (proxy) proxy->setError(proxy->isEnabled() ? error : QString());
        }
    }

    QAction *addAction(const QString &name, const QString &text, const QKeySequence &shortcut)
    {
        auto *action = new QAction(text, this);
        action->setObjectName(name);
        action->setProperty("componentName", QStringLiteral("org.gosh.goshosdock"));
        action->setProperty("componentDisplayName", i18n("Gosho's Dock"));
        action->setAutoRepeat(false);
        m_actions.append(action);
        // Default and active bindings are separate. Autoloading restores any
        // shortcut the user changed or disabled in System Settings.
        KGlobalAccel::self()->setDefaultShortcut(action, {shortcut}, KGlobalAccel::Autoloading);
        KGlobalAccel::self()->setShortcut(action, {shortcut}, KGlobalAccel::Autoloading);
        removeForeignConflicts(action);
        const int index = ownTaskIndex(name);
        const auto assigned = KGlobalAccel::self()->shortcut(action);
        if (nativeActivationEnabled(index)
            && std::none_of(assigned.cbegin(), assigned.cend(), [](const auto &key) { return !key.isEmpty(); })
            && KGlobalAccel::isGlobalShortcutAvailable(shortcut, QStringLiteral("org.gosh.goshosdock"))) {
            // A previously bridged default may become free after the native
            // task shortcut is remapped. Keep that default effective.
            KGlobalAccel::self()->setShortcut(action, {shortcut}, KGlobalAccel::NoAutoloading);
        }
        return action;
    }

    void removeForeignConflicts(QAction *action)
    {
        // KGlobalAccel can retain a duplicate saved/default key even when a
        // foreign component owns its delivery. Never steal that component's
        // key or silently present our action as working.
        const auto assigned = KGlobalAccel::self()->shortcut(action);
        QList<QKeySequence> available;
        for (const auto &key : assigned) {
            if (key.isEmpty()) continue;
            const auto owners = KGlobalAccel::globalShortcutsByKey(key);
            const bool foreign = std::any_of(owners.cbegin(), owners.cend(), [](const auto &owner) {
                return owner.componentUniqueName() != QLatin1String("org.gosh.goshosdock");
            });
            if (!foreign) available.append(key);
        }
        if (available != assigned && !assigned.isEmpty()) {
            KGlobalAccel::self()->setShortcut(action, available, KGlobalAccel::NoAutoloading);
        }
    }

private Q_SLOTS:
    void refreshAlternateShortcuts()
    {
        if (m_alternatePending || m_actions.isEmpty()) return;
        m_alternatePending = true;
        auto request = QDBusMessage::createMethodCall(QStringLiteral("org.gosh.GoshosDock.KWin"),
            QStringLiteral("/org/gosh/GoshosDock"), QStringLiteral("org.gosh.GoshosDock.KWin1"),
            QStringLiteral("NormalizedAlternateShortcuts"));
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request, 1000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
            const QDBusPendingReply<QVariantList> reply = *watcher;
            watcher->deleteLater();
            m_alternatePending = false;
            if (m_actions.isEmpty()) return;
            if (reply.isError() || reply.value().size() != 10) {
                // The applet's controller may not have registered its managed
                // surface when global actions are first constructed.
                m_alternateRetry.start();
                return;
            }
            m_alternateRetry.stop();
            KConfigGroup defaults(KSharedConfig::openConfig(QStringLiteral("goshosdock-shortcutsrc")), QStringLiteral("AlternateDefaults"));
            m_registering = true;
            for (int index = 0; index < 10; ++index) {
                const int combined = reply.value().at(index).toInt();
                if (!combined) continue;
                auto *action = findChild<QAction *>(QStringLiteral("goshosdock-alternate-%1").arg(index + 1));
                if (!action) continue;
                const QKeySequence normalized(combined);
                const Qt::Key digit = index == 9 ? Qt::Key_0 : static_cast<Qt::Key>(Qt::Key_1 + index);
                const QKeySequence legacy(QKeyCombination(Qt::MetaModifier | Qt::ShiftModifier, digit));
                const QKeySequence previous(defaults.readEntry(QString::number(index + 1), legacy.toString(QKeySequence::PortableText)));
                const auto assigned = KGlobalAccel::self()->shortcut(action);
                const bool usingDefault = assigned == QList<QKeySequence>{legacy} || assigned == QList<QKeySequence>{previous};
                KGlobalAccel::self()->setDefaultShortcut(action, {normalized}, KGlobalAccel::NoAutoloading);
                if (usingDefault) {
                    KGlobalAccel::self()->setShortcut(action, {normalized}, KGlobalAccel::NoAutoloading);
                    removeForeignConflicts(action);
                }
                defaults.writeEntry(QString::number(index + 1), normalized.toString(QKeySequence::PortableText));
            }
            defaults.sync();
            m_registering = false;
            updateErrors();
        });
    }

private:

    GlobalShortcuts *target(const QRect &pointerOutput = {}) const
    {
        QList<GlobalShortcuts *> enabled;
        QList<GlobalShortcuts *> replicas;
        for (const auto &proxy : m_proxies) {
            if (!proxy || !proxy->isEnabled()) continue;
            enabled.append(proxy);
            if (proxy->allOutputs()) replicas.append(proxy);
        }
        if (enabled.size() == 1) return enabled.first();
        // GNOME's multi-monitor mode activates the primary main dock. Keep
        // pointer routing for separately configured, independent KDE widgets.
        const auto *primary = QGuiApplication::primaryScreen();
        if (!replicas.isEmpty()) {
            if (primary) {
                for (auto *proxy : std::as_const(replicas)) {
                    if (proxy->screenGeometry().contains(primary->geometry().center())) return proxy;
                }
            }
            return replicas.first();
        }
        const QPoint pointer = pointerOutput.isValid() ? pointerOutput.center() : QCursor::pos();
        for (const auto &proxy : m_proxies) {
            if (proxy && proxy->isEnabled() && proxy->screenGeometry().contains(pointer)) {
                return proxy;
            }
        }

        // If the pointer is on a screen without this dock, use only the primary
        // screen's receiver. Do not activate a random secondary-screen instance.
        if (primary) {
            const QPoint primaryCenter = primary->geometry().center();
            for (const auto &proxy : m_proxies) {
                if (proxy && proxy->isEnabled() && proxy->screenGeometry().contains(primaryCenter)) {
                    return proxy;
                }
            }
        }
        return nullptr;
    }

    QList<QPointer<GlobalShortcuts>> m_proxies;
    QList<QAction *> m_actions;
    struct HeldNativeAction {
        QPointer<QAction> action;
        bool previouslyBlocked;
    };
    QList<HeldNativeAction> m_heldNative;
    std::array<bool, 10> m_nativeBridgeAllowed{};
    bool m_registering = false;
    quint64 m_generation = 0;
    QTimer m_alternateRetry;
    bool m_alternatePending = false;
};

QPointer<ShortcutRegistry> sharedRegistry;
}

GlobalShortcuts::GlobalShortcuts(QObject *parent)
    : QObject(parent)
{
    if (!sharedRegistry) {
        sharedRegistry = new ShortcutRegistry(QCoreApplication::instance());
    }
    sharedRegistry->addProxy(this);
}

GlobalShortcuts::~GlobalShortcuts()
{
    if (sharedRegistry && sharedRegistry->removeProxy(this)) {
        delete sharedRegistry.data();
    }
}

bool GlobalShortcuts::isEnabled() const
{
    return m_enabled;
}

void GlobalShortcuts::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (sharedRegistry) {
        sharedRegistry->updateActions();
    }
    Q_EMIT enabledChanged();
}

QRect GlobalShortcuts::screenGeometry() const
{
    return m_screenGeometry;
}

void GlobalShortcuts::setScreenGeometry(const QRect &geometry)
{
    if (m_screenGeometry == geometry) {
        return;
    }
    m_screenGeometry = geometry;
    Q_EMIT screenGeometryChanged();
}

void GlobalShortcuts::setError(const QString &error)
{
    if (m_error == error) return;
    m_error = error;
    Q_EMIT errorChanged();
}

bool GlobalShortcuts::nativeActivationEnabled(int index) const
{
    return m_enabled && sharedRegistry && sharedRegistry->nativeActivationEnabled(index);
}

void GlobalShortcuts::setAllOutputs(bool allOutputs)
{
    if (m_allOutputs == allOutputs) return;
    m_allOutputs = allOutputs;
    if (sharedRegistry) sharedRegistry->updateActions();
    Q_EMIT allOutputsChanged();
}

#include "globalshortcuts.moc"
