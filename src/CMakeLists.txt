set (mqttAgent_src mqttAgent.cpp)

# Make build flags compiler specific
if (CMAKE_COMPILER_IS_GNUCC)
	if (CMAKE_BUILD_TYPE STREQUAL "Debug")
		set (CMAKE_C_FLAGS "-D_REETRANT -ggdb -fPIC -Wall -Wextra -pedantic -O0 -Wno-missing-field-initializers -std=gnu99")
	elseif( CMAKE_BUILD_TYPE STREQUAL "Release" )
		set (CMAKE_C_FLAGS "-D_REETRANT -DNDEBUG -fPIC -Wall -Wextra -pedantic -O3 -Wno-missing-field-initializers -std=gnu99")
	endif ()
endif (CMAKE_COMPILER_IS_GNUCC)

LINK_DIRECTORIES(/usr/local/lib )

add_executable (mqttAgent ${mqttAgent_src})

TARGET_LINK_LIBRARIES(mqttAgent mysqlclient paho-mqtt3c curl)

# The rule to install daemon binary
install (TARGETS mqttAgent
	RUNTIME
	DESTINATION bin)
