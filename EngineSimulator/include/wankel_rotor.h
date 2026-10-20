#ifndef ATG_ENGINE_SIM_WANKEL_ROTOR_H
#define ATG_ENGINE_SIM_WANKEL_ROTOR_H

#include "part.h"

#include "crankshaft.h"
#include "function.h"

class WankelRotorHousing;
class WankelRotor : public Part {
    public:
        struct Parameters {
            double mass;
            double momentOfInertia;
            double radius;
            double crownGearRadius;
            double angle;
            double depth;
            double depressionVolume;
            Crankshaft *crankshaft;
            int rodJournal;
            int flankResolution = 256;
        };

        // Rotor turns once per three eccentric shaft turns; chamber cycle spans the full 6 pi
        static constexpr int ChamberSamples = 4096;

    public:
        WankelRotor();
        virtual ~WankelRotor();

        void initialize(const Parameters &params);
        virtual void destroy();

        double getAngle() const { return m_crankshaft->getAngle() / 3.0; }
        void getApexPositions(double *x, double *y) const;
        void getFlankNormal(int flank, double *x, double *y) const;
        void getHousingCenter(double *x, double *y) const;

        double getChamberVolume(int chamber) const;
        double getChamberPerimeter(int chamber) const;

        void generateFlankProfile();
        void calculateFlankArea();
        void sampleChamber(const WankelRotorHousing *housing);

        inline Crankshaft *getCrankshaft() const { return m_crankshaft; }
        inline int getRodJournal() const { return m_rodJournal; }
        inline double getRadius() const { return m_radius; }
        inline double getDepth() const { return m_depth; }
        inline double getAngleOffset() const { return m_angleOffset; }
        inline double getFlankArea() const { return m_flankArea; }
        inline const Function *getFlankProfile() const { return &m_flankProfile; }
        inline double getMass() const { return m_mass; }
        inline double getMomentOfInertia() const { return m_momentOfInertia; }
        inline double getMaxVolume() const { return m_maxVolume; }
        inline double getMinVolume() const { return m_minVolume; }

    protected:
        double calculateChamberArea(int chamber, const WankelRotorHousing *housing) const;
        double calculateChamberPerimeter(int chamber, const WankelRotorHousing *housing) const;
        double sampleCycle(const Function &f, int chamber) const;

        int m_rodJournal;
        int m_flankResolution;
        double m_radius;
        double m_crownGearRadius;
        double m_mass;
        double m_momentOfInertia;
        double m_angleOffset;
        double m_depth;
        double m_depressionVolume;
        double m_flankArea;
        double m_maxVolume;
        double m_minVolume;

        Crankshaft *m_crankshaft;

        Function m_flankProfile;
        Function m_chamberPerimeter;
        Function m_chamberVolume;
};

#endif /* ATG_ENGINE_SIM_WANKEL_ROTOR_H */
