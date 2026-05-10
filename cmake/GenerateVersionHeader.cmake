# Generates pyrite64_version.h with a fresh build timestamp.
# Accepts variables: OUTPUT_FILE, PROJECT_VERSION, BUILD_TIMESTAMP (optional override).
if(NOT BUILD_TIMESTAMP)
    if(CMAKE_HOST_WIN32)
        execute_process(
            COMMAND powershell -NoProfile -Command
                "[TimeZoneInfo]::ConvertTimeBySystemTimeZoneId((Get-Date), 'Eastern Standard Time').ToString('yyyyMMdd-HHmmss')"
            OUTPUT_VARIABLE BUILD_TIMESTAMP
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    else()
        execute_process(
            COMMAND env TZ=America/New_York date +%Y%m%d-%H%M%S
            OUTPUT_VARIABLE BUILD_TIMESTAMP
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
    if(NOT BUILD_TIMESTAMP)
        string(TIMESTAMP BUILD_TIMESTAMP "%Y%m%d-%H%M%S")
    endif()
endif()

file(WRITE "${OUTPUT_FILE}"
    "#pragma once\n#define PYRITE_VERSION \"${PROJECT_VERSION}-Full-Frontal-${BUILD_TIMESTAMP}\"\n"
)
