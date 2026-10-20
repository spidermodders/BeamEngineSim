#ifndef ATG_ENGINE_SIM_WANKEL_PINION_CONSTRAINT_H
#define ATG_ENGINE_SIM_WANKEL_PINION_CONSTRAINT_H

#include "scs.h"

// Locks the rotor to one third of the eccentric shaft's rotation
class WankelPinionConstraint : public atg_scs::Constraint {
public:
    WankelPinionConstraint();
    virtual ~WankelPinionConstraint();

    void setBodies(atg_scs::RigidBody *shaft, atg_scs::RigidBody *rotor);

    virtual void calculate(Output *output, atg_scs::SystemState *system);

    double m_ks;
    double m_kd;
};

#endif /* ATG_ENGINE_SIM_WANKEL_PINION_CONSTRAINT_H */
