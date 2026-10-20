#include "../include/wankel_engine_simulator.h"

#include "../include/units.h"

#include <cmath>
#include <assert.h>

WankelEngineSimulator::WankelEngineSimulator() {
    m_engine = nullptr;
    m_transmission = nullptr;
    m_vehicle = nullptr;
    m_delayFilters = nullptr;

    m_crankConstraints = nullptr;
    m_crankshaftLinks = nullptr;
    m_crankshaftFrictionConstraints = nullptr;
    m_rotorJournalConstraints = nullptr;
    m_pinionConstraints = nullptr;

    m_exhaustFlowStagingBuffer = nullptr;

    m_fluidSimulationSteps = 8;
}

WankelEngineSimulator::~WankelEngineSimulator() {
    assert(m_crankConstraints == nullptr);
    assert(m_rotorJournalConstraints == nullptr);
    assert(m_pinionConstraints == nullptr);
    assert(m_exhaustFlowStagingBuffer == nullptr);
    assert(m_delayFilters == nullptr);
}

void WankelEngineSimulator::loadSimulation(WankelEngine *engine, Vehicle *vehicle, Transmission *transmission) {
    Simulator::loadSimulation(engine, vehicle, transmission);

    m_engine = engine;
    m_vehicle = vehicle;
    m_transmission = transmission;

    const int crankCount = m_engine->getCrankshaftCount();
    const int rotorCount = m_engine->getRotorCount();

    if (crankCount <= 0) return;

    m_crankConstraints = new atg_scs::FixedPositionConstraint[crankCount];
    m_crankshaftFrictionConstraints = new atg_scs::RotationFrictionConstraint[crankCount];
    m_crankshaftLinks = new atg_scs::ClutchConstraint[crankCount - 1];
    m_rotorJournalConstraints = new atg_scs::LinkConstraint[rotorCount];
    m_pinionConstraints = new WankelPinionConstraint[rotorCount];
    m_delayFilters = new DelayFilter[rotorCount];

    const double ks = 5000;
    const double kd = 10;

    Crankshaft *outputShaft = m_engine->getCrankshaft(0);
    for (int i = 0; i < crankCount; ++i) {
        Crankshaft *crankshaft = m_engine->getCrankshaft(i);

        m_crankConstraints[i].setBody(&crankshaft->m_body);
        m_crankConstraints[i].setWorldPosition(crankshaft->getPosX(), crankshaft->getPosY());
        m_crankConstraints[i].setLocalPosition(0.0, 0.0);
        m_crankConstraints[i].m_kd = kd;
        m_crankConstraints[i].m_ks = ks;

        crankshaft->m_body.p_x = crankshaft->getPosX();
        crankshaft->m_body.p_y = crankshaft->getPosY();
        crankshaft->m_body.theta = 0;
        crankshaft->m_body.m = crankshaft->getMass() + crankshaft->getFlywheelMass();
        crankshaft->m_body.I = crankshaft->getMomentOfInertia();

        m_crankshaftFrictionConstraints[i].m_minTorque = -crankshaft->getFrictionTorque();
        m_crankshaftFrictionConstraints[i].m_maxTorque = crankshaft->getFrictionTorque();
        m_crankshaftFrictionConstraints[i].setBody(&crankshaft->m_body);

        m_system->addRigidBody(&crankshaft->m_body);
        m_system->addConstraint(&m_crankConstraints[i]);
        m_system->addConstraint(&m_crankshaftFrictionConstraints[i]);

        if (crankshaft != outputShaft) {
            atg_scs::ClutchConstraint *crankLink = &m_crankshaftLinks[i - 1];
            crankLink->setBody1(&outputShaft->m_body);
            crankLink->setBody2(&crankshaft->m_body);

            m_system->addConstraint(crankLink);
        }
    }

    m_transmission->addToSystem(m_system, &m_vehicleMass, m_vehicle, m_engine);
    m_vehicle->addToSystem(m_system, &m_vehicleMass);

    m_vehicleDrag.initialize(&m_vehicleMass, m_vehicle);
    m_system->addConstraint(&m_vehicleDrag);

    m_vehicleMass.reset();
    m_vehicleMass.m = 1.0;
    m_vehicleMass.I = 1.0;
    m_system->addRigidBody(&m_vehicleMass);

    for (int i = 0; i < rotorCount; ++i) {
        WankelRotor *rotor = m_engine->getRotor(i);
        Crankshaft *crankshaft = rotor->getCrankshaft();

        double journal_x, journal_y;
        crankshaft->getRodJournalPositionLocal(rotor->getRodJournal(), &journal_x, &journal_y);

        m_rotorJournalConstraints[i].setBody1(&rotor->m_body);
        m_rotorJournalConstraints[i].setBody2(&crankshaft->m_body);
        m_rotorJournalConstraints[i].setLocalPosition1(0.0, 0.0);
        m_rotorJournalConstraints[i].setLocalPosition2(journal_x, journal_y);

        m_pinionConstraints[i].setBodies(&crankshaft->m_body, &rotor->m_body);

        rotor->m_body.theta = rotor->getAngle();
        rotor->m_body.m = rotor->getMass();
        rotor->m_body.I = rotor->getMomentOfInertia();

        m_system->addRigidBody(&rotor->m_body);
        m_system->addConstraint(&m_rotorJournalConstraints[i]);
        m_system->addConstraint(&m_pinionConstraints[i]);
        m_system->addForceGenerator(m_engine->getRotorChamber(i));
    }

    m_dyno.connectCrankshaft(m_engine->getOutputCrankshaft());
    m_system->addConstraint(&m_dyno);

    m_starterMotor.connectCrankshaft(m_engine->getOutputCrankshaft());
    m_starterMotor.m_maxTorque = m_engine->getStarterTorque();
    m_starterMotor.m_rotationSpeed = -m_engine->getStarterSpeed();
    m_system->addConstraint(&m_starterMotor);

    placeAndInitialize();
    initializeSynthesizer();
}

