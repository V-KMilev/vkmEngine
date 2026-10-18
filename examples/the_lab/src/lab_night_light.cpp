#include "lab_night_light.h"

#include "ecs/component/render/light.h"
#include "ecs/component/render/mesh.h"
#include "ecs/environment.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"

namespace Lab {

void LabNightLight::onStart() {
    m_glow = HierarchyOperations::findNamed(scene(), entity(), "Glow");
}

void LabNightLight::onUpdate(float) {
    const bool lit = scene().environment().sky.sunElevation < below;
    if (Light* light = tryGet<Light>()) light->enabled = lit;
    if (Mesh* glow = scene().tryGet<Mesh>(m_glow)) glow->visible = lit;
}

} // namespace Lab
