#include "../include/wankel_combustion_chamber.h"

#include "../include/wankel_engine.h"
#include "../include/wankel_rotor.h"
#include "../include/wankel_rotor_housing.h"
#include "../include/constants.h"
#include "../include/units.h"
#include "../include/utilities.h"

#include <cmath>
#include <cstdlib>

WankelCombustionChamber::WankelCombustionChamber() {
    m_engine = nullptr;
    m_intake = nullptr;
    m_exhaust = nullptr;
    m_fuel = nullptr;
    m_rotor = nullptr;
    m_housing = nullptr;

    for (int i = 0; i < 3; ++i) {
        m_lastTimestepExhaustFlow[i] = 0;
        m_lastTimestepIntakeFlow[i] = 0;
        m_lit[i] = false;
        m_litLastFrame[i] = false;
    }

    m_intakeRunnerCrossSectionArea = 0;
    m_exhaustCrossSectionArea = 0;
    m_primaryLength = 0;
    m_soundAttenuation = 1.0;
    m_sparkPlugAngle = 0;

    m_pressure = nullptr;
    m_flankSpeed = nullptr;
}

WankelCombustionChamber::~WankelCombustionChamber() {
    /* void */
}

void WankelCombustionChamber::initialize(const Parameters &params) {
    m_intake = params.intake;
    m_rotor = params.rotor;
    m_housing = params.housing;
    m_exhaust = params.exhaust;
    m_fuel = params.fuel;
    m_engine = params.engine;
    m_intakeRunnerCrossSectionArea = params.intakeRunnerCrossSectionArea;
    m_exhaustCrossSectionArea = params.exhaustCrossSectionArea;
    m_primaryLength = params.primaryLength;
    m_soundAttenuation = params.soundAttenuation;
    m_sparkPlugAngle = params.sparkPlugAngle;

    for (int i = 0; i < 3; ++i) {
        m_chambers[i].initialize(
            params.startingPressure,
            m_rotor->getChamberVolume(i),
            params.startingTemperature);
        m_chambers[i].setGeometry(0.5, 0.5, 1.0, 0.0);
    }

    const double runnerLength = m_intake->getRunnerLength();
    m_intakeRunner.initialize(
        units::pressure(1.0, units::atm),
        m_intakeRunnerCrossSectionArea * runnerLength,
        units::celcius(25.0));
    m_intakeRunner.setGeometry(runnerLength, std::sqrt(m_intakeRunnerCrossSectionArea), 1.0, 0.0);

    const double exhaustLength = m_exhaust->getPrimaryTubeLength() + m_primaryLength;
    m_exhaustRunner.initialize(
        units::pressure(1.0, units::atm),
        m_exhaustCrossSectionArea * exhaustLength,
        units::celcius(25.0));
    m_exhaustRunner.setGeometry(exhaustLength, std::sqrt(m_exhaustCrossSectionArea), 1.0, 0.0);

    m_pressure = new double[StateSamples]();
    m_flankSpeed = new double[StateSamples]();
}

void WankelCombustionChamber::destroy() {
    delete[] m_pressure;
    delete[] m_flankSpeed;

    m_pressure = nullptr;
    m_flankSpeed = nullptr;
}

void WankelCombustionChamber::apply(atg_scs::SystemState *system) {
    double f_x = 0, f_y = 0;
    for (int i = 0; i < 3; ++i) {
        double n_x, n_y;
        m_rotor->getFlankNormal(i, &n_x, &n_y);

        const double P = m_chambers[i].pressure();
        f_x -= P * n_x * m_rotor->getFlankArea();
        f_y -= P * n_y * m_rotor->getFlankArea();
    }

    system->applyForce(0.0, 0.0, f_x, f_y, m_rotor->m_body.index);
}

void WankelCombustionChamber::getApexAngles(double *angles) const {
    double x[3], y[3], h_x, h_y;
    m_rotor->getApexPositions(x, y);
    m_rotor->getHousingCenter(&h_x, &h_y);

    for (int i = 0; i < 3; ++i) {
        angles[i] = vectorAngle(x[i] - h_x, y[i] - h_y);
    }
}

double WankelCombustionChamber::getPortExposure(int chamber, const GasSystem *runner) const {
    double apex[3];
    getApexAngles(apex);

    double exposure = 0;
    for (int i = 0; i < m_housing->getPortCount(); ++i) {
        const WankelPeripheralPort *port = m_housing->getPort(i);
        if (port->getRunner() != runner) continue;

        exposure += angularOverlap(
            apex[chamber], apex[(chamber + 1) % 3], port->getStartAngle(), port->getEndAngle());
    }

    return exposure;
}

