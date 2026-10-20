#ifndef ATG_ENGINE_SIM_WANKEL_ENGINE_H
#define ATG_ENGINE_SIM_WANKEL_ENGINE_H

#include "engine.h"

#include "wankel_rotor.h"
#include "wankel_rotor_housing.h"
#include "wankel_combustion_chamber.h"

// Rotary engine: no cylinders; each rotor rides an eccentric shaft journal inside its own housing
class WankelEngine : public Engine {
    public:
        struct RotorParameters {
            int rotorCount;
            int housingResolution = 256;
            double heatTransferCoefficient = 1000.0;
            double blockTemperature = units::celcius(190.0);
        };

    public:
        WankelEngine();
        virtual ~WankelEngine();

        void initializeRotors(const RotorParameters &params);
        virtual void destroy() override;

        virtual void calculateDisplacement() override;
        virtual Simulator *createSimulator(Vehicle *vehicle, Transmission *transmission) override;

        // Housing profiles first: the chamber volume tables are traced against them
        void generateHousingProfiles();
        void precomputeRotorFunctions();

        inline int getRotorCount() const { return m_rotorCount; }
        inline WankelRotor *getRotor(int i) const { return &m_rotors[i]; }
        inline WankelRotorHousing *getHousing(int i) const { return &m_housings[i]; }
        inline WankelCombustionChamber *getRotorChamber(int i) const { return &m_rotorChambers[i]; }

        inline double getHeatTransferCoefficient() const { return m_heatTransferCoefficient; }
        inline double getBlockTemperature() const { return m_blockTemperature; }

    protected:
        WankelRotor *m_rotors;
        WankelRotorHousing *m_housings;
        WankelCombustionChamber *m_rotorChambers;
        int m_rotorCount;
        int m_housingResolution;

        double m_heatTransferCoefficient;
        double m_blockTemperature;
};

#endif /* ATG_ENGINE_SIM_WANKEL_ENGINE_H */
