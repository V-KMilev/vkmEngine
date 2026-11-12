#include "panels/inspector/cards.h"

#include "ecs/component/net/net_local.h"
#include "ui/editor_widgets.h"
namespace Vkm::Engine {

CardInfo cardInfo(Tag<NetLocal>) {
    const char* hint = "Kept off the wire: every end makes its own";
    return {"Net Local", AddSection::Net, hint, EditorIcon::NetLocal};
}

void drawCard(Rows<NetLocal>&, CardContext&) {
    cardNote(Note::Info, "It and everything below stay off the wire: each end runs its own, as scenery.");
}

} // namespace Vkm::Engine
