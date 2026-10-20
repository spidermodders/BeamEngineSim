#include "../include/wankel_engine.h"

#include "../include/wankel_engine_simulator.h"
#include "../include/constants.h"

#include <cassert>

WankelEngine::WankelEngine() {
    m_rotors = nullptr;
    m_housings = nullptr;
    m_rotorChambers = nullptr;
    m_rotorCount = 0;
    m_housingResolution = 256;
    m_heatTransferCoefficient = 0;
    m_blockTemperature = 0;
}

WankelEngine::~WankelEngine() {
    assert(m_rotors == nullptr);
}

void WankelEngine::initializeRotors(const RotorParameters &params) {
    m_rotorCount = params.rotorCount;
    m_housingResolution = params.housingResolution;
    m_heatTransferCoefficient = params.heatTransferCoefficient;
    m_blockTemperature = params.blockTemperature;

    m_rotors = new WankelRotor[m_rotorCount];
    m_housings = new WankelRotorHousing[m_rotorCount];
    m_rotorChambers = new WankelCombustionChamber[m_rotorCount];
}

void WankelEngine::destroy() {
    for (int i = 0; i < m_rotorCount; ++i) {
        m_rotors[i].destroy();
        m_housings[i].destroy();
        m_rotorChambers[i].destroy();
    }

    delete[] m_rotors;
    delete[] m_housings;
    delete[] m_rotorChambers;

    m_rotors = nullptr;
    m_housings = nullptr;
    m_rotorChambers = nullptr;
    m_rotorCount = 0;

    Engine::destroy();
}

void WankelEngine::calculateDisplacement() {
    double displacement = 0;
    for (int i = 0; i < m_rotorCount; ++i) {
        displacement += m_rotors[i].getMaxVolume() - m_rotors[i].getMinVolume();
    }

    m_displacement = displacement;
}

Simulator *WankelEngine::createSimulator(Vehicle *vehicle, Transmission *transmission) {
    WankelEngineSimulator *simulator = new WankelEngineSimulator;
    Simulator::Parameters simulatorParams;
    simulatorParams.systemType = Simulator::SystemType::NsvOptimized;
    simulator->initialize(simulatorParams);

    simulator->loadSimulation(this, vehicle, transmission);

    return simulator;
}

void WankelEngine::generateHousingProfiles() {
    for (int i = 0; i < m_rotorCount; ++i) {
        m_housings[i].generateHousingProfile(m_housingResolution);
    }
}

void WankelEngine::precomputeRotorFunctions() {
    for (int i = 0; i < m_rotorCount; ++i) {
        m_rotors[i].generateFlankProfile();
        m_rotors[i].calculateFlankArea();
    }

    // Sweep the eccentric shaft through one full chamber cycle and tabulate chamber volume
    for (int i = 0; i < m_rotorCount; ++i) {
        WankelRotor *rotor = &m_rotors[i];
        Crankshaft *crankshaft = rotor->getCrankshaft();
        for (int j = 0; j < WankelRotor::ChamberSamples; ++j) {
            crankshaft->m_body.p_x = crankshaft->getPosX();
            crankshaft->m_body.p_y = crankshaft->getPosY();
            crankshaft->m_body.theta = j * (crankshaft->getCycleLength() / WankelRotor::ChamberSamples);
            rotor->m_body.theta = rotor->getAngle();
            crankshaft->getRodJournalPositionGlobal(
                rotor->getRodJournal(), &rotor->m_body.p_x, &rotor->m_body.p_y);
            rotor->sampleChamber(&m_housings[i]);
        }
    }
}
