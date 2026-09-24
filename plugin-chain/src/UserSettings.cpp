#include "UserSettings.h"
#include "UpdateChecker.h"

using namespace juce;

namespace axprefs {

File UserSettings::file() {
    return axupdate::UpdateChecker::stateFile().getSiblingFile("settings.json");
}

UserSettings::UserSettings() {
    const File f = file();
    if (!f.existsAsFile()) return;
    const var v = JSON::parse(f);
    if (!v.isObject()) return;
    loaded_ = v;
    meterPeakHold_.store((bool) v.getProperty("meter_peak_hold", false));
}

void UserSettings::setMeterPeakHold(bool on) {
    if (meterPeakHold_.exchange(on) == on) return;
    save();
}

void UserSettings::save() const {
    auto* obj = new DynamicObject();
    if (auto* old = loaded_.getDynamicObject())
        for (auto& p : old->getProperties()) obj->setProperty(p.name, p.value);
    obj->setProperty("meter_peak_hold", meterPeakHold());
    const File f = file();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(JSON::toString(var(obj)));
}

}  // namespace axprefs
