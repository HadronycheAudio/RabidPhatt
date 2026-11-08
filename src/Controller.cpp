#include "Controller.h"

#include "Editor.h"

namespace RabidPhatt {

Steinberg::IPlugView* Controller::createEditor ()
{
    return new Editor (this);
}

} // namespace RabidPhatt