void WankelEngineSimulator::placeAndInitialize() {
    const int rotorCount = m_engine->getRotorCount();
    for (int i = 0; i < rotorCount; ++i) {
        WankelRotor *rotor = m_engine->getRotor(i);
        rotor->getCrankshaft()->getRodJournalPositionGlobal(
            rotor->getRodJournal(), &rotor->m_body.p_x, &rotor->m_body.p_y);

        WankelCombustionChamber *chamber = m_engine->getRotorChamber(i);
        const double exhaustLength = chamber->getPrimaryLength() + chamber->getExhaustSystem()->getLength();
        const double speedOfSound = 343.0 * units::m / units::sec;
        m_delayFilters[i].initialize(exhaustLength / speedOfSound, 10000.0);
    }

    m_engine->getIgnitionModule()->reset();

    m_exhaustFlowStagingBuffer = new double[m_engine->getExhaustSystemCount()];
}

double WankelEngineSimulator::getAverageOutputSignal() const {
    double sum = 0.0;
    for (int i = 0; i < m_engine->getExhaustSystemCount(); ++i) {
        sum += m_engine->getExhaustSystem(i)->getSystem()->pressure();
    }

    return sum / m_engine->getExhaustSystemCount();
}

void WankelEngineSimulator::simulateStep_() {
    const double timestep = getTimestep();
    IgnitionModule *im = m_engine->getIgnitionModule();
    im->update(timestep);

    const int rotorCount = m_engine->getRotorCount();
    for (int i = 0; i < rotorCount; ++i) {
        WankelRotor *rotor = m_engine->getRotor(i);
        rotor->m_body.theta = rotor->getAngle();
    }

    for (int i = 0; i < rotorCount; ++i) {
        WankelCombustionChamber *chamber = m_engine->getRotorChamber(i);
        if (im->getIgnitionEvent(i)) {
            chamber->ignite();
        }

        chamber->update(timestep);
    }

    for (int i = 0; i < rotorCount; ++i) {
        m_engine->getRotorChamber(i)->resetLastTimestepExhaustFlow();
        m_engine->getRotorChamber(i)->resetLastTimestepIntakeFlow();
    }

    const int exhaustSystemCount = m_engine->getExhaustSystemCount();
    const int intakeCount = m_engine->getIntakeCount();
    const double fluidTimestep = timestep / m_fluidSimulationSteps;
    for (int i = 0; i < m_fluidSimulationSteps; ++i) {
        for (int j = 0; j < exhaustSystemCount; ++j) {
            m_engine->getExhaustSystem(j)->process(fluidTimestep);
        }

        for (int j = 0; j < intakeCount; ++j) {
            m_engine->getIntake(j)->process(fluidTimestep);
            m_engine->getIntake(j)->m_flowRate += m_engine->getIntake(j)->m_flow;
        }

        for (int j = 0; j < rotorCount; ++j) {
            m_engine->getRotorChamber(j)->flow(fluidTimestep);
        }
    }

    im->resetIgnitionEvents();
}

