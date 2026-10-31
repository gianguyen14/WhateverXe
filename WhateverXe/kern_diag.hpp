//
//  kern_diag.hpp
//  WhateverXe
//

#pragma once
#include <IOKit/IORegistryEntry.h>

// Owner of WhateverXe's diagnostic records. Without -wexdiag this is a private
// entry outside every plane: a client that copies the registry root's
// properties (PerfPowerServices does, continuously) would otherwise serialize
// ~1100 records and run the live serializers, several MB per call.
IORegistryEntry *wexDiagRoot();