double WankelCombustionChamber::getIntakePortExposure(int chamber) const {
    return getPortExposure(chamber, &m_intakeRunner);
}

double WankelCombustionChamber::getExhaustPortExposure(int chamber) const {
    return getPortExposure(chamber, &m_exhaustRunner);
}

bool WankelCombustionChamber::popLitLastFrame(int chamber) {
    const bool lit = m_litLastFrame[chamber];
    m_litLastFrame[chamber] = false;

    return lit;
}

void WankelCombustionChamber::resetLastTimestepExhaustFlow() {
    for (int i = 0; i < 3; ++i) m_lastTimestepExhaustFlow[i] = 0;
}

void WankelCombustionChamber::resetLastTimestepIntakeFlow() {
    for (int i = 0; i < 3; ++i) m_lastTimestepIntakeFlow[i] = 0;
}

double WankelCombustionChamber::getLastTimestepTotalExhaustFlow() const {
    return m_lastTimestepExhaustFlow[0] + m_lastTimestepExhaustFlow[1] + m_lastTimestepExhaustFlow[2];
}

// Fires whichever chamber currently faces the spark plug
void WankelCombustionChamber::ignite() {
    double apex[3];
    getApexAngles(apex);

    int c = -1;
    for (int i = 0; i < 3; ++i) {
        if (positiveAngularDistance(apex[i], m_sparkPlugAngle) <= 2 * constants::pi / 3) {
            c = i;
            break;
        }
    }

    if (c == -1) return;

    GasSystem &gas = m_chambers[c];
    const GasSystem::Mix mix = gas.mix();
    if (mix.p_fuel == 0) return;

    const double afr = mix.p_o2 / mix.p_fuel;
    const double equivalenceRatio = afr / m_fuel->getMolecularAfr();
    if (equivalenceRatio < 0.5 || equivalenceRatio > 1.9) return;

    const double dilution = std::fmax(mix.p_inert - (mix.p_o2 + mix.p_inert) * 0.75, 0.0);
    if (dilution > 0.15 && std::rand() % 5 != 0) return;

    FlameEvent &flame = m_flameEvents[c];
    flame.lastVolume = m_rotor->getChamberVolume(c);
    flame.travel_x = flame.travel_y = flame.travel_z = 0;
    flame.lit_n = 0;
    flame.total_n = gas.n();
    flame.globalMix = mix;
    m_lit[c] = true;
    m_litLastFrame[c] = true;

    double meanFlankSpeed = 0;
    for (int i = 0; i < StateSamples; ++i) meanFlankSpeed += m_flankSpeed[i];
    meanFlankSpeed /= StateSamples;

    const double turbulence = clamp(meanFlankSpeed / m_fuel->getMaxTurbulenceEffect());
    const double randomness = m_fuel->getBurningEfficiencyRandomness();
    const double rand_s = ((double)std::rand() / RAND_MAX) * randomness + (1 - randomness);
    flame.efficiency =
        (rand_s * m_fuel->getLowEfficiencyAttenuation() * (1 - turbulence) + turbulence)
        * m_fuel->getMaxBurningEfficiency();
    flame.flameSpeed = m_fuel->flameSpeed(meanFlankSpeed, afr, gas.temperature(), gas.pressure(), 0, 0);
}

void WankelCombustionChamber::update(double dt) {
    Crankshaft *crankshaft = m_rotor->getCrankshaft();
    double cycleAngle = crankshaft->getCycleAngle();
    if (!std::isfinite(cycleAngle)) cycleAngle = 0;

    const int sample =
        (int)std::round((cycleAngle / crankshaft->getCycleLength()) * (StateSamples - 1.0));
    double n_x, n_y;
    m_rotor->getFlankNormal(0, &n_x, &n_y);
    m_pressure[sample] = m_chambers[0].pressure();
    m_flankSpeed[sample] = std::abs(n_x * m_rotor->m_body.v_x + n_y * m_rotor->m_body.v_y);

    const double flankArea = m_rotor->getFlankArea();
    const double depth = m_rotor->getDepth();
    for (int i = 0; i < 3; ++i) {
        GasSystem &gas = m_chambers[i];
        const double volume = m_rotor->getChamberVolume(i);
        if (volume > 0) gas.setVolume(volume);

        m_rotor->getFlankNormal(i, &n_x, &n_y);
        gas.setGeometry(flankArea, depth, n_y, -n_x);

        if (!m_lit[i]) continue;

        FlameEvent &flame = m_flameEvents[i];
        const double step = dt * flame.flameSpeed;
        const double lastTravel_x = flame.travel_x;
        const double lastTravel_y = flame.travel_y;
        const double lastTravel_z = (volume / flame.lastVolume) * flame.travel_z;

        flame.travel_x = std::fmin(lastTravel_x + step, (flankArea / depth) * 0.5);
        flame.travel_y = std::fmin(lastTravel_y + step, depth * 0.5);
        flame.travel_z = std::fmin(lastTravel_z + step, volume / flankArea);

        if (lastTravel_x < flame.travel_x || lastTravel_y < flame.travel_y || lastTravel_z < flame.travel_z) {
            const double burned =
                flame.travel_x * 4 * flame.travel_x * constants::pi * flame.travel_y
                - lastTravel_x * 4 * lastTravel_x * constants::pi * lastTravel_y;
            const double n = (burned / volume) * gas.n();
            const double fuelBurned = gas.react(n * flame.efficiency, flame.globalMix);
            gas.changeEnergy(fuelBurned * m_fuel->getMolecularMass() * m_fuel->getEnergyDensity());
            flame.lit_n += n;
        }
        else {
            m_lit[i] = false;
        }

        flame.lastVolume = volume;
    }
}

