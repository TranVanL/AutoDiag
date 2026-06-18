#pragma once 

namespace autodiag {
    enum class UDS_Service {
        DiagnosticSessionControl = 0x10,
        ECUReset                 = 0x11,
        ReadDataByIdentifier     = 0x22,
        SecurityAccess           = 0x27,
        CommunicationControl     = 0x28,
        TesterPresent            = 0x3E,
        ControlDTCSetting        = 0x85
    };
}
