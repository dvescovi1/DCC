cpmaddpackage("gh:STMicroelectronics/STM32CubeH5@1.5.0")

function(add_stm32_target TARGET)
  file(GLOB_RECURSE SRC
       ${STM32CubeH7_SOURCE_DIR}/Drivers/STM32H5xx_HAL_Driver/*.c
       src/startup_stm32h563zitx.s src/bsp.c)
  # Filter unwanted .template files
  list(FILTER SRC EXCLUDE REGEX ".*template")
  add_executable(${TARGET} ${SRC})

  # Include either decoder.cpp or command_station.cpp, but not both
  target_sources(
    ${TARGET}
    PRIVATE
      "$<IF:$<STREQUAL:${TARGET},DCCStm32Decoder>,src/decoder.cpp,src/command_station.cpp>"
  )

  target_compile_definitions(${TARGET} PUBLIC USE_FULL_LL_DRIVER USE_HAL_DRIVER
                                              STM32H563xx)

  target_include_directories(
    ${TARGET}
    PUBLIC ${STM32CubeH5_SOURCE_DIR}/Drivers/CMSIS/Core/Include
           ${STM32CubeH5_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32H5xx/Include
           ${STM32CubeH5_SOURCE_DIR}/Drivers/CMSIS/Include
           ${STM32CubeH5_SOURCE_DIR}/Drivers/STM32H5xx_HAL_Driver/Inc)

  # Use some other projects stm32h5xx_hal_conf.h and system_stm32h5xx.c files
  target_include_directories(
    ${TARGET}
    PUBLIC
      ${STM32CubeH5_SOURCE_DIR}/Projects/NUCLEO-H563ZI/Applications/ThreadX/Tx_FreeRTOS_Wrapper/Inc
  )
  target_sources(
    ${TARGET}
    PRIVATE
      ${STM32CubeH5_SOURCE_DIR}/Projects/NUCLEO-H563ZI/Applications/ThreadX/Tx_FreeRTOS_Wrapper/Src/system_stm32h5xx.c
  )

  target_link_libraries(${TARGET} PRIVATE DCC::DCC)

  target_link_libraries(
    ${TARGET} PRIVATE --specs=nano.specs -Wl,--gc-sections,-Map=${TARGET}.map
                      -T${CMAKE_CURRENT_SOURCE_DIR}/STM32H563xx_FLASH.ld)

  # Create .hex files post build
  add_custom_command(
    TARGET ${TARGET}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${TARGET} ${TARGET}.hex
    COMMAND ${CMAKE_OBJDUMP} --source --all-headers --demangle --line-numbers
            --wide ${TARGET} > ${TARGET}.lst
    COMMAND ${CMAKE_SIZE} --format=berkeley ${TARGET})
endfunction()

add_stm32_target(DCCStm32Decoder)
add_stm32_target(DCCStm32CommandStation)
