#include "view/OrientationModel.h"

void OrientationModel::setLocked(bool locked) {
    if (m_locked == locked) return;
    m_locked = locked;
    emit changed();
}

// All four mutators refuse while locked rather than queueing the change.
// Deferring would be worse than refusing: the operator would rotate, see
// nothing happen, and then have the view jump after they pressed Stop.
void OrientationModel::rotateClockwise() {
    if (m_locked) return;
    m_o.rotateClockwise();
    emit changed();
}

void OrientationModel::rotateCounterClockwise() {
    if (m_locked) return;
    m_o.rotateCounterClockwise();
    emit changed();
}

void OrientationModel::toggleMirror() {
    if (m_locked) return;
    m_o.toggleMirror();
    emit changed();
}

void OrientationModel::reset() {
    if (m_locked) return;
    if (m_o.isIdentity()) return;
    m_o.reset();
    emit changed();
}
