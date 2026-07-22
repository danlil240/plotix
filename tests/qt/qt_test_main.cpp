#include <QApplication>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    // Keep QApplication in normal stack lifetime: it is created before every
    // Qt fixture and destroyed before the main thread's Qt TLS is torn down.
    QApplication app(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
