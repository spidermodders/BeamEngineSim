#ifndef ATG_ENGINE_SIM_WANKEL_ENGINE_SIMULATOR_H
#define ATG_ENGINE_SIM_WANKEL_ENGINE_SIMULATOR_H

#include "simulator.h"

#include "wankel_engine.h"
#include "wankel_pinion_constraint.h"
#include "delay_filter.h"

#include "scs.h"

class WankelEngineSimulator : public Simulator {
    public:
        WankelEngineSimulator();
        virtual ~WankelEngineSimulator() override;

        void loadSimulation(WankelEngine *engine, Vehicle *vehicle, Transmission *transmission);

        virtual double getTotalExhaustFlow() const override;
        virtual void endFrame() override;
        virtual void destroy() override;

        void setFluidSimulationSteps(int steps) { m_fluidSimulationSteps = steps; }
        int getFluidSimulationSteps() const { return m_fluidSimulationSteps; }

        virtual double getAverageOutputSignal() const override;

    protected:
        virtual void simulateStep_() override;
        virtual void writeToSynthesizer() override;

        void placeAndInitialize();

    protected:
        DelayFilter *m_delayFilters;

        atg_scs::FixedPositionConstraint *m_crankConstraints;
        atg_scs::ClutchConstraint *m_crankshaftLinks;
        atg_scs::RotationFrictionConstraint *m_crankshaftFrictionConstraints;
        atg_scs::LinkConstraint *m_rotorJournalConstraints;
        WankelPinionConstraint *m_pinionConstraints;
        atg_scs::RigidBody m_vehicleMass;
        VehicleDragConstraint m_vehicleDrag;

        WankelEngine *m_engine;
        Transmission *m_transmission;
        Vehicle *m_vehicle;

        double *m_exhaustFlowStagingBuffer;

        int m_fluidSimulationSteps;
};

#endif /* ATG_ENGINE_SIM_WANKEL_ENGINE_SIMULATOR_H */
