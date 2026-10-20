#ifndef ATG_ENGINE_SIM_WANKEL_COMBUSTION_CHAMBER_H
#define ATG_ENGINE_SIM_WANKEL_COMBUSTION_CHAMBER_H

#include "scs.h"

#include "gas_system.h"
#include "fuel.h"
#include "intake.h"
#include "exhaust_system.h"

class WankelEngine;
class WankelRotor;
class WankelRotorHousing;

// The three working chambers of one rotor, plus its intake and exhaust runners
class WankelCombustionChamber : public atg_scs::ForceGenerator {
    public:
        struct Parameters {
            Intake *intake;
            WankelRotor *rotor;
            WankelRotorHousing *housing;
            ExhaustSystem *exhaust;
            Fuel *fuel;
            WankelEngine *engine;
            double startingPressure;
            double startingTemperature;
            double intakeRunnerCrossSectionArea;
            double exhaustCrossSectionArea;
            double primaryLength;
            double soundAttenuation;
            double sparkPlugAngle;
        };

        struct FlameEvent {
            double lit_n = 0;
            double total_n = 0;
            double efficiency = 1.0;
            double flameSpeed = 0;
            double lastVolume = 0;
            double travel_x = 0;
            double travel_y = 0;
            double travel_z = 0;
            GasSystem::Mix globalMix;
        };

        static constexpr int StateSamples = 256;

    public:
        WankelCombustionChamber();
        virtual ~WankelCombustionChamber();

        void initialize(const Parameters &params);
        void destroy();
        virtual void apply(atg_scs::SystemState *system);

        void ignite();
        void update(double dt);
        void flow(double dt);

        double getIntakePortExposure(int chamber) const;
        double getExhaustPortExposure(int chamber) const;

        bool isLit(int chamber) const { return m_lit[chamber]; }
        bool popLitLastFrame(int chamber);

        void resetLastTimestepExhaustFlow();
        void resetLastTimestepIntakeFlow();
        double getLastTimestepTotalExhaustFlow() const;
        double getLastTimestepIntakeFlow(int chamber) const { return m_lastTimestepIntakeFlow[chamber]; }

        inline WankelRotor *getRotor() const { return m_rotor; }
        inline ExhaustSystem *getExhaustSystem() const { return m_exhaust; }
        inline double getPrimaryLength() const { return m_primaryLength; }
        inline double getSoundAttenuation() const { return m_soundAttenuation; }

        GasSystem m_chambers[3];
        GasSystem m_exhaustRunner;
        GasSystem m_intakeRunner;
        FlameEvent m_flameEvents[3];

    protected:
        double getPortExposure(int chamber, const GasSystem *runner) const;
        void getApexAngles(double *angles) const;

        WankelEngine *m_engine;
        Intake *m_intake;
        ExhaustSystem *m_exhaust;
        Fuel *m_fuel;
        WankelRotor *m_rotor;
        WankelRotorHousing *m_housing;

        double m_lastTimestepExhaustFlow[3];
        double m_lastTimestepIntakeFlow[3];

        double m_intakeRunnerCrossSectionArea;
        double m_exhaustCrossSectionArea;
        double m_primaryLength;
        double m_soundAttenuation;
        double m_sparkPlugAngle;

        bool m_lit[3];
        bool m_litLastFrame[3];

        double *m_pressure;
        double *m_flankSpeed;
};

#endif /* ATG_ENGINE_SIM_WANKEL_COMBUSTION_CHAMBER_H */
