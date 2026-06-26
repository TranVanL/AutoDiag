#pragma once
#include "diag_type.h"

namespace autodiag {

std::vector<std::uint8_t> encode(const DiagRequest& req);
DiagResponse decode(std::uint32_t reqId, const std::vector<std::uint8_t>& bytes);

}  // namespace autodiag

