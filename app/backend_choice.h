#pragma once

// The one file of app/ that names a graphics backend: what each host draws through. With a
// second backend, this is where a host chooses between them.

#include "editor/gl_editor_backend.h"
#include "gl_backend.h"

namespace Vkm::App {

using Backend       = Engine::GLBackend;        ///< The runtime's: the frame, and the window it draws into.
using EditorBackend = Engine::GLEditorBackend;  ///< The editor's: the runtime's, with ImGui's renderer.

} // namespace Vkm::App
