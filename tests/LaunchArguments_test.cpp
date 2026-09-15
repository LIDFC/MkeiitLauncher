#include <QTest>

#include "java/JavaVersion.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/launch/InjectAuthlib.h"

// NOTE: don't use raw string literals in this file, moc cannot parse them (see Accounts_test.cpp)
class LaunchArgumentsTest : public QObject {
    Q_OBJECT

   private slots:
    void test_stackShadowPages_data()
    {
        QTest::addColumn<QString>("javaVersion");
        QTest::addColumn<bool>("expected");

        QTest::newRow("Java 25, the runtime of Minecraft 26.3") << "25.0.1" << true;
        QTest::newRow("newer Java") << "26" << true;
        QTest::newRow("Java 21") << "21.0.8" << false;
        QTest::newRow("Java 17") << "17.0.16" << false;
        QTest::newRow("Java 8") << "1.8.0_462" << false;
        QTest::newRow("unknown Java version") << "" << false;
    }

    void test_stackShadowPages()
    {
        QFETCH(QString, javaVersion);
        QFETCH(bool, expected);

        const auto args = MinecraftInstance::compatibilityJvmArguments(JavaVersion(javaVersion));
        QCOMPARE(args.contains("-XX:StackShadowPages=32"), expected);
    }

    void test_describeJvmArguments()
    {
        const QByteArray metadata("{\"meta\":{\"serverName\":\"Ely.by\"}}");
        auto args = InjectAuthlib::buildJvmArguments("C:/data/authlib-injector-1.2.8.jar", "https://authserver.ely.by/api/authlib-injector",
                                                     metadata);
        args.prepend("-Xmx4096m");

        const auto described = InjectAuthlib::describeJvmArguments(args);
        QCOMPARE(described.size(), args.size());
        QCOMPARE(described[0], QString("-Xmx4096m"));
        // the java agent stays visible, so it is clear whether authlib-injector was used
        QCOMPARE(described[1], args[1]);
        QVERIFY(described[2].startsWith("-Dauthlibinjector.yggdrasil.prefetched=<"));
        QVERIFY(!described[2].contains(QString::fromLatin1(metadata.toBase64())));

        // arguments without prefetched metadata are left untouched
        const QStringList plain{ "-Xms512m", "-XX:StackShadowPages=32" };
        QCOMPARE(InjectAuthlib::describeJvmArguments(plain), plain);
    }
};

QTEST_GUILESS_MAIN(LaunchArgumentsTest)
#include "LaunchArguments_test.moc"