double WankelEngineSimulator::getTotalExhaustFlow() const {
    double totalFlow = 0.0;
    for (int i = 0; i < m_engine->getRotorCount(); ++i) {
        totalFlow += m_engine->getRotorChamber(i)->getLastTimestepTotalExhaustFlow();
    }

    return totalFlow;
}

void WankelEngineSimulator::endFrame() {
    Simulator::endFrame();

    if (m_engine == nullptr) {
        return;
    }

    const double frameTimestep = simulationSteps() * getTimestep();
    for (int i = 0; i < m_engine->getIntakeCount(); ++i) {
        m_engine->getIntake(i)->m_flowRate /= frameTimestep;
    }
}

void WankelEngineSimulator::destroy() {
    if (m_system != nullptr) m_system->reset();

    delete[] m_crankConstraints;
    delete[] m_crankshaftLinks;
    delete[] m_crankshaftFrictionConstraints;
    delete[] m_rotorJournalConstraints;
    delete[] m_pinionConstraints;
    delete[] m_exhaustFlowStagingBuffer;
    delete[] m_delayFilters;
    delete m_system;

    m_crankConstraints = nullptr;
    m_crankshaftLinks = nullptr;
    m_crankshaftFrictionConstraints = nullptr;
    m_rotorJournalConstraints = nullptr;
    m_pinionConstraints = nullptr;
    m_exhaustFlowStagingBuffer = nullptr;
    m_delayFilters = nullptr;
    m_system = nullptr;

    m_vehicle = nullptr;
    m_transmission = nullptr;
    m_engine = nullptr;
}

void WankelEngineSimulator::writeToSynthesizer() {
    const int exhaustSystemCount = m_engine->getExhaustSystemCount();
    for (int i = 0; i < exhaustSystemCount; ++i) {
        m_exhaustFlowStagingBuffer[i] = 0;
    }

    const double attenuation = std::min(std::abs(filteredEngineSpeed()), 40.0) / 40.0;

    const int rotorCount = m_engine->getRotorCount();
    for (int i = 0; i < rotorCount; ++i) {
        WankelCombustionChamber *chamber = m_engine->getRotorChamber(i);
        ExhaustSystem *exhaust = chamber->getExhaustSystem();
        const GasSystem &runner = chamber->m_exhaustRunner;

        const double exhaustLength = chamber->getPrimaryLength() + exhaust->getLength();
        const double runnerSpeed = std::hypot(runner.velocity_x(), runner.velocity_y());
        const double delayedExhaustPulse = m_delayFilters[i].fast_f(runnerSpeed * 10000.0);

        m_exhaustFlowStagingBuffer[exhaust->getIndex()] +=
            chamber->getSoundAttenuation()
            * attenuation
            * (exhaust->getAudioVolume() * delayedExhaustPulse / rotorCount)
            * (1 / (exhaustLength * exhaustLength));
    }

    synthesizer().writeInput(m_exhaustFlowStagingBuffer);
}
