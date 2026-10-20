#ifndef ATG_ENGINE_SIM_WANKEL_ENGINE_NODE_H
#define ATG_ENGINE_SIM_WANKEL_ENGINE_NODE_H

#include "engine_node.h"

#include "wankel_rotor_node.h"
#include "wankel_peripheral_port_node.h"
#include "rod_journal_node.h"
#include "intake_node.h"
#include "exhaust_system_node.h"

#include "engine_sim.h"

#include <set>
#include <vector>

namespace es_script {

    class WankelEngineNode : public EngineNode {
    public:
        struct Rotor {
            WankelRotorNode *rotor;
            RodJournalNode *rodJournal;
            IntakeNode *intake;
            ExhaustSystemNode *exhaust;
            WankelPeripheralPortNode *intakePort;
            WankelPeripheralPortNode *exhaustPort;
            double soundAttenuation;
            double primaryLength;
            double intakeRunnerCrossSectionArea;
            double exhaustCrossSectionArea;
            double sparkPlugAngle;
        };

    public:
        WankelEngineNode() { /* void */ }
        virtual ~WankelEngineNode() { /* void */ }

        int addRotor(const Rotor &rotor) {
            m_rotors.push_back(rotor);
            return (int)m_rotors.size() - 1;
        }

        virtual Engine *buildEngine() override {
            std::set<ExhaustSystemNode *> exhaustSystems;
            std::set<IntakeNode *> intakes;
            for (const Rotor &rotor : m_rotors) {
                exhaustSystems.insert(rotor.exhaust);
                intakes.insert(rotor.intake);
            }

            WankelEngine *engine = new WankelEngine;

            EngineContext context;
            context.setEngine(engine);

            Engine::Parameters parameters = m_parameters;
            parameters.crankshaftCount = (int)m_crankshafts.size();
            parameters.cylinderBanks = 0;
            parameters.cylinderCount = 0;
            parameters.exhaustSystemCount = (int)exhaustSystems.size();
            parameters.intakeCount = (int)intakes.size();
            parameters.throttle = m_throttle->generate();
            engine->initialize(parameters);

            {
                int i = 0;
                for (ExhaustSystemNode *exhaust : exhaustSystems) {
                    context.addExhaust(exhaust, engine->getExhaustSystem(i++));
                }
            }

            {
                int i = 0;
                for (IntakeNode *intake : intakes) {
                    context.addIntake(intake, engine->getIntake(i++));
                }
            }

            for (int i = 0; i < parameters.crankshaftCount; ++i) {
                m_crankshafts[i]->generate(engine->getCrankshaft(i), &context, 6 * constants::pi);
            }

            const int rotorCount = (int)m_rotors.size();

            WankelEngine::RotorParameters rotorParameters;
            rotorParameters.rotorCount = rotorCount;
            rotorParameters.housingResolution = m_housingResolution;
            rotorParameters.heatTransferCoefficient = m_heatTransferCoefficient;
            rotorParameters.blockTemperature = m_blockTemperature;
            engine->initializeRotors(rotorParameters);

            for (int i = 0; i < rotorCount; ++i) {
                RodJournalNode *rodJournal = m_rotors[i].rodJournal;
                m_rotors[i].rotor->generate(
                    engine->getRotor(i),
                    context.getCrankshaft(rodJournal->getCrankshaft()),
                    context.getRodJournalIndex(rodJournal),
                    m_rotorCalculationQuality);

                WankelRotorHousing::Parameters housingParameters;
                housingParameters.portCount = 2;
                housingParameters.rotor = engine->getRotor(i);
                engine->getHousing(i)->initialize(housingParameters);

                context.setCylinderIndex(nullptr, i, i);
            }

            m_ignitionModule->generate(engine, rotorCount, &context);

            engine->generateHousingProfiles();
            engine->precomputeRotorFunctions();

            m_fuel->generate(engine->getFuel(), &context);

            for (int i = 0; i < rotorCount; ++i) {
                const Rotor &rotor = m_rotors[i];
                WankelCombustionChamber *chamber = engine->getRotorChamber(i);
                WankelRotorHousing *housing = engine->getHousing(i);

                WankelCombustionChamber::Parameters chamberParameters;
                chamberParameters.intake = rotor.intake->generate(&context);
                chamberParameters.exhaust = rotor.exhaust->generate(&context);
                chamberParameters.rotor = engine->getRotor(i);
                chamberParameters.housing = housing;
                chamberParameters.fuel = engine->getFuel();
                chamberParameters.engine = engine;
                chamberParameters.startingPressure = units::pressure(1.0, units::atm);
                chamberParameters.startingTemperature = units::celcius(25.0);
                chamberParameters.intakeRunnerCrossSectionArea = rotor.intakeRunnerCrossSectionArea;
                chamberParameters.exhaustCrossSectionArea = rotor.exhaustCrossSectionArea;
                chamberParameters.primaryLength = rotor.primaryLength;
                chamberParameters.soundAttenuation = rotor.soundAttenuation;
                chamberParameters.sparkPlugAngle = rotor.sparkPlugAngle;
                chamber->initialize(chamberParameters);

                rotor.intakePort->generate(housing->getPort(0), &chamber->m_intakeRunner, &context);
                rotor.exhaustPort->generate(housing->getPort(1), &chamber->m_exhaustRunner, &context);
            }

            return engine;
        }

    protected:
        virtual void registerInputs() {
            addInput("rotor_calculation_quality", &m_rotorCalculationQuality);
            addInput("rotor_housing_resolution", &m_housingResolution);
            addInput("heat_transfer_coefficient", &m_heatTransferCoefficient);
            addInput("block_temperature", &m_blockTemperature);

            EngineNode::registerInputs();
        }

        int m_rotorCalculationQuality = 1024;
        int m_housingResolution = 256;
        double m_heatTransferCoefficient = 1000.0;
        double m_blockTemperature = units::celcius(190.0);

        std::vector<Rotor> m_rotors;
    };

} /* namespace es_script */

#endif /* ATG_ENGINE_SIM_WANKEL_ENGINE_NODE_H */
