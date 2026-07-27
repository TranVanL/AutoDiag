#include "idiag_hal.h"
#include <memory>
#include <string>

namespace autodiag {

class HalFactory {
public:
    std::unique_ptr<IDiagnosticHal> createHal(const std::string& spec);
};

} // namespace autodiag

