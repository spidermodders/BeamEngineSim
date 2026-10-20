#include "../include/wankel_rotor_housing.h"

#include "../include/wankel_rotor.h"
#include "../include/constants.h"
#include "../include/utilities.h"

#include <cmath>

WankelPeripheralPort::WankelPeripheralPort() {
    m_flowRate = nullptr;
    m_runner = nullptr;
    m_startAngle = m_endAngle = 0;
    m_dx = m_dy = 0;
}

WankelPeripheralPort::~WankelPeripheralPort() {
    /* void */
}

void WankelPeripheralPort::initialize(const Parameters &params) {
    m_flowRate = params.flowRate;
    m_runner = params.runner;
    m_startAngle = params.startAngle;
    m_endAngle = params.endAngle;

    const double mid = m_startAngle + positiveAngularDistance(m_startAngle, m_endAngle) * 0.5;
    m_dx = std::cos(mid);
    m_dy = std::sin(mid);
}

WankelRotorHousing::WankelRotorHousing() {
    m_rotor = nullptr;
    m_ports = nullptr;
    m_portCount = 0;
}

WankelRotorHousing::~WankelRotorHousing() {
    /* void */
}

void WankelRotorHousing::initialize(const Parameters &params) {
    m_rotor = params.rotor;
    m_portCount = params.portCount;
    m_ports = new WankelPeripheralPort[m_portCount];
}

void WankelRotorHousing::destroy() {
    delete[] m_ports;
    m_ports = nullptr;
    m_portCount = 0;

    m_profile.destroy();
}

void WankelRotorHousing::generateHousingProfile(int resolution) {
    m_profile.initialize(resolution, 1.0);

    Crankshaft *crankshaft = m_rotor->getCrankshaft();
    const double offset = m_rotor->getAngleOffset();
    const double k = crankshaft->getThrow() / m_rotor->getRadius();
    const double phi = (crankshaft->getRodJournalAngle(m_rotor->getRodJournal()) - offset) * 0.5;
    const double s1 = std::sin(phi), c1 = std::cos(phi);

    for (int i = 0; i < resolution; ++i) {
        const double t = i * (2 * constants::pi / (resolution - 1));
        const double x = std::cos(3 * t) * k + std::cos(t);
        const double y = std::sin(3 * t) * k + std::sin(t);
        const double ry = y * c1 - x * s1;
        const double rx = x * c1 + y * s1;

        const double angle = positiveMod(vectorAngle(rx, ry) + offset, 2 * constants::pi);
        m_profile.addSample(angle, std::sqrt(rx * rx + ry * ry) * m_rotor->getRadius());
    }
}

int WankelRotorHousing::findProfileSegment(double angle) const {
    const int i = m_profile.closestSample(angle);
    if (i == -1) return -1;

    return (m_profile.getSampleX(i) < angle)
        ? (i + 1) % m_profile.getSampleCount()
        : i;
}

void WankelRotorHousing::getProfilePoint(int i, double *x, double *y, double *angle) const {
    double c_x, c_y;
    m_rotor->getHousingCenter(&c_x, &c_y);

    const double a = m_profile.getSampleX(i);
    const double r = m_profile.getSampleY(i);
    *x = std::cos(a) * r + c_x;
    *y = std::sin(a) * r + c_y;
    *angle = a;
}
