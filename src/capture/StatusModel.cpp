#include "capture/StatusModel.h"

void StatusModel::setDeviceStatus(const QString& text) {
    if (m_deviceStatus == text) return;
    m_deviceStatus = text;
    emit deviceStatusChanged();
}

void StatusModel::setLastOutcome(const QString& text) {
    if (m_lastOutcome == text) return;
    m_lastOutcome = text;
    emit lastOutcomeChanged();
}
