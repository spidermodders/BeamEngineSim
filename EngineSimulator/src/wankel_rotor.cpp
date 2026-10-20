#include "../include/wankel_rotor.h"

#include "../include/wankel_rotor_housing.h"
#include "../include/constants.h"
#include "../include/utilities.h"

#include <cfloat>
#include <cmath>

// Ported from the engine-sim 0.1.14a binary; geometry quirks (mixed absolute and housing-relative
// points in the shoelace sums) are kept as-is so chamber volumes match the original

WankelRotor::WankelRotor() {
    m_rodJournal = 0;
    m_flankResolution = 0;
    m_radius = 0;
    m_crownGearRadius = 0;
    m_mass = 0;
    m_momentOfInertia = 0;
    m_angleOffset = 0;
    m_depth = 0;
    m_depressionVolume = 0;
    m_flankArea = 0;
    m_maxVolume = -DBL_MAX;
    m_minVolume = DBL_MAX;
    m_crankshaft = nullptr;
}

WankelRotor::~WankelRotor() {
    /* void */
}

void WankelRotor::initialize(const Parameters &params) {
    m_crankshaft = params.crankshaft;
    m_mass = params.mass;
    m_momentOfInertia = params.momentOfInertia;
    m_rodJournal = params.rodJournal;
    m_radius = params.radius;
    m_crownGearRadius = params.crownGearRadius;
    m_angleOffset = params.angle;
    m_depth = params.depth;
    m_depressionVolume = params.depressionVolume;
    m_flankResolution = params.flankResolution;
    m_maxVolume = -DBL_MAX;
    m_minVolume = DBL_MAX;

    m_flankProfile.initialize(m_flankResolution, 1.0);
    m_chamberPerimeter.initialize(ChamberSamples, 1.0);
    m_chamberVolume.initialize(ChamberSamples, 1.0);
}

void WankelRotor::destroy() {
    m_flankProfile.destroy();
    m_chamberPerimeter.destroy();
    m_chamberVolume.destroy();
}

void WankelRotor::getApexPositions(double *x, double *y) const {
    for (int i = 0; i < 3; ++i) {
        const double a = m_angleOffset + i * (2 * constants::pi / 3) + m_body.theta;
        x[i] = std::cos(a) * m_radius + m_body.p_x;
        y[i] = std::sin(a) * m_radius + m_body.p_y;
    }
}

void WankelRotor::getFlankNormal(int flank, double *x, double *y) const {
    const double a = m_angleOffset + flank * (2 * constants::pi / 3) + constants::pi / 3 + m_body.theta;
    *x = std::cos(a);
    *y = std::sin(a);
}

void WankelRotor::getHousingCenter(double *x, double *y) const {
    *x = m_crankshaft->getPosX();
    *y = m_crankshaft->getPosY();
}

double WankelRotor::sampleCycle(const Function &f, int chamber) const {
    const double cycle = m_crankshaft->getCycleLength();
    double x = m_crankshaft->getCycleAngle() - chamber * 2 * constants::pi;
    if (x > cycle) x -= cycle;
    else if (x < 0) x += cycle;

    return f.sampleLinear(x);
}

double WankelRotor::getChamberVolume(int chamber) const {
    return sampleCycle(m_chamberVolume, chamber);
}

double WankelRotor::getChamberPerimeter(int chamber) const {
    return sampleCycle(m_chamberPerimeter, chamber);
}

void WankelRotor::generateFlankProfile() {
    const double k = m_crankshaft->getThrow() / m_radius;
    const int n = m_flankResolution;
    for (int i = 0; i < n; ++i) {
        const double t = i * ((constants::pi / 3) / (n - 1)) + constants::pi / 6;
        const double c2 = std::cos(2 * t), s2 = std::sin(2 * t);
        const double s3 = std::sin(3 * t);
        const double root = std::sqrt(std::fmax(1.0 - 9 * k * k * s3 * s3, 0.0));
        const double c3 = std::cos(3 * t) * 2 * k;
        const double s6 = std::sin(6 * t) * 3 * k * k;

        const double y = s6 * c2 + s2 + c3 * s2 * root;
        const double x = (c2 - s6 * s2) + c3 * c2 * root;
        m_flankProfile.addSample(vectorAngle(x, y) - constants::pi / 3, std::sqrt(x * x + y * y) * m_radius);
    }
}