void WankelCombustionChamber::flow(double dt) {
    GasSystem::FlowParameters flowParams;
    flowParams.dt = dt;
    flowParams.direction_x = 1.0;
    flowParams.direction_y = 0.0;

    flowParams.k_flow = m_intake->getRunnerFlowRate();
    flowParams.crossSectionArea_0 = m_intake->getPlenumCrossSectionArea();
    flowParams.crossSectionArea_1 = m_intakeRunnerCrossSectionArea;
    flowParams.system_0 = &m_intake->m_system;
    flowParams.system_1 = &m_intakeRunner;
    GasSystem::flow(flowParams);

    double apex[3];
    getApexAngles(apex);

    for (int i = 0; i < 3; ++i) {
        const double a0 = apex[i], a1 = apex[(i + 1) % 3];
        const double span = positiveAngularDistance(a0, a1);

        for (int j = 0; j < m_housing->getPortCount(); ++j) {
            const WankelPeripheralPort *port = m_housing->getPort(j);
            const double overlap = angularOverlap(a0, a1, port->getStartAngle(), port->getEndAngle());
            if (overlap == 0) continue;

            const bool exhaust = port->getRunner() == &m_exhaustRunner;
            port->getDirection(&flowParams.direction_x, &flowParams.direction_y);
            flowParams.k_flow = port->getFlowRate()->sampleLinear(overlap / span);
            flowParams.crossSectionArea_0 = m_rotor->getFlankArea();
            flowParams.crossSectionArea_1 = exhaust ? m_exhaustCrossSectionArea : m_intakeRunnerCrossSectionArea;
            flowParams.system_0 = &m_chambers[i];
            flowParams.system_1 = port->getRunner();

            const double flow = GasSystem::flow(flowParams);
            if (exhaust) m_lastTimestepExhaustFlow[i] += flow;
            else m_lastTimestepIntakeFlow[i] -= flow;
        }
    }

    flowParams.k_flow = m_exhaust->getPrimaryFlowRate();
    flowParams.crossSectionArea_0 = m_exhaustCrossSectionArea;
    flowParams.crossSectionArea_1 = m_exhaust->getCollectorCrossSectionArea();
    flowParams.direction_x = 1.0;
    flowParams.direction_y = 0.0;
    flowParams.system_0 = &m_exhaustRunner;
    flowParams.system_1 = m_exhaust->getSystem();
    GasSystem::flow(flowParams);

    m_intakeRunner.updateVelocity(dt, 0.5);
    m_intakeRunner.dissipateExcessVelocity();
    m_exhaustRunner.updateVelocity(dt, 0.5);
    m_exhaustRunner.dissipateExcessVelocity();
    for (int i = 0; i < 3; ++i) {
        m_chambers[i].updateVelocity(dt, 0.5);
        m_chambers[i].dissipateExcessVelocity();
    }

    // Wall heat transfer: runner tube surface and each chamber's wetted perimeter
    const double h = m_engine->getHeatTransferCoefficient();
    const double T_block = m_engine->getBlockTemperature();
    m_intakeRunner.changeEnergy(
        std::sqrt(m_intakeRunnerCrossSectionArea / constants::pi) * 2 * constants::pi
        * m_intake->getRunnerLength() * (T_block - m_intakeRunner.temperature()) * h * dt);

    for (int i = 0; i < 3; ++i) {
        m_chambers[i].changeEnergy(
            (T_block - m_chambers[i].temperature()) * m_rotor->getChamberPerimeter(i) * h * dt);
    }
}
