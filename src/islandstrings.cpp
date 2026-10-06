#include "islandstrings.h"

#include <QLocale>

namespace {

bool isChinese()
{
    static const bool chinese = QLocale::system().language() == QLocale::Chinese;
    return chinese;
}

QString pick(const char *zh, const char *en)
{
    return QString::fromUtf8(isChinese() ? zh : en);
}

}  // namespace

namespace IslandStrings {

QString connecting()      { return pick("连接中…", "Connecting…"); }
QString connected()       { return pick("已连接", "Connected"); }
QString charging()        { return pick("正在充电", "Charging"); }
QString connectFailed()   { return pick("连接失败", "Connection failed"); }
QString connectTimedOut() { return pick("连接超时", "Timed out"); }

QString chargeLimit(int percent)
{
    return pick("上限 %1%", "Limit %1%").arg(percent);
}

}  // namespace IslandStrings
