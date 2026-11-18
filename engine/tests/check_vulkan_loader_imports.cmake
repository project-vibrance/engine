# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0




execute_process(
    COMMAND "${OBJDUMP}" -p "${ENGINE_BINARY}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE imports
    ERROR_VARIABLE diagnostic)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect engine DLL imports: ${diagnostic}")
endif()
if(NOT imports MATCHES "vulkan-1[.]dll")
    message(FATAL_ERROR "The import check did not find the Vulkan loader dependency.")
endif()
foreach(symbol vkGetDeviceBufferMemoryRequirements vkGetDeviceImageMemoryRequirements)
    if(imports MATCHES "${symbol}")
        message(FATAL_ERROR
            "${ENGINE_BINARY} statically imports Vulkan 1.3 function ${symbol}; "
            "Windows will reject this engine on a Vulkan 1.2 loader before startup.")
    endif()
endforeach()
