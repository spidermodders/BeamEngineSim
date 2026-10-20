#ifndef ATG_ENGINE_SIM_WANKEL_PERIPHERAL_PORT_NODE_H
#define ATG_ENGINE_SIM_WANKEL_PERIPHERAL_PORT_NODE_H

#include "object_reference_node.h"

#include "function_node.h"

#include "engine_sim.h"

namespace es_script {

    class WankelPeripheralPortNode : public ObjectReferenceNode<WankelPeripheralPortNode> {
    public:
        WankelPeripheralPortNode() { /* void */ }
        virtual ~WankelPeripheralPortNode() { /* void */ }

        void generate(WankelPeripheralPort *port, GasSystem *runner, EngineContext *context) const {
            WankelPeripheralPort::Parameters params;
            params.startAngle = m_startAngle;
            params.endAngle = m_endAngle;
            params.flowRate = m_flowRate->generate(context);
            params.runner = runner;

            port->initialize(params);
        }

    protected:
        virtual void registerInputs() {
            addInput("start_angle", &m_startAngle);
            addInput("end_angle", &m_endAngle);
            addInput("flow_rate", &m_flowRate, InputTarget::Type::Object);

            ObjectReferenceNode<WankelPeripheralPortNode>::registerInputs();
        }

        virtual void _evaluate() {
            setOutput(this);

            // Read inputs
            readAllInputs();
        }

        double m_startAngle = 0.0;
        double m_endAngle = 0.0;
        FunctionNode *m_flowRate = nullptr;
    };

} /* namespace es_script */

#endif /* ATG_ENGINE_SIM_WANKEL_PERIPHERAL_PORT_NODE_H */