void WankelRotor::calculateFlankArea() {
    m_flankArea = 2 * std::cos(constants::pi / 6) * m_radius * m_depth;
}

void WankelRotor::sampleChamber(const WankelRotorHousing *housing) {
    const double x = m_crankshaft->getCycleAngle();
    const double volume = calculateChamberArea(0, housing) * m_depth + m_depressionVolume;
    m_chamberVolume.addSample(x, volume);
    m_chamberPerimeter.addSample(x, calculateChamberPerimeter(0, housing));

    if (volume > m_maxVolume) m_maxVolume = volume;
    if (volume < m_minVolume) m_minVolume = volume;
}

double WankelRotor::calculateChamberArea(int chamber, const WankelRotorHousing *housing) const {
    double apex_x[3], apex_y[3];
    getApexPositions(apex_x, apex_y);

    double h_x, h_y;
    getHousingCenter(&h_x, &h_y);

    const int next = (chamber + 1) % 3;
    const double a_x = apex_x[chamber] - h_x, a_y = apex_y[chamber] - h_y;
    const double b_x = apex_x[next] - h_x, b_y = apex_y[next] - h_y;
    const double apexAngle = m_angleOffset + chamber * (2 * constants::pi / 3);

    double sum = 0, prev_x = b_x, prev_y = b_y;
    for (int i = m_flankProfile.getSampleCount() - 2; i >= 0; --i) {
        const double a = m_body.theta + apexAngle + m_flankProfile.getSampleX(i);
        const double r = m_flankProfile.getSampleY(i);
        const double x = std::cos(a) * r + m_body.p_x;
        const double y = std::sin(a) * r + m_body.p_y;
        sum += y * prev_x - x * prev_y;
        prev_x = x;
        prev_y = y;
    }

    int segment = housing->findProfileSegment(vectorAngle(a_x, a_y));
    const int end = housing->findProfileSegment(vectorAngle(b_x, b_y));
    while (segment != end) {
        double x, y, angle;
        housing->getProfilePoint(segment, &x, &y, &angle);
        segment = (segment < housing->getProfileSize() - 1) ? segment + 1 : 0;
        sum += y * prev_x - x * prev_y;
        prev_x = x;
        prev_y = y;
    }

    return ((prev_x * b_y - prev_y * b_x) + sum) * 0.5;
}

double WankelRotor::calculateChamberPerimeter(int chamber, const WankelRotorHousing *housing) const {
    double apex_x[3], apex_y[3];
    getApexPositions(apex_x, apex_y);

    double h_x, h_y;
    getHousingCenter(&h_x, &h_y);

    const int next = (chamber + 1) % 3;
    const double a_x = apex_x[chamber] - h_x, a_y = apex_y[chamber] - h_y;
    const double b_x = apex_x[next] - h_x, b_y = apex_y[next] - h_y;
    const double apexAngle = m_angleOffset + chamber * (2 * constants::pi / 3);

    double sum = 0, flankWall = 0, housingWall = 0, prev_x = b_x, prev_y = b_y;
    for (int i = m_flankProfile.getSampleCount() - 2; i >= 0; --i) {
        const double a = m_body.theta + apexAngle + m_flankProfile.getSampleX(i);
        const double r = m_flankProfile.getSampleY(i);
        const double x = std::cos(a) * r + m_body.p_x;
        const double y = std::sin(a) * r + m_body.p_y;
        flankWall += std::sqrt((y - prev_y) * (y - prev_y) + (x - prev_x) * (x - prev_x)) * m_depth;
        sum += y * prev_x - x * prev_y;
        prev_x = x;
        prev_y = y;
    }

    int segment = housing->findProfileSegment(vectorAngle(a_x, a_y));
    const int end = housing->findProfileSegment(vectorAngle(b_x, b_y));
    while (segment != end) {
        double x, y, angle;
        housing->getProfilePoint(segment, &x, &y, &angle);
        sum += y * prev_x - x * prev_y;
        housingWall += std::sqrt((y - prev_y) * (y - prev_y) + (x - prev_x) * (x - prev_x)) * m_depth;
        segment = (segment < housing->getProfileSize() - 1) ? segment + 1 : 0;
        prev_x = x;
        prev_y = y;
    }

    return ((prev_x * b_y - prev_y * b_x) + sum) * 0.5 + housingWall + flankWall;
}
