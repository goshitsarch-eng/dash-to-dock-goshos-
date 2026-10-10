// SPDX-License-Identifier: GPL-2.0-or-later
#include <QLibrary>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>

class QmlComponentsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        m_applet.setFileName(QString::fromUtf8(APPLET_LIBRARY_PATH));
        m_applet.setLoadHints(QLibrary::PreventUnloadHint);
        QVERIFY2(m_applet.load(), qPrintable(m_applet.errorString()));
    }

    void loadDynamicComponent_data()
    {
        QTest::addColumn<QString>("filename");
        for (const auto *name : {"ContextMenu.qml", "LauncherContextMenu.qml", "ContextPreviewDialog.qml",
                                 "GroupDialog.qml", "StackPopup.qml", "LocationWindowsPopup.qml"}) {
            QTest::newRow(name) << QString::fromUtf8(name);
        }
    }

    void loadDynamicComponent()
    {
        QFETCH(QString, filename);
        // Load the actual compiled module resources, as plasmashell does.
        // These components are created lazily; compiling the plugin alone
        // does not detect errors such as Connections in Dialog.mainItem.
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/plasma/applet/org/gosh/goshosdock/") + filename));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 10000);
        QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));
    }

private:
    QLibrary m_applet;
};

QTEST_MAIN(QmlComponentsTest)
#include "tst_qmlcomponents.moc"
