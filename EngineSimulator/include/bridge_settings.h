#ifndef ATG_ENGINE_SIM_BRIDGE_SETTINGS_H
#define ATG_ENGINE_SIM_BRIDGE_SETTINGS_H

#include <string>


void bridgeSettingsStart(const std::string &initialEnginePath);
float bridgeSettingsVolume();
float bridgeSettingsBeamIdleRpm();
double bridgeSettingsSimulationFrequency();
bool bridgeSettingsTuneBeamEngine();
bool bridgeSettingsTakeEnginePath(std::string *path);
std::string bridgeSettingsLastEnginePath();
bool bridgeSettingsTakeRetune();
void bridgeSettingsSetLoadError(const std::string &message);
void bridgeSettingsSetEnginePath(const std::string &path);
bool bridgeSettingsConnect();
void bridgeSettingsReport(bool linked, float fps);
void bridgeSettingsSetAppIcon();

#endif /* ATG_ENGINE_SIM_BRIDGE_SETTINGS_H */
