#ifndef ATG_ENGINE_SIM_WANKEL_ROTOR_NODE_H
#define ATG_ENGINE_SIM_WANKEL_ROTOR_NODE_H

#include "object_reference_node.h"

#include "engine_sim.h"

namespace es_script {

    class WankelRotorNode : public ObjectReferenceNode<WankelRotorNode> {
    public:
        WankelRotorNode() { /* void */ }
        virtual ~WankelRotorNode() { /* void */ }

        void generate(WankelRotor *rotor, Crankshaft *crankshaft, int rodJournal, int flankResolution) const {
            WankelRotor::Parameters params = m_parameters;
            params.crankshaft = crankshaft;
            params.rodJournal = rodJournal;
            params.flankResolution = flankResolution;

            rotor->initialize(params);
        }

    protected:
        virtual void registerInputs() {
            addInput("mass", &m_parameters.mass);
            addInput("moment_of_inertia", &m_parameters.momentOfInertia);
            addInput("radius", &m_parameters.radius);
            addInput("crown_gear_radius", &m_parameters.crownGearRadius);
            addInput("angle", &m_parameters.angle);
            addInput("depth", &m_parameters.depth);
            addInput("depression_volume", &m_parameters.depressionVolume);

            ObjectReferenceNode<WankelRotorNode>::registerInputs();
        }

        virtual void _evaluate() {
            setOutput(this);

            // Read inputs
            readAllInputs();
        }

        WankelRotor::Parameters m_parameters;
    };

} /* namespace es_script */

#endif /* ATG_ENGINE_SIM_WANKEL_ROTOR_NODE_H */
