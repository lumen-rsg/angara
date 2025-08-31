#include "AngaraInstance.h"
#include "AngaraClass.h"

namespace angara {

    AngaraInstance::AngaraInstance(std::shared_ptr<AngaraClass> klass)
            : m_class(std::move(klass)) {}

} // namespace angara