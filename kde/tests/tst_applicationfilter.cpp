// SPDX-License-Identifier: GPL-2.0-or-later
#include <libmalcontent/app-filter.h>
#include "applicationfilter.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class ApplicationFilterTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void appliesNativeExecutableAndFlatpakRules()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString desktop = directory.filePath(QStringLiteral("blocked.desktop"));
        QFile file(desktop);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Desktop Entry]\nType=Application\nName=Filter Test\nExec=/usr/bin/true\nX-Flatpak=org.gosh.Restricted\n");
        file.close();
        auto *builder = mct_app_filter_builder_new();
        mct_app_filter_builder_blocklist_path(builder, "/usr/bin/true");
        auto *filter = mct_app_filter_builder_end(builder);
        QVERIFY(!ApplicationFilter::allows(filter, desktop));
        mct_app_filter_unref(filter);
        mct_app_filter_builder_free(builder);

        builder = mct_app_filter_builder_new();
        mct_app_filter_builder_blocklist_flatpak_ref(builder, "app/org.gosh.Restricted/x86_64/stable");
        filter = mct_app_filter_builder_end(builder);
        QVERIFY(!ApplicationFilter::allows(filter, desktop));
        mct_app_filter_unref(filter);
        mct_app_filter_builder_free(builder);

        builder = mct_app_filter_builder_new();
        filter = mct_app_filter_builder_end(builder);
        QVERIFY(ApplicationFilter::allows(filter, desktop));
        QVERIFY(!ApplicationFilter::allows(filter, directory.filePath(QStringLiteral("missing.desktop"))));
        QVERIFY(ApplicationFilter::allows(nullptr, desktop));
        mct_app_filter_unref(filter);
        mct_app_filter_builder_free(builder);
    }
};
QTEST_GUILESS_MAIN(ApplicationFilterTest)
#include "tst_applicationfilter.moc"
