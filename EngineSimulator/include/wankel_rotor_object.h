#ifndef ATG_ENGINE_SIM_WANKEL_ROTOR_OBJECT_H
#define ATG_ENGINE_SIM_WANKEL_ROTOR_OBJECT_H

#include "simulation_object.h"

#include "wankel_rotor.h"
#include "wankel_rotor_housing.h"
#include "geometry_generator.h"

#include <vector>

class WankelRotorObject : public SimulationObject {
    public:
        WankelRotorObject();
        virtual ~WankelRotorObject();

        virtual void generateGeometry();
        virtual void render(const ViewParameters *view);
        virtual void process(float dt);
        virtual void destroy();

        WankelRotor *m_rotor;
        WankelRotorHousing *m_housing;

    protected:
        GeometryGenerator::GeometryIndices m_housingGeometry;
        std::vector<GeometryGenerator::GeometryIndices> m_portGeometry;
        GeometryGenerator::GeometryIndices m_rotorGeometry;
};

#endif /* ATG_ENGINE_SIM_WANKEL_ROTOR_OBJECT_H */
