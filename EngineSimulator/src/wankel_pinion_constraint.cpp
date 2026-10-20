#include "../include/wankel_pinion_constraint.h"

#include <cfloat>

WankelPinionConstraint::WankelPinionConstraint() : Constraint(1, 2) {
    m_ks = 10.0;
    m_kd = 1.0;
}

WankelPinionConstraint::~WankelPinionConstraint() {
    /* void */
}

void WankelPinionConstraint::setBodies(atg_scs::RigidBody *shaft, atg_scs::RigidBody *rotor) {
    m_bodies[0] = shaft;
    m_bodies[1] = rotor;
}

void WankelPinionConstraint::calculate(Output *output, atg_scs::SystemState *system) {
    output->C[0] = 0;

    output->J[0][0] = 0.0;
    output->J[0][1] = 0.0;
    output->J[0][2] = 1.0;

    output->J[0][3] = 0.0;
    output->J[0][4] = 0.0;
    output->J[0][5] = -3.0;

    output->J_dot[0][0] = 0;
    output->J_dot[0][1] = 0;
    output->J_dot[0][2] = 0;

    output->J_dot[0][3] = 0;
    output->J_dot[0][4] = 0;
    output->J_dot[0][5] = 0;

    output->kd[0] = m_kd;
    output->ks[0] = m_ks;

    output->v_bias[0] = 0;

    output->limits[0][0] = -DBL_MAX;
    output->limits[0][1] = DBL_MAX;
}
