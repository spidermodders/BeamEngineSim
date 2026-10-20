#ifndef ATG_ENGINE_SIM_WANKEL_ROTOR_HOUSING_H
#define ATG_ENGINE_SIM_WANKEL_ROTOR_HOUSING_H

#include "part.h"

#include "function.h"
#include "gas_system.h"

class WankelRotor;

// Opening in the housing wall between two angles; flow scales with how much of a chamber faces it
class WankelPeripheralPort : public Part {
    public:
        struct Parameters {
            double startAngle;
            double endAngle;
            Function *flowRate;
            GasSystem *runner;
        };

    public:
        WankelPeripheralPort();
        virtual ~WankelPeripheralPort();

        void initialize(const Parameters &params);

        inline double getStartAngle() const { return m_startAngle; }
        inline double getEndAngle() const { return m_endAngle; }
        inline Function *getFlowRate() const { return m_flowRate; }
        inline GasSystem *getRunner() const { return m_runner; }
        inline void getDirection(double *x, double *y) const { *x = m_dx; *y = m_dy; }

    protected:
        Function *m_flowRate;
        GasSystem *m_runner;
        double m_startAngle;
        double m_endAngle;
        double m_dx;
        double m_dy;
};

class WankelRotorHousing : public Part {
    public:
        struct Parameters {
            int portCount;
            WankelRotor *rotor;
        };

    public:
        WankelRotorHousing();
        virtual ~WankelRotorHousing();

        void initialize(const Parameters &params);
        virtual void destroy();

        void generateHousingProfile(int resolution);
        int findProfileSegment(double angle) const;
        void getProfilePoint(int i, double *x, double *y, double *angle) const;
        inline int getProfileSize() const { return m_profile.getSampleCount(); }
        inline const Function *getProfile() const { return &m_profile; }

        inline WankelRotor *getRotor() const { return m_rotor; }
        inline int getPortCount() const { return m_portCount; }
        inline WankelPeripheralPort *getPort(int i) const { return &m_ports[i]; }

    protected:
        WankelRotor *m_rotor;
        Function m_profile;
        WankelPeripheralPort *m_ports;
        int m_portCount;
};

#endif /* ATG_ENGINE_SIM_WANKEL_ROTOR_HOUSING_H */
