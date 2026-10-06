#pragma once

namespace Vkm::Engine {

class EngineErrorLog;

/**
 * @brief Draw the Errors window: the recoverable failures reportError() recorded, newest first.
 *
 * @param errorLog The log the engine reports into; Clear empties it.
 */
void drawErrorsPanel(EngineErrorLog& errorLog);

} // namespace Vkm::Engine
