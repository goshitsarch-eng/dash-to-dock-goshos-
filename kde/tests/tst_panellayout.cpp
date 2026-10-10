/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "panellayout.h"

#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

class PanelLayoutTest : public QObject
{
    Q_OBJECT

    static void prepare(QJSEngine &engine)
    {
        const QJSValue result = engine.evaluate(QString::fromUtf8(R"JS(
            var _panels = [], _removed = [], _created = 0, _result = null;
            var _outputs = {"eDP-1":0,"DP-1":1,"HDMI-1":2};
            function print(value) { _result = JSON.parse(value); }
            function configObject(object) {
                object.currentConfigGroup = [];
                object._config = {};
                object._writes = 0;
                Object.defineProperty(object, "configKeys", {get:function() {
                    var prefix = this.currentConfigGroup.join("/") + "/";
                    return Object.keys(this._config).filter(function(path) { return path.startsWith(prefix); })
                        .map(function(path) { return path.substring(prefix.length); });
                }});
                object.readConfig = function(key, fallback) {
                    if (fallback === null || fallback === undefined) throw new Error("KConfig requires a typed default");
                    var path = this.currentConfigGroup.join("/") + "/" + key;
                    return Object.prototype.hasOwnProperty.call(this._config, path) ? this._config[path] : fallback;
                };
                object.writeConfig = function(key, value) {
                    this._config[this.currentConfigGroup.join("/") + "/" + key] = value;
                    this._writes++;
                };
                object.reloadConfig = function() {};
                return object;
            }
            function createPanel(id, plugins, screen) {
                var widgets = plugins.map(function(plugin, index) { return configObject({id:id*10+index,type:plugin}); });
                var panel = configObject({id:id,screen:screen,location:"bottom",height:40,hiding:"autohide"});
                panel.widgets = function() { return widgets; };
                panel.addWidget = function(plugin) {
                    var widget = configObject({id:id*10+widgets.length,type:plugin});
                    widgets.push(widget);
                    return widget;
                };
                panel.remove = function() { _removed.push(id); _panels.splice(_panels.indexOf(panel),1); };
                _panels.push(panel);
                return panel;
            }
            function panels() { return _panels.slice(); }
            function panelById(id) { return _panels.find(function(panel) { return panel.id === id; }); }
            function screenForConnector(name) { return name in _outputs ? _outputs[name] : -1; }
            function Panel() { _created++; return createPanel(1000+_created,[],0); }
            createPanel(10,["org.gosh.goshosdock"],0);
            createPanel(20,["org.kde.plasma.kickoff","org.kde.plasma.icontasks"],0);
            createPanel(30,["org.gosh.goshosdock"],1);
        )JS"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
    }

    static QVariantMap request(bool multiple = false)
    {
        return {{QStringLiteral("panelId"), 10}, {QStringLiteral("appletId"), 100},
                {QStringLiteral("primaryOutput"), QStringLiteral("eDP-1")},
                {QStringLiteral("outputs"), QVariantList{
                    QVariantMap{{QStringLiteral("name"), QStringLiteral("eDP-1")}, {QStringLiteral("width"), 1920}, {QStringLiteral("height"), 1080}},
                    QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-1")}, {QStringLiteral("width"), 2560}, {QStringLiteral("height"), 1440}}}},
                {QStringLiteral("configuration"), QVariantMap{
                    {QStringLiteral("dockPosition"), 3}, {QStringLiteral("preferredOutput"), QStringLiteral("DP-1")},
                    {QStringLiteral("intendedLengthFraction"), 0.5}, {QStringLiteral("extendDock"), false},
                    {QStringLiteral("iconSize"), 64}, {QStringLiteral("allOutputs"), multiple}, {QStringLiteral("launchers"), QStringList{QStringLiteral("applications:test.desktop")}}}}};
    }

    static void run(QJSEngine &engine, const QVariantMap &settings)
    {
        const QJSValue result = engine.evaluate(PanelLayout::scriptFor(settings));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
    }

private Q_SLOTS:
    void refusesMixedOrMismatchedPanel()
    {
        QJSEngine engine;
        prepare(engine);
        QVariantMap options = request();
        options[QStringLiteral("panelId")] = 20;
        options[QStringLiteral("appletId")] = 200;
        run(engine, options);
        QVERIFY(!engine.evaluate(QStringLiteral("_result.ok")).toBool());
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(20)._writes")).toInt(), 0);
        QCOMPARE(engine.evaluate(QStringLiteral("_created")).toInt(), 0);
        options[QStringLiteral("panelId")] = 10;
        run(engine, options);
        QVERIFY(!engine.evaluate(QStringLiteral("_result.ok")).toBool());
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(10)._writes")).toInt(), 0);
    }

    void geometryUsesSelectedOutputAndLeavesOtherPanelsUntouched()
    {
        QJSEngine engine;
        prepare(engine);
        run(engine, request());
        QVERIFY(engine.evaluate(QStringLiteral("_result.ok")).toBool());
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(10).screen")).toInt(), 1);
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(10).location")).toString(), QStringLiteral("left"));
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(10).maximumLength")).toInt(), 720);
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(10).minimumLength")).toInt(), 76);
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(10).height")).toInt(), 76);
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(10).lengthMode")).toString(), QStringLiteral("custom"));
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(10).hiding")).toString(), QStringLiteral("autohide"));
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(20)._writes + panelById(30)._writes")).toInt(), 0);
        QCOMPARE(engine.evaluate(QStringLiteral("_created")).toInt(), 0);
    }

    void sharesSettingsFromEitherScreenWithoutDuplicatingPanels()
    {
        QJSEngine engine;
        prepare(engine);
        QVariantMap options = request(true);
        run(engine, options);
        QCOMPARE(engine.evaluate(QStringLiteral("_created")).toInt(), 1);
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(1001).screen")).toInt(), 0);
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(1001).widgets()[0]._config['General/iconSize']")).toInt(), 64);
        options[QStringLiteral("panelId")] = 1001;
        options[QStringLiteral("appletId")] = 10010;
        QVariantMap configuration = options.value(QStringLiteral("configuration")).toMap();
        configuration[QStringLiteral("iconSize")] = 80;
        configuration[QStringLiteral("extendDock")] = true;
        options[QStringLiteral("configuration")] = configuration;
        run(engine, options);
        QCOMPARE(engine.evaluate(QStringLiteral("_created")).toInt(), 1);
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(10).widgets()[0]._config['General/iconSize']")).toInt(), 80);
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(1001).minimumLength")).toInt(), 540);
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(30)._writes")).toInt(), 0);
    }

    void disablingMultipleScreensRemovesOnlyOwnedCopies()
    {
        QJSEngine engine;
        prepare(engine);
        run(engine, request(true));
        run(engine, request(false));
        QCOMPARE(engine.evaluate(QStringLiteral("JSON.stringify(_removed)")).toString(), QStringLiteral("[1001]"));
        QVERIFY(engine.evaluate(QStringLiteral("!!panelById(30)")).toBool());
        QVERIFY(engine.evaluate(QStringLiteral("!!panelById(20)")).toBool());
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(10).widgets()[0]._config['General/allOutputs']")).toBool(), false);
    }

    void foreignWidgetPreventsAutomaticRemoval()
    {
        QJSEngine engine;
        prepare(engine);
        run(engine, request(true));
        engine.evaluate(QStringLiteral("panelById(1001).addWidget('org.kde.plasma.digitalclock')"));
        run(engine, request(false));
        QVERIFY(engine.evaluate(QStringLiteral("!!panelById(1001)")).toBool());
        QCOMPARE(engine.evaluate(QStringLiteral("_removed.length")).toInt(), 0);
    }

    void unpluggingOutputFallsBackAndRepluggingRestoresPreference()
    {
        QJSEngine engine;
        prepare(engine);
        QVariantMap options = request();
        options[QStringLiteral("outputs")] = QVariantList{options.value(QStringLiteral("outputs")).toList().first()};
        run(engine, options);
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(10).screen")).toInt(), 0);
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(10).widgets()[0]._config['General/preferredOutput']")).toString(), QStringLiteral("DP-1"));
        run(engine, request());
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(10).screen")).toInt(), 1);
    }

    void configurationIsDataAndCannotExecuteScript()
    {
        QJSEngine engine;
        prepare(engine);
        QVariantMap options = request();
        QVariantMap configuration = options.value(QStringLiteral("configuration")).toMap();
        const QString text = QStringLiteral("'); panelById(20).remove(); //\n\"unsafe\"");
        configuration[QStringLiteral("customColor")] = text;
        options[QStringLiteral("configuration")] = configuration;
        run(engine, options);
        QCOMPARE(engine.evaluate(QStringLiteral("_removed.length")).toInt(), 0);
        QCOMPARE(engine.evaluate(QStringLiteral("panelById(10).widgets()[0]._config['General/customColor']")).toString(), text);
    }
};

QTEST_GUILESS_MAIN(PanelLayoutTest)
#include "tst_panellayout.moc"
