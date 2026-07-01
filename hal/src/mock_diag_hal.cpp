#include "mock_diag_hal.h"

#include "diag_type.h"

#include <string>

namespace autodiag {

std::vector<uint8_t> makeNegativeResponse(std::uint8_t serviceId , std::uint8_t nrc) {
    return {0x7F , serviceId , nrc};
}

MockDiagnosticHal::MockDiagnosticHal() {
    is_ready = true;
    initDIDDb();
    initDTCData();
}

void MockDiagnosticHal::initDIDDb() {
    DID_db.clear();
    DID_db[static_cast<uint16_t>(DiagProperty::VIN)] = {
        'V' , 'I' , 'N' , 'F' , 'A' , 'S' , 'T' ,
        '1' , '2' , '3' , '4' , '5' , '6' , '7' , '8' , '9' , '0' , '1'
    };

    DID_db[static_cast<uint16_t>(DiagProperty::SoftwareVer)] = {
        'S' , 'W' , '_' , 'V' , '3' , '.' , '2' , '.' , '1' , '_' , 'A' , 'A' , 'O' , 'S'
    };

    DID_db[static_cast<uint16_t>(DiagProperty::SOC)] = {78};
    DID_db[static_cast<uint16_t>(DiagProperty::RPM)] = {0x32 , 0x70};
    
}


IDiagnosticHal::Result MockDiagnosticHal::SendAndReceive(const std::vector<uint8_t> &req) {
    if (!is_ready) {
        return {false , {} , "Mock Hal is not ready!"};
    }
    if (req.empty()) {
        return {false , {} , "Request is empty!"};
    }

    const uint8_t &serviceID = req[0];

    if (serviceID == static_cast<uint8_t>(UdsService::ReadDataByIdentifier)) {
        if (req.size() < 3 ) {
            return {false , {} , "Request Packet is not enough !"};
        }

        const uint16_t did = static_cast<std::uint16_t>(req[1] << 8U) |
            static_cast<std::uint16_t>(req[2]);
        
        const auto it = DID_db.find(did);
        if (it == DID_db.end()) {
            return {true, makeNegativeResponse(serviceID, static_cast<std::uint8_t>(Nrc::RequestOutOfRange)), ""};
        }

        std::vector<uint8_t> res = {
            static_cast<uint8_t>(serviceID + 0x40), 
            static_cast<uint8_t>((did >> 8U) & 0xFFU),
            static_cast<uint8_t>(did & 0xFFU)
        };

        res.insert(res.end(), (*it).second.begin(), (*it).second.end());
        return {true , res , {}};
    }

    else if (serviceID == static_cast<uint8_t>(UdsService::ReadDTC)) {
        const uint8_t subFunction = (req.size() >= 2) ? req[1] : 0x02;
        std::vector<uint8_t> res {
            static_cast<uint8_t>(serviceID + 0x40),
            static_cast<uint8_t>(subFunction)
        };
        res.insert(res.end(),DTC_payload.begin(),DTC_payload.end());
        return {true , res ,{}};
    } 

    else if (serviceID == static_cast<uint8_t>(UdsService::ClearDTC)) {
        DTC_payload.clear();
        return {true , {0x54, 0xFF, 0xFF, 0xFF} , {}};
    }
    
    else if (serviceID == static_cast<uint8_t>(UdsService::TesterPresent)) {
        return  {true , {0x7E , 0x00 } , {}};
    }

    else {
        return {true ,makeNegativeResponse(serviceID,static_cast<uint8_t>(Nrc::ServiceNotSupported)) , {}};
    }
    
}

void MockDiagnosticHal::initDTCData(){
    DTC_payload = {
        'P' , '0' , 'A' , '0' , '0' , ',' , ' ' ,
        'P' , '0' , '5' , '6' , '2'
    };
}


void MockDiagnosticHal::reset()  {
    is_ready = true;
    initDIDDb();
    initDTCData();
}

bool MockDiagnosticHal::isReady() const {
    return is_ready;
}




} // namespace autodiag 


