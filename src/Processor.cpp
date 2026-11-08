#include "Processor.h"

#include "Controller.h"

namespace RabidPhatt {

Processor::Processor () : RustProcessor (Controller::getClassFUID ())
{
}

} // namespace RabidPhatt
