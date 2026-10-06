#pragma once

#include <QString>

/**
 * The few words the island shows. Chinese when the system locale is Chinese,
 * English otherwise; resolved once, at the first call.
 */
namespace IslandStrings {

QString connecting();
QString paired();
QString notPaired();
QString charging();
QString chargeLimit(int percent);
QString connectFailed();
QString connectTimedOut();

}  // namespace IslandStrings
