#ifndef ATG_ENGINE_SIM_BEAM_BRIDGE_H
#define ATG_ENGINE_SIM_BEAM_BRIDGE_H

// Own header so winsock never meets the windows.h delta-studio pulls in

struct BeamBridgeInput {
    float rpm;
    float throttle;
    float ignition;
    float volume;
    float pan;
};

bool beamBridgeInit(int port);
bool beamBridgeLatest(BeamBridgeInput *in, double *ageSeconds, float *smoothRpm);
double beamBridgeRpmRate(double windowSeconds);
void beamBridgeSend(float torqueNm, float rpm);
void beamBridgeSendProfile(const float *values, int count);
void beamBridgeSendAudio(const short *samples, int count, float gain);

#endif /* ATG_ENGINE_SIM_BEAM_BRIDGE_H */
