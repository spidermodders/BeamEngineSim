#include "../include/wankel_rotor_object.h"

#include "../include/engine_sim_application.h"
#include "../include/ui_utilities.h"
#include "../include/utilities.h"

WankelRotorObject::WankelRotorObject() {
    m_rotor = nullptr;
    m_housing = nullptr;
}

WankelRotorObject::~WankelRotorObject() {
    /* void */
}

void WankelRotorObject::generateGeometry() {
    GeometryGenerator *gen = m_app->getGeometryGenerator();
    Crankshaft *crankshaft = m_rotor->getCrankshaft();

    const float center_x = (float)crankshaft->getPosX();
    const float center_y = (float)crankshaft->getPosY();
    const float width = (float)(m_rotor->getRadius() * 0.1);

    gen->startShape();
    gen->generateWankelHousing(
        m_housing->getProfile(), center_x, center_y, width, 0.0f, ysMath::Constants::TWO_PI, 1024);
    gen->endShape(&m_housingGeometry);

    m_portGeometry.resize(m_housing->getPortCount());
    for (int i = 0; i < m_housing->getPortCount(); ++i) {
        const WankelPeripheralPort *port = m_housing->getPort(i);

        gen->startShape();
        gen->generateWankelHousing(
            m_housing->getProfile(),
            center_x,
            center_y,
            width,
            (float)port->getStartAngle(),
            (float)(port->getStartAngle() + positiveAngularDistance(port->getStartAngle(), port->getEndAngle())),
            256);
        gen->endShape(&m_portGeometry[i]);
    }

    gen->startShape();
    gen->generateWankelRotor(m_rotor->getFlankProfile(), (float)m_rotor->getAngleOffset());
    gen->endShape(&m_rotorGeometry);
}

void WankelRotorObject::render(const ViewParameters *view) {
    const int layer = m_rotor->getRodJournal();
    if (layer > view->Layer1 || layer < view->Layer0) return;

    if (view->Sublayer == 0) {
        resetShader();
        m_app->getShaders()->SetBaseColor(
            tintByLayer(mix(m_app->getBackgroundColor(), m_app->getForegroundColor(), 0.9f), layer - view->Layer0));
        m_app->drawGenerated(m_housingGeometry, 0x32 - layer);

        m_app->getShaders()->SetBaseColor(m_app->getBackgroundColor());
        for (const GeometryGenerator::GeometryIndices &port : m_portGeometry) {
            m_app->drawGenerated(port, 0x32 - layer);
        }
    }
    else if (view->Sublayer == 1) {
        resetShader();
        setTransform(&m_rotor->m_body);
        m_app->getShaders()->SetBaseColor(
            tintByLayer(mix(m_app->getBackgroundColor(), m_app->getForegroundColor(), 0.5f), layer - view->Layer0));
        m_app->drawGenerated(m_rotorGeometry, 0x32 - layer);
    }
}

void WankelRotorObject::process(float dt) {
    /* void */
}

void WankelRotorObject::destroy() {
    /* void */
}
